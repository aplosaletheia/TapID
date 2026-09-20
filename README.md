# TapID

Identify a specific object by the sound it makes when tapped.

## What it does

TapID listens to a tap on an object and tells you *which* object it was —
your water bottle, your stapler, a particular mug — purely from the
sound's resonant frequencies. No camera, no barcode, no machine learning
model. Just a microphone and signal processing.

## Scope note: object identification, not material classification

This started as a "what material is this?" classifier, but that framing
doesn't hold up physically: a tap's resonant frequency is set jointly by
material *and* geometry (size, shape, thickness) — the same wood ringing
in a thin ruler and a thick block will produce different frequencies.
There's no clean way to isolate "material" from a single frequency
reading without controlling for shape.

What a frequency signature *does* reliably capture is the combination of
material and geometry that makes up one specific object — so the project
is scoped to that instead: identifying a known object from a small
personal set (e.g. "which of these five things did I just tap?"), the
same principle behind judging a coin's authenticity by its ring, or
industrial tap-testing of a known part for internal damage.

## How it works

1. **Capture** a few seconds of audio via the mic (miniaudio), starting
   after a countdown, stopped on keypress.
2. **Isolate the tap**: detect onset by amplitude threshold, skip the
   initial impact transient (broadband noise, not tonal), and take a
   fixed-length window over the ringing decay that follows.
3. **Window and transform**: DC removal, a Hann taper (to avoid spectral
   leakage from truncating a non-periodic signal), then a DFT to get the
   frequency spectrum.
4. **Extract peaks**: find local maxima above a threshold, above a
   minimum-frequency cutoff (excludes handling noise/rumble/mains hum),
   keeping the strongest few as that tap's signature.
5. **Add mode** (`a`): append the new tap's peaks to that object's raw
   sample history, then rebuild its aggregate signature by pooling peaks
   across *all* recorded taps of that object and clustering them by
   frequency proximity — a cluster only becomes a trusted "mode" once it
   shows up in a large-enough fraction of the recorded taps, which
   filters out one-off noise instead of averaging it in.
6. **Identify mode** (`i`): match the live tap's peaks against every
   stored object using optimal peak alignment (each live peak paired
   with at most one stored peak, within a frequency tolerance), then
   apply two rejection checks before reporting a result: an absolute
   score cutoff, and a margin check against the second-best match, so a
   genuinely ambiguous tap is reported as "unknown" rather than a
   confident-looking wrong guess.

## Why no machine learning

Classification is nearest-match against clustered reference peaks, not a
trained model:
- No training pipeline, no GPU, nothing to overfit on a handful of taps
- Every result is explainable — you can point to exactly which stored
  frequencies matched and by how much
- Adding a new object is a few taps and a name, not retraining anything

## Methodology note

Reference recordings and live identification use the same rigid tapping
implement (e.g. a pen cap), at a consistent location on each object. The
tapper is the excitation source, not the signal of interest — varying it
changes which frequencies get excited and would contaminate every
comparison downstream, from clustering to matching. This matters more
now than it would for a coarser material classifier, since object
identification is leaning on the full, specific frequency signature of
one exact item.

## Status

Core pipeline implemented — hackathon in progress.

- [x] Microphone capture (miniaudio)
- [x] Onset detection + tap window extraction (Hann-windowed, DC-removed)
- [x] DFT-based spectral analysis
- [x] Peak extraction (local maxima, thresholded, frequency-floor filtered)
- [x] Reference database: per-object raw sample history + clustered,
      support-weighted aggregate signatures
- [x] Matching: optimal peak alignment with confidence rejection
- [x] CLI (add / identify)

## Build & run

cd ".\src\"
.\run.bat                     (uses gcc compiler)

recommened to not compile miniaudio_impl.c everytime, instead compile once using gcc miniaudio_impl.c -o miniaudio_impl.o
and replace miniaudio_impl.c by miniaudio_impl.o in run.bat.

On launch: a 3-second countdown, then it records until you press `s` and
Enter. It then asks:

- `a` — prompts for an object name, saves this tap's peaks to that
  object's history in `data/`, and rebuilds its aggregate signature
- `i` — matches the tap against everything in `data/objects.bin` and
  prints a result (or `unknown` if nothing clears the confidence checks)

Record several taps per object with `a` before relying on `i` against
it — a single tap has no history to cluster against, so its aggregate
signature will be thin or empty.

## Tech

Written in C with minimal external dependencies. Audio capture
via [miniaudio](https://miniaud.io) (single-header, no install
required). Currently Windows-specific (`Windows.h`, `CreateDirectoryA`,
`Sleep`) — portable-capture and portable-directory-creation are on the
list if cross-platform support becomes worth the time.

## License

MIT — see [LICENSE](LICENSE).