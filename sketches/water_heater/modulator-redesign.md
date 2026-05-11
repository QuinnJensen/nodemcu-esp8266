# Water Heater SSR Modulator Redesign

## Hardware Context

- **MCU:** ESP8266 (NodeMCU), Arduino framework
- **SSR:** Sensata/Crydom Series 1, zero-voltage turn-on type (24–280 VAC input, SCR output)
- **Load:** Resistive 120 VAC water heater (~1800 W)
- **Current sketch:** GitHub repo `QuinnJensen/nodemcu-esp8266`, path `sketches/water_heater/src` — uses a 120 Hz hardware timer (timer1) ISR running a Bresenham's line algorithm accumulator to drive a simulated SSR output pin

## Key SSR Behavior (Confirmed)

1. A **zero-voltage turn-on SSR** waits for the next mains zero crossing to begin conducting. The control input **must be asserted before and held through that zero crossing** — if the input drops before the zero crossing, that half-cycle will be skipped.
2. Once conducting, the SSR stays on until the next current zero (the next zero crossing for a resistive load), regardless of whether the input has already gone low.
3. Dropping the input low at any point causes the SSR to turn off naturally at the next zero crossing after that — it does not stop mid-cycle.

## Design Decisions

### Free-Running Timer Is Acceptable

No hardware zero-cross detector will be used. The water heater's large thermal time constant means that the occasional off-by-one-cycle error from free-running timer drift is inconsequential. Power delivery accuracy is sufficient on average.

### Switch Accumulator from 120 Hz to 60 Hz

Each "quantum" of power should be **one full mains cycle** (both half-cycles, ~16.67 ms) rather than one half-cycle. Running the Bresenham accumulator at 60 Hz makes each tick correspond to exactly one full cycle, which is the minimum useful power unit for a zero-cross SSR.

### Extended Gate Pulse Width

When the accumulator fires an "on" decision, the SSR gate input should be asserted for **slightly longer than 16.67 ms** (recommended: ~18–20 ms). This ensures the gate reliably spans both zero crossings of the full cycle even if the free-running timer is slightly out of phase with the real mains. The SSR's own zero-cross logic ensures it only turns on at actual zero crossings — the extended pulse width just guarantees the opportunity is not missed.

### No Second Hardware Timer Needed

The ESP8266 has one general-purpose hardware timer (timer1), already used for the modulator ISR. Gate pulse de-assertion can be handled entirely with a `micros()`-based one-shot in the main `loop()`, which provides sub-millisecond accuracy — more than sufficient for spanning 16.67 ms zero-crossing windows.

## Required Code Changes

### 1. Change `MODULATOR_HZ` from 120 to 60

```cpp
#define MODULATOR_HZ 60UL
```

### 2. Recalculate `TIMER1_TICKS`

The existing formula already handles this automatically:

```cpp
#define TIMER1_TICKS (5000000UL / MODULATOR_HZ - 1)
```

### 3. ISR: Assert Gate and Record One-Shot Expiry

In the ISR, when the Bresenham accumulator triggers an "on" decision, assert the SSR gate pin high and record the expiry time. Do **not** de-assert the gate from within the ISR.

```cpp
void IRAM_ATTR modulator_isr() {
    simtickcount++;
    bresacc += isrpowerpct;
    if (bresacc >= 100) {
        bresacc -= 100;
        isroutputstate = 1;
        GPOS = (1 << SSR_GATE_PIN);           // assert gate high
        gateOffAtMicros = micros() + 18500UL; // ~18.5 ms one-shot
    }
    // Do NOT de-assert gate here
}
```

### 4. `loop()`: Poll and De-Assert Gate via One-Shot

```cpp
// Declare at file scope:
volatile unsigned long gateOffAtMicros = 0;
volatile bool ssrGateOn = false;

// In loop():
if (ssrGateOn && (long)(micros() - gateOffAtMicros) >= 0) {
    digitalWrite(SSR_GATE_PIN, LOW);
    GPOC = (1 << SSR_GATE_PIN); // or use digitalWrite
    ssrGateOn = false;
    isroutputstate = 0;
}
```

### 5. Bresenham Accumulator Must Not De-Assert Gate

The ISR's "else" branch (accumulator did not fire) should leave the gate pin alone — only the `loop()` one-shot de-asserts it. This ensures the gate stays high for the full extended pulse window regardless of how many ISR ticks pass.

## Power Relationship Summary

| Waveform | RMS Voltage | Power (relative to full sine) |
|---|---|---|
| Full sine (unrectified) | \(V_m / \sqrt{2}\) | 100% |
| Half-wave rectified | \(V_m / 2\) | 50% |
| Every other half-wave dropped | \(V_m / (2\sqrt{2})\) | 25% |
| Full-cycle modulation at N% duty | \(V_{rms} \cdot \sqrt{N/100}\) | N% |

At 60 Hz full-cycle modulation, each accumulator tick represents 1/60 s of power. The Bresenham algorithm distributes "on" cycles to achieve the target average power percentage over time.
