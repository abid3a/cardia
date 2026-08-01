# Results

All numbers here were produced by running the code in this repository. Every
table states whether a figure is **measured** or **estimated**, and estimates
show their derivation.

Reproduce with:

```bash
make data          # download MIT-BIH, build the DS1/DS2 caches
make train         # float -> BatchNorm fold -> int8 QAT
make export        # generate the C weights, check the integer path
make test          # host unit tests
make parity        # stream all of DS2 through the C chain, check bit-exactness
make eval          # inter-patient vs intra-patient comparison
make firmware      # cross-compile for the STM32F446RE
```

---

## 1. The headline, and the number most projects report instead

Same architecture, same training schedule, same data. The only difference is
whether a patient is allowed to appear in both the training and the test set.

| | Intra-patient<br>(random 80/20 beat split) | **Inter-patient**<br>**(AAMI EC57, DS1→DS2)** |
|---|---|---|
| Patients shared between train and test | **44** | **0** |
| Overall accuracy | 98.99% | **92.85%** |
| N sensitivity / PPV | 99.71% / 99.35% | 96.45% / 96.30% |
| S sensitivity / PPV | 83.30% / 96.77% | **20.25% / 35.26%** |
| V sensitivity / PPV | 99.05% / 95.58% | **96.24% / 71.60%** |
| F sensitivity / PPV | 73.61% / 92.98% | **0.00% / –** |
| Macro F1 (N/S/V/F) | 92.13% | 51.05% |

**The right-hand column is the result. The left-hand column is the number to be
suspicious of whenever you see it.**

### Why the gap exists

Consecutive beats from one patient are near-duplicates. Same electrode
placement, same body habitus, same conduction pathology, and very often the same
ectopic focus firing over and over with an almost identical waveform. Pool every
beat from all 44 records, shuffle, and split 80/20, and roughly four out of five
of any given beat's near-twins land in the training set. At test time the model
is not recognising an arrhythmia; it is recognising a patient it has already
memorised.

That is textbook data leakage, and it is worth **6.14 points of overall
accuracy** here — but the aggregate badly understates it, because accuracy is
dominated by the 89% of beats that are class N. Look at the minority classes
instead:

- **S sensitivity: 83.3% → 20.3%.** A supraventricular ectopic beat is conducted
  normally through the His-Purkinje system, so its QRS looks *normal*; what makes
  it ectopic is its timing. Within a patient, "early" is a well-defined offset
  from that patient's own stable rhythm, and the model learns it easily. Across
  patients, resting rate, rhythm variability and the shape of "normal" all move,
  and the same decision boundary stops transferring.
- **F sensitivity: 73.6% → 0.0%.** This is the cleanest illustration in the
  whole dataset. DS1 has 414 fusion beats and **372 of them come from record 208
  alone**. With leakage, the model memorises what patient 208's fusion beats look
  like and scores 74% on held-out beats from that same patient. Without leakage
  it has effectively been shown one example of the class and scores zero. The
  intra-patient number is not a better model; it is the same model being asked an
  easier question.

An engineer who reports 99% here has not built a better classifier. They have
built a patient recogniser and measured it on the patients it recognises.

### How to read our own S number honestly

20.25% S sensitivity is low, and lower than a flattering narrative would
like. The context:

- de Chazal et al. (2004), the reference for this protocol, report S sensitivity
  of **75.9%** at **38.5% positive predictivity** — using *both* MIT-BIH leads,
  a much richer hand-designed feature set including record-wide RR statistics,
  and a linear discriminant with tuned class priors.
- This project uses **one lead** (matching the single-channel AD8232 hardware),
  four causal RR features, and a 5.4k-parameter network that has to fit in 6 KB
  of flash.
- Our own model reaches **80.2% S sensitivity on the DS1 patients it trained
  on** and 20.25% on DS2. The information is learnable; it does not transfer
  from 22 patients at this capacity and with this feature set.
- 75% of DS2's S beats come from **record 232**, a sick-sinus patient whose
  *normal* beats follow long pauses, so its baseline RR statistics look nothing
  like anything in DS1. Our S sensitivity on record 232 is the single biggest
  determinant of the aggregate.

Honest conclusion: **S is the weak class, the reason is a genuine distribution
shift rather than a bug, and the fix is more RR context and a second lead, not
more epochs.** See §7.

---

## 2. Beat classification, inter-patient (DS1 → DS2)

22 training patients, 22 test patients, disjoint. Paced records 102/104/107/217
excluded per AAMI EC57. 49,691 test beats, segmented at the reference
annotations so classifier errors are not confounded with detector errors (the
full-chain result is §4).

### int8 quantised model (what the firmware runs) — **measured**

```
beats: 49691   overall accuracy: 92.85%

class   support     sens      PPV       F1
N         44239  96.45%  96.30%  96.37%
S          1837  20.25%  35.26%  25.73%
V          3220  96.24%  71.60%  82.11%
F           388   0.00%    --     0.00%
Q             7   0.00%    --     0.00%

macro (N/S/V/F)  sens 53.23%  PPV 50.79%  F1 51.05%

confusion matrix (rows = truth, cols = predicted)
             N       S       V       F       Q
N        42667     633     939       0       0
S          1211     372     254       0       0
V            72      49    3099       0       0
F           354       1      33       0       0
Q             4       0       3       0       0
```

### Float model, for comparison — **measured**

| | Accuracy | S sens / PPV | V sens / PPV |
|---|---|---|---|
| Float (BN folded) | 92.77% | 16.88% / 36.00% | 97.58% / 68.54% |
| int8 QAT | **92.85%** | 20.25% / 35.26% | 96.24% / 71.60% |

Quantisation to int8 cost nothing measurable — the QAT model is fractionally
*ahead* on accuracy, which is within run-to-run noise but does show that
quantisation-aware training absorbed the rounding rather than paying for it. On
a 5.4k-parameter network there is no redundancy to spare, so this is not a
given; post-training quantisation was not attempted precisely because there is
no capacity to absorb the error.

### What the errors actually are

The dominant confusion is **N → V** (939 beats) and **S → N** (1,211 beats).

- N → V is mostly bundle-branch-block beats. Under AAMI these are class N, but
  their QRS is wide and bizarre because conduction detours through myocardium
  instead of the fast His-Purkinje system. DS2 contains four such records (111,
  214 LBBB; 212, 231 RBBB) and DS1 contains only four to learn from. This is
  also why V positive predictivity (71.60%) is well below V sensitivity (96.24%):
  the model over-calls V rather than missing it, which for a monitor is the
  safer direction of error.
- S → N is the class-S problem described above: morphologically these beats
  *are* normal, and the timing cue did not transfer.

---

## 3. QRS detection (Pan-Tompkins), inter-patient — **measured**

All 22 DS2 records streamed through the C implementation. Scored against the
reference cardiologist annotations with the EC57 150 ms matching window and a
one-to-one constraint, so a detector firing five times per QRS scores one true
positive and four false ones rather than five true positives.

| Metric | Value |
|---|---|
| Reference beats | 49,657 |
| Detections | 50,390 |
| **Sensitivity** | **99.805%** |
| **Positive predictivity** | **98.353%** |
| False positives | 830 |
| False negatives | 97 |
| Detection error rate | 1.867% |
| Mean R-peak offset | −1.77 samples (−4.9 ms) |

For reference, Pan & Tompkins reported 99.3% sensitivity and 99.5% positive
predictivity on MIT-BIH in the original 1985 paper.

The first two seconds of each record are excluded from both sides of the
comparison: that is the detector's threshold-learning phase, during which it
deliberately reports nothing. Counting those beats as misses would measure the
warm-up rather than the algorithm; dropping only the detections would flatter it.

Worst records: 222 (+P 92.08%) and 111 (+P 92.38%), both known for atrial
arrhythmia with irregular rhythm and low-amplitude QRS segments, where the
adaptive threshold tracks downward and admits noise.

### A real bug this measurement caught

Record 119 is largely ventricular bigeminy. It initially produced **442 false
detections** against 1,985 reference beats (+P 81.7%).

The cause: a wide ventricular complex produces *two* integrator humps — one from
the R upstroke, one from the deep S downstroke — separated by more than the
200 ms refractory period, so the refractory rule could not suppress the second.
The T-wave discrimination rule should have, since it fires on candidates 200–360
ms after a QRS whose slope is less than half the previous QRS's. It never fired,
because the slope was being measured across the entire 56-sample search window,
so both humps found the *same* steepest edge and the ratio came out at exactly
1.000.

Fix: measure slope in a tight ±10-sample window around each candidate's own
fiducial point. Record 119 went from **442 false detections to 7**, and overall
DS2 positive predictivity from 93.5% to 98.4%.

---

## 4. End-to-end, full chain — **measured**

The above two sections measure the classifier and the detector separately. This
one runs the complete C pipeline on raw samples — detection, segmentation,
feature extraction and inference — and matches whatever it produces against the
reference annotations. Detection errors are therefore included.

```
beats: 49600   overall accuracy: 92.33%

class   support     sens      PPV       F1
N         44196  96.16%  96.83%  96.49%
S          1827  13.25%  26.56%  17.68%
V          3185  96.01%  63.70%  76.58%
F           385   0.00%    --     0.00%
Q             7   0.00%    --     0.00%
```

The ~0.5-point drop from §2 is the cost of imperfect segmentation: beats whose R
peak was localised a few samples off produce a slightly shifted window, and S
suffers most because its RR features depend on detection timing.

---

## 5. Host/target numerical parity — **measured**

The central verification claim of the project.

| Check | Result |
|---|---|
| C pipeline vs numpy integer reference, DS2 | **50,374 / 50,374 beats bit-identical (100.0000%)** |
| int32 logit mismatches | **0** |
| numpy integer path vs PyTorch QAT float | 99.908% same prediction |

The first row is exact equality of all five int32 logits on every beat of all 22
test records — not agreement within a tolerance. Two integer implementations
written from one specification in two languages, sharing no code, either agree
bit for bit or one of them is wrong.

The second row is expected to be below 100% and is not a defect: fake
quantisation accumulates in float and rounds at every node, while the integer
path accumulates in int32 and rounds once at requantisation. 0.09% of beats sit
close enough to a decision boundary for that to flip the argmax.

### A bug that only an exact test could find

An earlier run showed 12,322 / 12,323 beats identical. One beat.

The C kernel multiplies by a precomputed reciprocal `1/scale`, because VDIV
costs ~14 cycles on a Cortex-M4F against 1 for VMUL and this runs 260 times per
beat. The numpy reference divided. `x * (1/s)` and `x / s` differ in the last
bit of the float32 result, and one activation sat exactly on a rounding
boundary, quantising one code apart and propagating to a different class.

Under a tolerance-based test this would have passed silently forever. It is the
strongest argument for insisting on equality.

---

## 6. Resource usage on the STM32F446RE

### Flash and RAM — **measured** (from the linker map and `arm-none-eabi-size`)

Build: `arm-none-eabi-gcc 14.2.1`, `-mcpu=cortex-m4 -mfpu=fpv4-sp-d16
-mfloat-abi=hard`, `-Os` overall with `-O2` on the DSP/NN kernels,
`-ffp-contract=off`, `--gc-sections`.

| Section | Bytes | Note |
|---|---|---|
| `.isr_vector` | 452 | 16 core exceptions + 97 IRQ slots |
| `.text` | 8,900 | all code |
| `.rodata` | 6,472 | of which **6,188 is the model** |
| `.data` | 84 | initialised RAM, copied from flash at reset |
| `.bss` | 11,660 | zeroed RAM |
| `._user_heap_stack` | 4,096 | stack reservation; heap size is **0** |
| **Flash total** | **15,920 B** | **3.04% of 512 KB** |
| **RAM total** | **15,840 B** | **12.08% of 128 KB** |

Largest RAM objects:

| Object | Bytes | What it is |
|---|---|---|
| `g_pipeline` | 8,212 | filter state, PT state, 512-sample ring, 4-slot pending-beat queue |
| `g_out` | 1,084 | one beat result including its 256-float window |
| `s_buf_a` + `s_buf_b` | 1,024 | NN activation ping-pong |
| `beat` (bench) | 1,024 | startup benchmark fixture |
| `g_adc_buf0/1` | 128 | DMA double buffer, 32 samples each |
| NN scratch total | 1,128 | reported at runtime by `cardia_inference_scratch_bytes()` |

**Zero dynamic allocation.** Heap size is 0 in the linker script and
`firmware/src/syscalls.c` traps `_sbrk`, so any accidental heap use halts loudly
instead of appearing to work. The image links or it does not fit; there is no
third outcome and nothing to fragment at 3 a.m.

### Model — **measured**

| Metric | Value |
|---|---|
| Parameters | 5,413 |
| int8 weight bytes | 5,304 |
| Bias + requantisation constants | 884 |
| **Total model in `.rodata`** | **6,188 B** |
| **MACs per beat** | **77,024** |

Per-layer MACs:

| Layer | Output shape | MACs |
|---|---|---|
| conv1 1→8, k=11, s=4 | 64 × 8 | 5,632 |
| conv2 8→16, k=7 | 32 × 16 | 28,672 |
| conv3 16→32, k=5 | 16 × 32 | 40,960 |
| RR branch 4→16 | 16 | 64 |
| fc1 48→32 | 32 | 1,536 |
| fc2 32→5 | 5 | 160 |
| **Total** | | **77,024** |

### Inference latency — **ESTIMATED, not measured**

**The board was not available during this work, so no cycle count in this
section was measured on hardware.** The DWT cycle-counter harness is implemented
and wired into `firmware/src/main.c` (`bench_inference()` runs at startup and
prints min/mean/max cycles over UART), so the real figure is one flash away.

The estimate is derived from the actual ARM disassembly of the convolution inner
loop in the shipped binary, at `-O2`:

```
8000ecc:  ldrsb.w lr, [r2], #1        2 cycles   load weight
8000ed0:  ldrsb.w r3, [r1, #1]!       2 cycles   load activation
8000ed4:  cmp     r6, r2              1 cycle
8000ed6:  add     r3, r9              1 cycle    apply input offset
8000ed8:  mla     r0, lr, r3, r0      2 cycles   multiply-accumulate
8000edc:  bne.n   8000ecc             2 cycles   taken branch
                                     ---------
                                     10 cycles per MAC
```

| Quantity | Estimate |
|---|---|
| Cycles per MAC (portable scalar kernel, `-O2`) | ~10 |
| Cycles per beat | ~770,000 |
| **Inference time at 180 MHz** | **~4.3 ms** |
| CPU load at 75 bpm | ~0.53% |
| CPU load at 200 bpm | ~1.43% |

Sanity check on the real-time budget: at 360 Hz the sample period is 2.78 ms =
500,000 cycles, and the per-sample DSP work (two 2-section biquads, a 5-tap
derivative, a squaring, a ring-buffer integrator update) is on the order of 100
cycles — about 0.02% of the budget. Inference runs once per *beat*, not per
sample, and a beat arrives at most about three times a second. The chain has
roughly two orders of magnitude of headroom.

At `-Os` the same loop spills to the stack and runs at roughly 20 cycles per
MAC; compiling only the DSP and NN translation units at `-O2` costs 776 bytes of
flash out of 512 KB and halves the inference time. With CMSIS-NN's SIMD kernels
(SMLAD retires two 16-bit MACs per cycle) a further 3–5× is realistic, which
would put inference near 1 ms. That path is wired behind
`-DCARDIA_USE_CMSIS_NN=ON` but has **not** been benchmarked.

---

## 7. Known limitations

Stated plainly, because knowing them is worth more than hiding them.

1. **S-class sensitivity is 20.25%.** The weakest real result. Causes, in order:
   single lead; only four causal RR features where the literature uses
   record-wide statistics; 5.4k parameters; and DS2's S beats being 75%
   concentrated in one sick-sinus patient whose baseline rhythm is unlike
   anything in DS1. Likely fixes: a long-horizon RR feature (an exponentially
   weighted average over ~64 beats, still causal and MCU-friendly), the second
   MIT-BIH lead, and per-patient threshold calibration.
2. **F and Q sensitivity are 0%.** DS1 contains 414 fusion beats, 90% from one
   patient, and 8 unclassifiable beats total. There is not enough
   patient-diverse data to learn either class, and up-weighting them made
   results actively worse (see §8). The system should be described as an
   N/S/V classifier that reports F and Q for completeness.
3. **V positive predictivity is 71.60%.** The model over-calls V, mostly on
   bundle-branch-block beats. For a monitor this is the safer error direction,
   but it means roughly one in four V alarms is a wide normal beat.
4. **Inference latency is estimated, not measured.** No hardware.
5. **One beat of classification latency** (~0.8 s at rest) is inherent to using
   post-RR features. Fine for arrhythmia logging, not for beat-synchronous
   therapy.
6. **No analog front end has been connected.** The AD8232 is not yet purchased.
   Everything is validated on MIT-BIH samples, either through the simulator or
   through the UART hardware-in-the-loop path.
7. **Not a medical device.** No clinical validation, no isolation, no
   certification. See `hardware/README.md`.

---

## 8. Bugs found and fixed, with their cost

Kept because the diagnostic path is more instructive than the final numbers.

| # | Bug | Symptom | Cost if unfixed |
|---|---|---|---|
| 1 | macro-F1 computed from sensitivity and PPV returns NaN when a class is never predicted; NaNs were dropped from the average | Model selection preferred checkpoints that ignored class S entirely | The metric *rewarded* ignoring the hardest class |
| 2 | Both RBBB records (118, 124) placed in validation, leaving 86 RBBB beats in training | Model learned "wide QRS = ventricular" | V positive predictivity 13%; ~30% of normal beats called ventricular |
| 3 | Class weights computed from beat count alone | F weighted up on 397 beats, 94% from one patient | 3,049 false F predictions on DS2; F PPV 0.25%; ~6 points of accuracy |
| 4 | T-wave slope measured across the whole 56-sample search window | Both integrator humps of a wide PVC reported identical slope, ratio exactly 1.000 | 442 false detections on record 119 alone |
| 5 | numpy reference divided where the C kernel multiplies by a reciprocal | Last-bit float32 difference at a rounding boundary | 1 beat in 12,323 — invisible to any tolerance-based test |
| 6 | Four raw RR features concatenated with 32 morphology channels on a shared scale | A one-line threshold on the prematurity feature beat the whole network (76% vs 21% S sensitivity on validation) | The timing branch was effectively unused |

Bugs 1, 2 and 3 are all failures of *evaluation discipline* rather than of
modelling, and all three made the numbers look better, not worse. That is the
direction evaluation bugs usually point, which is exactly why they survive.

### A methodological disclosure

Bug 6 was first noticed while inspecting DS2 error patterns, which means the
observation was made with test-set visibility. The fix was then re-derived and
validated on the held-out DS1 validation patients before being adopted, and the
final DS2 evaluation was run once afterwards. The architecture change is
justified by the validation-set evidence alone, but the sequence is recorded
here rather than presented as if DS2 had never been looked at.

---

## 9. Comparison to published inter-patient work

Same protocol, for calibration. These systems are not directly comparable —
most use two leads and far larger models — but they bound what the protocol
allows.

| System | Leads | V sens / PPV | S sens / PPV |
|---|---|---|---|
| de Chazal et al. 2004 | 2 | 77.7% / 81.9% | 75.9% / 38.5% |
| **Cardia (this project)** | **1** | **96.2% / 71.6%** | **20.3% / 35.3%** |

V detection is competitive; S detection is not. Both facts are visible in the
same table on purpose.
