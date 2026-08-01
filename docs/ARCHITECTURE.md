# Architecture and design decisions

This document records *why* the system is shaped the way it is. Where a choice
had a real alternative, the alternative is named and the reason for rejecting
it is given. Measured numbers live in [RESULTS.md](RESULTS.md).

## The shape of the problem

```
AD8232 → ADC (360 Hz) → ┬→ 0.5-40 Hz bandpass ──→ beat window ──┐
                        │                                        ├→ int8 CNN → class
                        └→ 5-15 Hz bandpass → Pan-Tompkins ──────┘
                                                    │
                                                    └→ RR intervals ─┘
```

Two constraints drove everything:

1. **The device has never met the patient.** A monitor is useful only if it
   works on someone whose ECG it has never seen. That forces the inter-patient
   evaluation protocol, and the protocol in turn forces most of the modelling
   decisions.
2. **The MCU cannot look into the future.** Every stage is causal and O(1) per
   sample with bounded state. Any Python convenience the firmware could not
   reproduce is a lie that surfaces later as a parity failure.

## Development order: dataset first, hardware last

The entire chain was built and validated against MIT-BIH before any analog
front end existed. The board runs the *same C source* the simulator runs, and
a hardware-in-the-loop mode streams an annotated record to the device over
UART, so the physical system is validated against ground truth before an
electrode is ever attached.

This is not just scheduling convenience. Debugging a classifier through a noisy
analog front end means never knowing whether a wrong answer came from the
electrodes, the ADC, the filter, the detector or the model. Fixing the input to
a known-good annotated recording removes four of those five.

## One sample rate everywhere: 360 Hz

MIT-BIH is recorded at 360 Hz, so the firmware samples at 360 Hz and there is
no resampling stage anywhere in the project.

The alternative — resample the dataset to a rounder 250 Hz — would have added
an interpolation filter to the Python side that the firmware does not have,
which is exactly the kind of asymmetry that makes host/target parity
unverifiable. It also divides exactly on the target: TIM2's clock is 90 MHz
(see the APB1 note below) and 90 MHz / 360 Hz = 250 000 = 50 × 5000, so
PSC = 49, ARR = 4999 with zero frequency error.

**The APB1 trap.** TIM2 sits on APB1. At SYSCLK = 180 MHz the APB1 prescaler is
/4, giving PCLK1 = 45 MHz — but because that prescaler is not 1, the timer
clock is doubled back to 90 MHz. Sizing the prescaler from 45 MHz gives 720 Hz;
sizing it from 180 MHz gives 180 Hz. Both are silent failures that would show up
only as wrong heart rates. `firmware/src/drivers/tim.c` carries a
`_Static_assert` on the clock relationship so a future change to the clock tree
breaks the build instead of halving the sample rate.

## Two bandpass filters, not one

The conditioning filter and the detection filter have opposite goals, so they
cannot be the same filter.

| | Conditioning | Detection |
|---|---|---|
| Band | 0.5-40 Hz | 5-15 Hz |
| Feeds | the classifier | Pan-Tompkins |
| Goal | **preserve** P/QRS/T morphology | **destroy** P and T so only QRS survives |
| Order | 4th (2 biquads) | 4th (2 biquads) |

Sharing one filter would mean either passing P and T into a QRS detector that
then has to reject them by threshold (which is what makes tall T waves such a
classic false-detection source), or stripping the P and T waves the classifier
needs. Two 4th-order IIR filters cost about 20 multiply-accumulates per sample
on a 180 MHz FPU. It is not a trade worth making.

**Butterworth**, not Chebyshev or elliptic: maximally flat passband. Arrhythmia
classification is morphology classification, so passband ripple would distort
precisely the relative amplitudes the model reads. The price is a gentler
roll-off, which is affordable because nothing important sits immediately outside
either band.

**Second-order sections, direct form I.** Direct form I costs four state words
per section instead of two. The 0.5 Hz high-pass section has poles at radius
≈0.9939 — very high Q — and direct form II's shared summing node makes that
numerically fragile in float32. Eight bytes per section is a cheap insurance
premium.

**Causal (`sosfilt`), never zero-phase (`sosfiltfilt`).** Zero-phase filtering
needs the whole record and looks into the future. A great deal of published ECG
code uses `filtfilt` and then reports results as if they were achievable
online; they are not.

**Coefficients designed offline in scipy and frozen into C.** Designing a filter
on the MCU means shipping a pole-placement routine to compute ten constants that
never change. Freezing them also guarantees host and target run numerically
identical filters, which is what makes the parity check meaningful.

## QRS detection: Pan-Tompkins

Chosen over wavelet, Hilbert-transform and learned detectors because of its
shape, not just its accuracy: O(1) per sample, fixed small state, no FFT, no
buffering of a whole beat, no dynamic allocation — and adaptive thresholds,
which matter because electrode contact degrades and signal amplitude drifts by
an order of magnitude over a recording. A fixed threshold fails within minutes.

Implementation notes that differ from a naive reading of the paper:

- The R-peak index is refined on the **conditioning-band** signal, not the
  detection band, because the beat window is cut from the conditioning band. A
  fiducial point located in a different signal than the one being windowed
  would offset every beat by the difference in group delay.
- Slope for the T-wave rule is measured in a **tight ±10-sample window** around
  each candidate's own fiducial point. Measuring it across the whole search
  window let a wide ventricular complex's two integrator humps report identical
  slopes, which broke the rule completely — see RESULTS.md.
- Search-back matters more than it looks. A missed beat corrupts *two* RR
  intervals, so it costs the classifier two beats, not one.

## Beat representation

**256 samples: 100 before the R peak, 156 after.** 711 ms total. The pre-window
covers the PR interval and P wave; the post-window covers the ST segment and T
wave. 256 is a power of two, so the ring-buffer wrap is a mask rather than a
modulo.

**Per-beat z-score normalisation.** Electrode placement, skin impedance, body
habitus and amplifier gain vary enormously between patients, and under the
inter-patient protocol every test patient is a stranger. A global scale fitted
on DS1 simply would not apply to DS2. Standardising each window makes the model
depend on shape, which transfers.

The honest cost: absolute QRS amplitude is discarded, and large amplitude is one
cue for ventricular beats. QRS *width* and the RR features still carry that
information, and width is the stronger cue. On the MCU this is two passes over
256 samples and one reciprocal square root.

## Two branches: morphology and timing

The classifier has a convolutional branch over the beat window and a small
dense branch over four RR-interval features.

This is forced by physiology, not by architecture fashion. A supraventricular
ectopic beat is conducted down the normal His-Purkinje system, so its QRS looks
essentially **normal**. What makes it ectopic is that it arrives *early*. A
morphology-only model is being asked to separate two classes that genuinely
look alike. Conversely a ventricular beat conducts cell-to-cell through
myocardium and is wide and bizarre, which morphology handles well. So: two
branches.

**The timing branch needs its own projection and its own quantisation scale.**
An earlier version concatenated the four raw features directly with the 32
pooled morphology channels. On held-out validation patients, a single threshold
on the prematurity feature beat the whole network's S sensitivity by a wide
margin — the information was present and the model was not using it. Four inputs
among thirty-six, sharing whatever scale the convolution stack produced, cannot
compete for the first layer's attention. A `Linear(4→16)` costs 64 MACs.

**One beat of latency, accepted deliberately.** Two of the four RR features
(post-RR and the post/pre ratio) need the *next* R peak, because the
compensatory pause after a beat is the classic clinical discriminator between
supraventricular and ventricular ectopy. So a beat is classified about one RR
interval after it occurs. The firmware implements this with a fixed four-slot
pending-beat queue; four slots because at 200 bpm the next R arrives before the
current beat's 156-sample post-window has even filled.

## Model sizing: budget first

The MAC budget was set before the architecture. A beat arrives at most about
three times a second even in tachycardia, and the sample-rate ISR plus the DSP
chain must keep their margin, so ~100k MACs/beat was the target. The result is
~77k MACs and ~5.4k parameters.

| Layer | Output | MACs |
|---|---|---|
| conv1 1→8, k=11, s=4 | 64×8 | 5 632 |
| maxpool 2 | 32×8 | – |
| conv2 8→16, k=7 | 32×16 | 28 672 |
| maxpool 2 | 16×16 | – |
| conv3 16→32, k=5 | 16×32 | 40 960 |
| global average pool | 32 | – |
| RR branch 4→16 | 16 | 64 |
| fc1 48→32 | 32 | 1 536 |
| fc2 32→5 | 5 | 160 |
| **total** | | **77 024** |

**Stride 4 in the first layer.** At 360 Hz a 256-sample window is heavily
redundant for a shape-classification task, and striding early is where the MAC
savings are — the same reduction applied later saves far less.

**Global average pooling instead of flatten-then-dense.** Removes 16× the head
parameters, and makes the network invariant to small errors in R-peak
localisation, which matters because Pan-Tompkins does not place the fiducial
point perfectly (the measured mean offset is a couple of samples).

**BatchNorm during float training, folded away before quantisation.** BN makes
this depth trainable with a plain schedule; at inference it is a fixed
per-channel affine map, which is exactly what a convolution's weight and bias
already are, so folding is exact and costs the MCU nothing.

## Quantisation: int8, and hand-written

The scheme is the standard TFLite/gemmlowp/CMSIS-NN one: `r = S(q − Z)`,
symmetric per-output-channel weights for convolutions, per-tensor for the dense
layers, affine per-tensor activations, int32 accumulators, and requantisation by
a saturating rounding doubling high multiply followed by a rounding shift.

It is implemented by hand rather than through `torch.ao.quantization` for one
reason: **the claim at the end of this project is that the C produces the same
answer as the model, and "the same" should mean equality, not similarity.** To
get equality the Python side must use the identical arithmetic the C side uses.
Bending a general framework into exactly that shape is more work than writing
150 lines of fake quantisation, and it leaves the numerics implicit.

Three consequences worth naming:

- **The final layer is not requantised.** Its int32 accumulators go straight to
  argmax. Requantising would quantise away the margin between the top two
  classes, which is the only thing the decision depends on. This is also why
  fc2 uses a per-tensor weight scale: with per-channel scales the five logits
  would be expressed in five different units and comparing them would be
  meaningless.
- **Zero padding pads with the quantised zero (−Z), not the byte 0.** This is
  the most common int8 convolution bug and it only shows at tensor edges. There
  is a unit test for it.
- **QAT, not post-training quantisation.** With eight filters in the first
  layer there is no redundancy to absorb rounding error. QAT puts the rounding
  inside the training loop so the loss can see it.

## Code generation instead of an interpreter

The model is emitted as straight-line C arrays rather than shipped as a
`.tflite` file with a runtime interpreter. An interpreter costs tens of
kilobytes of flash to walk a graph that is fixed at build time, and it moves the
weights into RAM. Generated C puts every weight in `.rodata`, executed in place
from flash at zero RAM cost, lets the linker garbage-collect anything unused,
and makes the whole network visible in a diff. An interpreter earns its keep
only when the model can change without a rebuild, which is not the case here.

## Bare metal, not HAL

The drivers are register-level, written against a hand-written
`firmware/include/stm32f446_regs.h` derived from RM0390 and PM0214. No STM32Cube
HAL, no vendor headers, no RTOS.

Reasons, in order of weight:

1. **The problem does not need an RTOS.** One periodic DMA callback and one
   main-loop consumer. A scheduler would add a context-switch cost, a stack per
   task, and a class of priority bugs, in exchange for nothing.
2. **Determinism.** Register writes are visible and countable. The HAL's
   `HAL_ADC_Start_DMA` hides a state machine whose timing is not obvious, which
   matters when the sample clock has to be exact.
3. **Size.** The whole image is under 16 KB including the model. The HAL alone
   is larger than that.

Static allocation throughout: no `malloc`, and `firmware/src/syscalls.c` traps
if anything reaches for the heap, so accidental heap use fails loudly instead of
working quietly until it does not.

## Portability boundary

Everything under `firmware/src/dsp/` and `firmware/src/nn/` is portable C with
no MCU dependency. Everything under `firmware/src/drivers/` plus the startup and
linker files is target-only.

That boundary is what makes the host unit tests and the simulator possible, and
it is what lets the project claim that the accuracy measured on 49 691 held-out
beats applies to the device: the thing evaluated and the thing that runs are the
same translation units, with only the compiler changed.

`-ffp-contract=off` is set on both host builds. Without it the compiler may fuse
a multiply and an add into a single FMA with one rounding step — faster and more
accurate, but it makes host and target disagree in the last bits and turns an
exact parity test into a tolerance check. Determinism beats accuracy here.

## What is deliberately not here

- **Multi-lead input.** MIT-BIH has two channels and de Chazal uses both.
  Single-lead matches the AD8232 hardware and keeps the RAM budget honest.
- **Patient-specific adaptation.** Several published systems fine-tune on a few
  annotated beats from the target patient, which lifts S sensitivity a long way.
  It also requires a cardiologist to label those beats, which changes what the
  device is.
- **CMSIS-DSP.** The biquads are hand-written so host and target run bit-identical
  filters. CMSIS-NN is wired in behind `CARDIA_USE_CMSIS_NN` because its
  requantisation matches the portable kernels exactly, so it is a drop-in.
