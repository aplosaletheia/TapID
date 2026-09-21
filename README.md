# TapID

> **Tap an object. Hear its fingerprint. Identify it.**

TapID identifies a **specific physical object** from the sound it produces when tapped.

It uses a microphone and signal processing to extract the object's acoustic fingerprint and compare it against previously recorded references.

**No camera. No barcode. No machine learning.**

```text
             ┌──────────────┐
             │ Physical     │
             │   Object     │
             └──────┬───────┘
                    │
              standardized tap
                    │
                    ▼
             ┌──────────────┐
             │ Microphone   │
             └──────┬───────┘
                    │
                    ▼
             ┌──────────────┐
             │ Tap Isolation│
             └──────┬───────┘
                    │
                    ▼
             ┌──────────────┐
             │ DFT / Spectrum│
             └──────┬───────┘
                    │
                    ▼
             ┌──────────────┐
             │ Acoustic     │
             │ Fingerprint  │
             └──────┬───────┘
                    │
                    ▼
             ┌──────────────┐
             │ Reference    │
             │ Matching     │
             └──────┬───────┘
                    │
                    ▼
             ┌──────────────┐
             │ Object ID    │
             └──────────────┘
```

---

## The idea

Different objects respond differently to the same mechanical impact.

Their **material, geometry, thickness, construction, and damping** determine which resonant frequencies are excited and how strongly they ring.

TapID treats that response as an **acoustic fingerprint**.

Instead of asking:

> "What material is this?"

TapID asks:

> **"Which known object produced this acoustic fingerprint?"**

For example:

```text
        ┌─────────────┐
        │ Unknown tap │
        └──────┬──────┘
               │
          acoustic
         fingerprint
               │
       ┌───────┼────────┐
       ▼       ▼        ▼
     Mug     Bottle   Stapler
       │       │        │
       └───────┴────────┘
               │
               ▼
        strongest match
               │
               ▼
           "Bottle"
```

---

## Why object identification instead of material classification?

The original idea was to identify the material of an object from its sound.

That turns out to be a much harder physical problem than it first appears.

A resonant frequency is not determined by material alone. It also depends on factors such as:

* Geometry
* Size
* Thickness
* Shape
* Boundary conditions
* Impact location
* Excitation
* Damping

The same material can produce very different acoustic responses when its geometry changes.

TapID therefore focuses on a more measurable problem:

> **Identify a known object from its acoustic response under controlled excitation.**

This makes the system useful for small sets of known objects — for example:

```text
Which of these objects was tapped?

[ Mug ] [ Bottle ] [ Stapler ] [ Metal Box ] [ Glass ]
```

---

## How it works

### 1. Capture

A microphone records the acoustic response using `miniaudio`.

A short countdown gives the operator time to position the microphone and object before recording begins.

### 2. Detect the tap

TapID searches the recording for the impact onset using an amplitude threshold.

The initial impact contains a large broadband transient, so the system isolates the ringing response that follows it.

### 3. Prepare the signal

The extracted window is:

* DC-offset corrected
* Fixed in length
* Hann-windowed

The Hann window reduces spectral leakage caused by cutting a non-periodic signal at the boundaries.

### 4. Transform into the frequency domain

TapID performs a DFT on the isolated response.

This converts the time-domain recording into a frequency spectrum where resonant components become visible.

### 5. Extract resonant peaks

Local spectral maxima are detected above a threshold and above a minimum frequency floor.

The strongest relevant peaks form the acoustic signature of that tap.

Conceptually:

```text
Amplitude
   │
   │             ╭╮
   │       ╭╮    ││
   │       ││    ││          ╭╮
   │   ╭╮  ││    ││    ╭╮    ││
   │   ││  ││    ││    ││    ││
   └───┴───┴┴────┴┴────┴┴────┴┴────── Frequency
       f1  f2    f3    f4    f5
```

The frequencies themselves become the primary fingerprint.

### 6. Build an object reference

In **add mode**, multiple taps can be recorded for the same object.

TapID stores the raw peak history rather than immediately collapsing everything into one average.

The peaks from different taps are then:

1. Pooled
2. Grouped by frequency proximity
3. Evaluated by how frequently they occur
4. Converted into an aggregate reference signature

A resonance that appears consistently across many taps becomes part of the object's trusted fingerprint.

A one-off noisy peak does not.

### 7. Identify the object

In **identify mode**, the live fingerprint is compared against every stored object.

Peaks are aligned optimally within a configurable frequency tolerance, with each peak allowed to participate in at most one match.

The resulting score accounts for how closely the observed resonances agree with the reference.

### 8. Reject ambiguous matches

TapID does not blindly return the closest object.

Two checks are applied:

* **Absolute score threshold** — is the match good enough?
* **Best-vs-second-best margin** — is the winning match sufficiently separated from the alternatives?

If the evidence is insufficient, TapID returns:

```text
UNKNOWN
```

rather than forcing an identification.

---

## Why signal processing?

TapID deliberately uses deterministic signal processing instead of a trained classifier.

This makes the identification process directly inspectable.

There is no hidden representation inside a neural network. A match can be explained in terms of measured acoustic features:

```text
Unknown object

Resonances:
  1.84 kHz
  3.21 kHz
  5.47 kHz
  7.02 kHz

Reference:
  Bottle

Matched:
  1.84 kHz  ✓
  3.20 kHz  ✓
  5.49 kHz  ✓
  7.01 kHz  ✓
```

Adding a new object also does not require retraining a model.

Record its acoustic response, build its reference fingerprint, and it becomes another candidate.

---

## Controlled excitation

The tapping implement is deliberately kept consistent between reference recordings and identification.

For example:

> **The same rigid object is used to perform the tap at a consistent location.**

The tapper is the excitation source, not the signal being identified.

Changing the excitation can change which resonant modes are excited and therefore change the measured fingerprint.

Keeping the excitation consistent makes the comparison between reference and live recordings much more meaningful.

---

## Validation

TapID is designed to be evaluated experimentally rather than assumed to work.

Example validation format: (tapped using a 10rs coin, 7-10 samples were taken for the database for each object)

| Object          | Taps | Correct | Unknown | Incorrect |
| ----------------| ---: | ------: | ------: | --------: |
| metal bottle    |   10 |      10 |       - |         - |
| glass container |   10 |      10 |       0 |         0 |
| plastic bottle  |   10 |      10 |       — |         - |

The goal is not to demonstrate that TapID can always identify an object.

The goal is to measure how reliably its acoustic fingerprints distinguish objects under controlled conditions.

---

## Limitations

TapID currently identifies objects from a **known reference set**.

It is not intended to identify arbitrary objects it has never encountered.

Performance can also be affected by:

* Different tapping locations
* Different excitation force
* Microphone position
* Background noise
* Object orientation
* Objects with very similar acoustic responses
* Environmental reflections
* Insufficient reference recordings

These constraints are part of the measurement problem rather than something the current system attempts to hide.

---

## Technical implementation

TapID is written primarily in **C** with minimal external dependencies.

### Core components

* **C** — application and signal-processing pipeline
* **miniaudio** — microphone/audio capture
* **Custom DFT implementation** — frequency-domain analysis
* **Custom peak extraction** — resonance detection
* **Binary reference database** — persistent object signatures
* **Deterministic peak matching** — object identification

Currently the application is Windows-specific because of platform APIs used for input and filesystem handling.

---

## Build

### Requirements

* Windows
* GCC / MinGW
* Microphone

From the repository root:

```bat
cd src
run.bat
```

For faster rebuilds, the miniaudio implementation can be compiled separately:

```bash
gcc miniaudio_impl.c -o miniaudio_impl.o
```

The build script can then link against `miniaudio_impl.o` instead of recompiling the miniaudio implementation every time.

---

## Usage

When the program starts, TapID gives a short countdown and begins recording.

Press:

```text
s + Enter
```

to stop recording.

The program then asks for the operating mode.

### Add an object

```text
a
```

Enter the object's name.

The tap is added to that object's reference history and the aggregate fingerprint is rebuilt.

Record multiple taps per object before attempting identification.

### Identify an object

```text
i
```

TapID compares the live fingerprint against the stored reference database.

Possible outcomes:

```text
RESULT: bottle
```

or:

```text
RESULT: unknown
```

---

## Project status

Core identification pipeline is implemented.

* [x] Microphone capture
* [x] Tap onset detection
* [x] Tap window extraction
* [x] DC removal
* [x] Hann windowing
* [x] DFT-based spectral analysis
* [x] Local spectral peak extraction
* [x] Frequency-floor filtering
* [x] Per-object raw sample history
* [x] Frequency-proximity clustering
* [x] Support-weighted aggregate signatures
* [x] Optimal peak alignment
* [x] Absolute match rejection
* [x] Best-vs-second-best rejection
* [x] CLI add mode
* [x] CLI identify mode

---

## Project structure

```text
TapID/
├── src/
│   ├── ...
│   └── run.bat
├── data/
├── README.md
└── LICENSE
```

---

## License

MIT License — see [LICENSE](LICENSE).
