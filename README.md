# TapID

Identify an object by the sound it makes when tapped.

## What it does

TapID listens to a short tap on an object and identifies what it is purely from the sound's resonant
frequencies. No camera, no touch sensor, no machine learning model. Just a
microphone and signal processing.

## How it works

Tapping an object excites its natural resonant modes — frequencies
determined by the material's stiffness, density, and internal damping.
Different objects ring at characteristically different frequencies, the
same way a coin's "ring" is used to judge whether it's genuine.

1. Capture a short audio clip of the tap
2. Run a windowed FFT to get its frequency spectrum
3. Extract the dominant resonant peak(s)
4. Compare against reference profiles built from labeled tap recordings
5. Report the closest-matching object

## Why no machine learning

The classification is nearest-match against a small set of reference peak
frequencies, not a trained model. That means:
- No training data pipeline, no GPU, nothing to overfit
- Every classification is explainable — you can see exactly which
  frequency peak drove the result
- Adding a new object is just recording a handful of reference taps,
  not retraining anything

## Methodology note

Reference recordings and live identification both use the same rigid
tapping implement (e.g. 10rs coin), tapped at a consistent location on
each object. The tapper is the excitation source, not the signal of
interest — varying it changes which frequencies get excited and would
contaminate the comparison. Classification is based on *where* the
resonant peaks sit, not their amplitude, which keeps results reasonably
robust to natural variation in tap force.

## Status

Early build — hackathon in progress.

- [Y] Microphone capture (miniaudio)
- [Y] Windowed FFT / spectral analysis
- [ ] Peak extraction
- [ ] Reference profile recorder
- [ ] Nearest-peak classifier
- [ ] CLI

## Build & run

*Planned interface — not yet implemented.*

```
gcc -O2 -o tapid src/*.c -lm
./tapid record wood        # build a reference profile for a material
./tapid identify           # classify a live tap
```

## Tech

Written in C with minimal external dependencies. Audio capture via
[miniaudio](https://miniaud.io) (single-header, no install required).

## License

MIT — see [LICENSE](LICENSE).
