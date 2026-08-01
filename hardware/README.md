# Cardia — Hardware

Analog front end, wiring, electrode placement and bring-up for the live-capture
path of the Cardia ECG classifier.

## 1. Overview and status

| Item | State |
|---|---|
| NUCLEO-F446RE (STM32F446RE, Cortex-M4F, 180 MHz, 512 KB flash, 128 KB RAM) | on hand |
| AD8232 single-lead front end + cable + electrodes | **not yet purchased** — see the BOM |
| Firmware signal chain (ADC → bandpass → Pan-Tompkins → int8 CNN → UART report) | validated over HIL |

The analog front end is deliberately the **last** phase of this project, and
nothing upstream of it depends on the part arriving.

The firmware has two input sources behind one interface:

```
 live mode:  electrodes -> AD8232 -> PA0 (ADC1_IN0) -.
                                                      >-- ring buffer -> DSP -> QRS -> CNN -> UART report
 HIL mode:   MIT-BIH record -> UART -> frame decoder -'
```

In HIL (hardware-in-the-loop) mode the host streams a MIT-BIH record to the
board sample-by-sample over the same UART that carries the report, and the board
runs the identical filter, detector and inference code it would run on live
samples. Because MIT-BIH is annotated beat-by-beat, that path is scored against
ground truth and against the host simulator on the same input — a far stronger
correctness gate than any electrode session could be. Every firmware claim
(filter response, detector sensitivity, classifier accuracy, cycle budget) is
established there.

What the AD8232 adds is narrow: proof that the board works on a real body in
real time, not proof that the algorithm is correct. Build it in the final week;
if the part never shows up, the project still stands.

Sampling is **exactly 360 Hz**, matching MIT-BIH, so no resampling stage exists
between the two input paths. TIM2 triggers ADC1 conversions in hardware and DMA
writes them into a double buffer; the CPU never polls the ADC and jitter is
crystal-limited rather than software-limited.

---

## 2. SAFETY — read before anything is connected to a person

This section is not boilerplate. It is the part of the build with a
non-recoverable failure mode.

### The rules

1. **Battery power or a fully-isolated USB link only, whenever electrodes are on a
   person.** A USB power bank into CN1, or a laptop running on its own battery
   with the charger *physically unplugged*, or a certified USB isolator between
   laptop and board. Never a board plugged into a mains-connected laptop,
   charger, dock, or powered hub.
2. **Never share ground with mains-powered test equipment while electrodes are
   attached.** A benchtop scope's probe ground clip is bonded to protective
   earth. Clipping it anywhere on this circuit earths the subject through the
   electrodes. Same for a mains-powered external debugger. Probe the front end
   with a dummy load, never with a person in the loop.
3. **The AD8232 breakout provides no patient isolation.** No isolation barrier,
   no defibrillation protection, no leakage-current testing, no certification of
   any kind. The electrode is directly wired to the board's analog input and its
   reference is the board's own ground.
4. **This is not a medical device.** Not for diagnosis, monitoring, screening or
   treatment. Any classification it produces is an engineering output, not a
   clinical finding.
5. **Never on anyone with a pacemaker, ICD, or any implanted electrical device.**
6. **Only ever on yourself.** Not friends, not family, not "just to see if it
   works on someone else".

### Why — the actual mechanism

Skin is the body's protective impedance: dry skin is on the order of 100 kΩ, and
that is what keeps small fault voltages harmless. A gelled Ag/AgCl electrode
deliberately defeats it, dropping the contact impedance to a few kΩ. Currents
that would be imperceptible through dry contact become significant through a
prepped electrode.

If the board's ground is referenced to mains earth, the electrode becomes a
low-impedance path from the subject to earth. That is fine until something
faults. A shorted Y-capacitor or a failed insulation barrier in a cheap charger
can place line potential on the low-voltage side, and the subject's chest is now
in series with it. The risk is not the normal-condition leakage — it is the
single-fault condition, which is exactly what medical-grade isolation exists to
survive.

Even without a fault, mains adapters leak: switch-mode supplies bridge primary
and secondary with Y-capacitors, which pass a mains-frequency current — commonly
one to a few hundred µA — onto the DC output, and therefore onto the board
ground and the electrodes. IEC 60601-1 limits patient leakage current on a type
CF applied part (what an ECG electrode is) to **10 µA normal condition, 50 µA
single-fault**; type BF allows 100 µA / 500 µA. An ordinary laptop brick is one
to two orders of magnitude outside those limits before anything has gone wrong.

Certified equipment closes this gap with **galvanic isolation** — an isolated
DC-DC converter plus optical, transformer, or capacitive digital isolators — so
that no continuous conductive path exists between the patient connection and
mains-referenced circuitry, and leakage is bounded by design and verified by
test. This build has none of that.

**Microshock**, strictly speaking, is the case where a conductive path reaches
the myocardium directly: with an intracardiac catheter or a pacing lead, currents
as low as ~10–100 µA can induce ventricular fibrillation, versus roughly 100 mA
required at the skin surface. Surface electrodes on a healthy subject are not an
intracardiac path — but a pacemaker or ICD lead *is* one, permanently installed.
That asymmetry is why rule 5 is absolute rather than cautious.

### The practical safe setup

| Option | Cost | Notes |
|---|---|---|
| USB power bank into CN1, board logs to flash or reports later | ~$20 | Simplest true isolation. Nothing mains-referenced anywhere. |
| USB isolator (ADuM3160 / ADuM4160 class) between laptop and board | ~$30–40 | Keeps the live UART stream to the host. What to buy if you want to watch the trace in real time. |
| Laptop on battery, charger unplugged from the wall | free | Acceptable. A two-prong adapter is **not** a substitute — the Y-cap leakage path is still there. |

The ADuM3160 is a full/low-speed isolator and the ST-LINK VCP enumerates as full
speed, so it works. Hobby-grade isolators are rated for basic insulation, not
2×MOPP patient protection: they raise the floor, they do not make this a medical
device.

---

## 3. Bill of materials

Prices are approximate, in CAD, as of 2026, before shipping and duty. Canadian
availability is the main reason for the supplier column — several of these are
cheaper direct from AliExpress but arrive in weeks, not days.

| Item | Part number | Supplier | ~CAD | Link |
|---|---|---|---|---|
| AD8232 single-lead heart rate monitor breakout | SparkFun SEN-12650 | SparkFun / Digi-Key CA / Mouser CA | 30 | https://www.sparkfun.com/products/12650 |
| ↳ budget alternative: AD8232 clone module | generic "AD8232 ECG module" | AliExpress / Amazon.ca | 8–12 | usually ships with cable + 3 electrodes |
| 3-lead sensor cable, 3.5 mm TRS | SparkFun CAB-12970 (or bundled) | SparkFun / Amazon.ca | 8 | often included with the clone modules |
| Disposable Ag/AgCl ECG electrodes, 30–50 pack | Kendall H124SG / 3M 2560 / SEN-12969 | Amazon.ca / medical supply | 12–18 | check the expiry date — see §5 |
| Jumper wires, female–male, 20 pc | generic | Amazon.ca / local | 5 | short leads; long analog runs pick up hum |
| USB isolator, ADuM3160/4160 based | generic module | Amazon.ca / AliExpress | 30–40 | optional but recommended — see §2 |
| USB power bank (any) | — | already owned | 0–20 | the cheapest correct isolation |
| LiPo 1000–2000 mAh + charger | — | AliExpress / local | 15–25 | optional; a power bank is simpler and needs no boost stage |

**Minimum build** — clone module with bundled cable, one pack of electrodes,
jumpers, powered from a power bank already owned: **~CAD 30–40**.

**Recommended build** — SparkFun SEN-12650 (documented, known-good schematic),
proper cable, 50-pack of electrodes, jumpers, USB isolator:
**~CAD 90–110 landed**.

The clones implement the same datasheet reference circuit and generally work.
What you give up is a published schematic, a known-good 3.5 mm jack, and any
confidence in the filter-network tolerances. For a one-week bring-up where
debugging time costs more than the part, buy the SparkFun board.

---

## 4. Wiring

```
         AD8232 breakout                              NUCLEO-F446RE
      (SparkFun SEN-12650)                    (STM32F446RE, Cortex-M4F @180 MHz)
     +---------------------+                  +-----------------------------------+
     |                 3.3V|>-----------------|3V3   CN6-4                        |
     |                  GND|>-----------------|GND   CN6-6      (single tie point)|
     |               OUTPUT|>-----------------|PA0   CN8-1 / A0   ADC1_IN0        |
     |                  LO+|>-----------------|PB0   CN8-4 / A3   GPIO in, pull-up|
     |                  LO-|>-----------------|PB1   CN10-24      GPIO in, pull-up|
     |                  SDN|   n/c            |                                   |
     |                     |                  |PA2   CN10-35  USART2_TX  --\ VCP  |
     |   [3.5 mm TRS jack] |                  |PA3   CN10-37  USART2_RX  --/ 8N1  |
     +----------+----------+                  +----------------+------------------+
                |                                              |
                | 3-lead cable                                 | CN1 (ST-LINK USB)
                v                                              v
        RA  /  LA  /  RL  electrodes                  power bank, or laptop
                                                      behind a USB isolator
```

| AD8232 pin | Nucleo pin | Header | Function / note |
|---|---|---|---|
| 3.3V | 3V3 | CN6-4 | Front end draws ~170 µA typical. Do not feed it 5 V. |
| GND | GND | CN6-6 | **One** ground wire only. A second tie point creates a loop that shows up as hum. |
| OUTPUT | PA0 | CN8-1 (A0) | ADC1_IN0. TIM2_TRGO-triggered, DMA to a double buffer. |
| LO+ | PB0 | CN8-4 (A3) | Leads-off, RA electrode. High = electrode detached. |
| LO- | PB1 | CN10-24 | Leads-off, LA electrode. High = electrode detached. |
| SDN | — | — | Active-low shutdown; pulled high on the breakout. Leave unconnected, or tie to 3V3 if the wiring run is long. |
| — | PA2 / PA3 | CN10-35 / -37 | USART2 to the ST-LINK virtual COM port. Keep clear — do not reuse D0/D1. |

Header positions are from UM1724 (Nucleo-64); confirm against the silkscreen
before wiring, and confirm the AD8232 pin order against your own board — the
clone layouts are not all identical.

Configure LO+/LO- as inputs **with internal pull-up**. The AD8232 drives them
push-pull when powered, so the pull-up is invisible in normal operation, but it
makes an unpowered or disconnected front end read as *leads off* rather than
floating noise. The firmware suppresses reporting on leads-off, so the fail-safe
direction matters.

### Signal budget

The AD8232 outputs a single-ended signal referenced to mid-supply. The SparkFun
board wires the datasheet's cardiac-monitor configuration: instrumentation-amp
gain of 100 followed by an ~11× output stage, ≈**1100** total, with a two-pole
0.5 Hz high-pass and a two-pole 40 Hz low-pass in the analog path.

| Quantity | Value |
|---|---|
| Output bias | ~1.65 V (VS/2) |
| Output swing | roughly ±1 V for a ~1 mV surface ECG |
| ADC | 12-bit, VREF+ = VDDA = 3.3 V |
| LSB at the pin | 805.7 µV |
| LSB referred to the electrodes | 805.7 µV / 1100 ≈ **0.73 µV** |
| Range used | ~2480 of 4096 codes ≈ 61 %, ~11.3 effective bits |

Two consequences. First, no external biasing, level shifting or extra gain stage
is needed — a mid-supply-centred ±1 V signal already fits a 0–3.3 V single-ended
ADC with headroom for electrode offset drift. Second, 0.73 µV per code sits well
below the front-end and skin-electrode noise floor (tens of µV), so the ADC is
not the limiting element; it is also finer than MIT-BIH's own 4.88 µV/LSB
(11 bits over 10 mV), so the live path is not quantisation-disadvantaged
relative to the training data.

Use a long ADC sampling time (480 cycles at ~22.5 MHz ADCCLK is 21 µs, against a
2.78 ms sample period) — there is no throughput pressure at 360 Hz, and it
removes source-impedance error entirely. Keep the OUTPUT wire short, twisted
with its ground return, and away from the USB cable and the ST-LINK end of the
board; add 100 nF across the module's supply pins if the resting trace is noisy.

**Known cascade caveat:** the analog front end already band-limits at 0.5–40 Hz,
and the firmware applies its own 0.5–40 Hz digital bandpass. Live samples
therefore pass through both, while HIL samples pass only through the digital
one. The passband edges are gentle enough that QRS morphology is not materially
changed, but a small live-vs-HIL difference in T-wave amplitude is expected and
is not a firmware fault.

---

## 5. Electrode placement — match the training lead

The classifier was trained on MIT-BIH channel 0, which in almost every record is
**MLII** (modified limb lead II) — `ml/cardia/dataset.py` selects the lead by
name for exactly this reason. Morphology is lead-dependent: the same beat looks
different from a different vector, so a model trained on MLII and fed another
lead is being asked a question it was never trained on. Placement is the
difference between the live path meaning something and meaning nothing.

### The lead definitions, precisely

Einthoven's triangle defines three limb leads as potential differences:

| Lead | Definition | Axis |
|---|---|---|
| I | LA − RA | 0° (right shoulder → left shoulder) |
| **II** | **LL − RA** | **+60° (right shoulder → left hip)** |
| III | LL − LA | +120° |

The AD8232's three-electrode configuration has one positive input, one negative
input, and one driven reference (right-leg drive), measuring LA(+) − RA(−).
With electrodes actually on the two arms that is **Lead I**, not Lead II — so
the default placement in most AD8232 quick-start guides (RA below the right
collarbone, LA below the left collarbone, RL on the abdomen) gives Lead I.
**That is the wrong lead for this model.**

### The placement to use — MLII approximation

MLII is Lead II recorded from the torso rather than the limbs: this is what
Holter monitors do, and what MIT-BIH used. Reproduce it by moving the positive
electrode from the left shoulder down to where LL effectively sits:

| Cable | Electrode site | Role |
|---|---|---|
| RA (−) | Below the **right** collarbone, mid-clavicular, over bone not muscle | negative input |
| LA (+) | **Lower left ribcage**, anterior axillary line, around the 5th–6th intercostal space | positive input — stands in for LL |
| RL | Lower **right** abdomen / right hip bone | driven reference (right-leg drive) |

That puts the measurement vector along right shoulder → left hip, the Lead II
axis at +60°, with both electrodes on the torso — which is MLII by definition.
Placing electrodes over bone rather than muscle belly (clavicle, lower rib, iliac
crest) is what keeps EMG out of the trace.

Cable colours on the SparkFun cable are black = RA, blue = LA, red = RL. Clone
cables vary; verify against your own labelling before trusting the colour.

**Polarity check:** with correct placement the R wave points **up**. If the QRS
is inverted, RA and LA are swapped — swap the electrodes rather than negating in
firmware, because the model expects upright MLII morphology and a sign flip in
software fixes the amplitude but not the asymmetry of the ST-T segment.

### Skin prep

The single largest determinant of trace quality, and the cheapest thing to get
right.

- Wipe each site with an alcohol swab and **let it dry completely**. Wet alcohol
  raises impedance and drifts.
- Abrade very lightly with dry gauze — removing the outer dead-skin layer drops
  contact impedance more than anything else you can do.
- Avoid hair. Shave the site if needed; hair holds the electrode off the skin.
- Use **fresh, in-date, gel-wet** electrodes. Dried-out or expired electrodes are
  the number-one cause of a noisy trace, and they fail in a way that looks like a
  firmware bug: intermittent baseline jumps, hum pickup, dropouts.
- Press each electrode down for a few seconds, then wait 30–60 s before trusting
  the trace — the half-cell potential at the skin interface takes that long to
  settle, and the 0.5 Hz high-pass takes a few seconds more to recentre.
- Strain-relieve the cable (tape a loop to the skin). Any tug on an electrode is
  a motion artefact.
- Sit still, arms supported, do not talk during a capture.

---

## 6. Bring-up checklist

Sequential. Each step has a pass criterion; do not proceed past a failing step.
Steps 1–3 involve no person at all.

**0. Precondition — the HIL parity gate (already passed before the part arrives)**
- [ ] Board and host simulator produce **bit-identical** filter output, identical
      R-peak sample indices, and identical class decisions on the same
      HIL-streamed MIT-BIH record.
- **PASS:** zero mismatches across the DS2 evaluation records.
- **If it fails:** this is a firmware problem, not a hardware one. Do not order
  electrodes to debug it. Check `cardia_config.h` against `ml/cardia/config.py`,
  the fixed-point biquad state, and the int8 requantisation shifts.

**1. Bench check — no electrodes, no person**
- [ ] Module powered from the Nucleo 3V3, cable unplugged.
- **PASS:** 3.30 V ±0.1 V at the module's 3.3V pin; total board current within a
  few hundred µA of its no-module value.
- **If it fails:** reversed supply pins, a solder bridge on the module, or a
  short in the jumper harness. Measure before assuming the module is dead.

**2. Output bias check**
- [ ] With the cable unplugged or terminated into a dummy (a 1 MΩ resistor
      between the RA and LA inputs), dump raw ADC codes over UART.
- **PASS:** the mean sits near **2048 ± 150 codes** (~1.65 V) and is stable; the
  sample-to-sample noise is a handful of codes.
- **If it fails:** railed low (≈0) or high (≈4095) means the front end is
  saturated — check SDN is not pulled low, check the 3.3 V rail under load, and
  confirm OUTPUT is on PA0 and not a neighbouring pin.

**3. Leads-off detection**
- [ ] Read PB0/PB1 while plugging and unplugging the cable / lifting an
      electrode.
- **PASS:** LO+ goes high when the RA connection is broken, LO- when LA is;
  both read low with all three electrodes connected.
- **If it fails:** confirm the GPIOs are inputs with pull-ups, confirm the
  module is in DC leads-off mode, and confirm PB1 is really CN10-24 on your
  board.

**4. Raw trace — first capture on a person (§2 rules now apply)**
- [ ] Electrodes placed per §5, board on battery or behind an isolator. Stream
      raw ADC samples to the host and plot them.
- **PASS:** a recognisable PQRST complex at rest — sharp upright R, visible P
  before it, broader T after it, at 50–90 bpm.
- **If it fails:** see the troubleshooting table below. Ninety percent of the
  time it is the electrodes, not the board.

**5. Sample rate — exactly 360 Hz**
- [ ] Timestamp the first and last of N ≥ 100,000 samples on the host and
      compute N / elapsed.
- **PASS:** 360.0 Hz ±0.1 %. Anything else invalidates every filter coefficient
  and every RR-derived feature.
- **If it fails:** check the TIM2 divider against the **APB1 timer clock, not
  SYSCLK**. At the standard 180 MHz configuration APB1 is /4 = 45 MHz and the
  timer clock is doubled to 90 MHz, so the total divider for 360 Hz is 250,000
  (e.g. PSC = 49, ARR = 4999). Computing it from 180 MHz gives 180 Hz — a clean
  factor of two that shows up downstream as a halved heart rate.

**6. Baseline-wander rejection**
- [ ] Compare raw and bandpassed streams while breathing deeply, then raising
      and lowering the arms.
- **PASS:** the raw trace shows a slow sinusoidal drift at the respiration rate;
  the filtered trace stays centred and settles within ~2 s of a step
  disturbance.
- **If it fails:** the high-pass corner is wrong or the biquad state is
  saturating. Feed a synthetic step through the same code on the host and
  compare against the Python reference.

**7. QRS detection and heart rate**
- [ ] Run Pan-Tompkins live. Count your own pulse at the wrist for 60 s and
      compare against the reported rate.
- **PASS:** every visible R peak is marked, no extra marks, and the reported
  rate is within ±2 bpm of the manual count.
- **If it fails:** check the troubleshooting table for doubled/halved rate
  before touching thresholds — the adaptive thresholds are not usually the
  problem.

**8. Classifier sanity**
- [ ] Classify at rest for 2–3 minutes.
- **PASS:** overwhelmingly class **N**, with at most occasional isolated
  non-N calls on visibly noisy beats.
- **If it fails:** a persistent non-N class on clean beats almost always means
  the input window is wrong — check R-peak alignment (BEAT_PRE = 100,
  BEAT_POST = 156), the per-beat normalisation, and the lead polarity from §5.

**9. Close the loop — re-run parity on the same image**
- [ ] Re-run step 0 on the exact firmware binary that just ran live.
- **PASS:** still zero mismatches. This proves the live-capture path did not
  regress the shared DSP/inference code.
- [ ] Save one live session to file and hand-annotate a minute of it as a
      qualitative record. It is not a benchmark and must never be reported as
      one.

### Troubleshooting

| Symptom | Most likely cause | Check |
|---|---|---|
| Flat line, no signal | Electrode not making contact; cable not seated in the jack | LO+/LO- state first — it will tell you which electrode; then re-prep skin, replace electrodes |
| Signal railed at 0 or 4095 | Front end saturated: DC offset from a dried electrode, or a supply problem | Replace electrodes; confirm 3.3 V under load; confirm SDN not low |
| 50/60 Hz hum riding on everything | Mains coupling — usually a mains-referenced ground or a long analog wire | **Go to battery power** (also the safety fix); shorten and twist the OUTPUT lead; verify a single ground tie |
| Slow rolling baseline | Respiration, motion, or poor electrode adhesion | Expected at ~0.2–0.5 Hz; if the digital high-pass is not removing it, go to step 6 |
| No R peaks detected | Amplitude too low, or inverted QRS defeating the threshold | Confirm upright R waves (§5 polarity); check the integrator output amplitude before blaming the thresholds |
| Heart rate ≈ 2× true | T-wave over-detection | The 200 ms refractory and 360 ms T-wave slope test are doing nothing — verify `PT_REFRACTORY_SAMPLES` = 72 and `PT_TWAVE_SAMPLES` = 130 for the actual sample rate |
| Heart rate ≈ 0.5× true | Sample rate is half of what the firmware believes | Step 5 — TIM2 clocked from APB1 (90 MHz), not SYSCLK |
| Intermittent spikes / dropouts | Cable tug, dry electrode, loose jumper | Strain-relieve the cable; replace electrodes; reseat the harness |

---

## 7. What a real product would need

Stated plainly, because the gap is large and pretending otherwise would be worse
than the gap itself. To move from this to something that could legally touch a
patient:

- **Galvanic isolation.** Isolated DC-DC plus digital isolators on every signal
  crossing to mains-referenced circuitry, designed and tested to 2×MOPP under
  IEC 60601-1, with patient leakage current verified against the type CF limits
  (10 µA normal, 50 µA single-fault) — not assumed.
- **Defibrillation protection.** IEC 60601-2-25 / -2-27 require the applied part
  to survive repeated defibrillation discharges and recover baseline within
  seconds. That means series current-limiting resistors, gas-discharge or clamp
  networks, and creepage/clearance designed for kilovolts.
- **Right-leg-drive and CMRR quality.** Real monitors target >90 dB CMRR at
  50/60 Hz with driven shields, ±300 mV electrode-offset tolerance, and >10 MΩ
  input impedance. The AD8232 is a good part, but this breakout, with unshielded
  jumper wire, is nowhere near that.
- **Pace-pulse detection and rejection**, per IEC 60601-2-27 — a pacing spike
  must be detected and must not be counted as a QRS.
- **Standards compliance beyond the electronics:** IEC 60601-1-2 (EMC),
  60601-1-8 (alarms), IEC 62304 (software lifecycle), ISO 14971 (risk
  management), ISO 10993 (biocompatibility of skin-contact materials).
- **Clinical validation.** Performance would have to be established on a
  prospective population that actually contains the arrhythmias being claimed,
  not on a 1975–79 database of 48 half-hour records from 47 subjects on a single
  lead. Reporting would follow ANSI/AAMI EC57. Market entry would require a
  Health Canada Class II device licence or an FDA 510(k), with a predicate.

Cardia is an engineering study: an end-to-end embedded ML pipeline — dataset
handling, inter-patient AAMI evaluation, fixed-point DSP, int8 inference,
host/target parity — running in real time on a 180 MHz Cortex-M4F. The analog
front end shows the pipeline closes on a live signal. It does not make the
result clinical, and nothing here should be read as though it were.
