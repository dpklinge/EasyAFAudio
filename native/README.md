# Intent native / 0.8.0 development slice

This is the first C++ implementation of the plugin design. It processes audio; the browser concept remains a separate design tool.

## Implemented

- Windows x64 VST3 and standalone application targets.
- Mono/stereo audio with neutral startup settings and a fixed look-ahead delay of about 5 ms, reported to the host for compensation.
- Warmth: a one-pole low-band gain adjustment around 180 Hz, up to ±2 dB.
- Air: a one-pole high-band gain adjustment around 6 kHz, up to ±2.5 dB. This is broad tone shaping, not a precision parametric equalizer.
- Punch: stereo-linked feed-forward compression; ratio varies from 1:1 to 3:1 above a fixed -18 dBFS sample detector threshold. Attack is 12 ms and release 120 ms. No automatic makeup gain.
- Width: upper-side-band scaling from 80–120%, with existing low-frequency side content preserved. Mono remains mono.
- Output trim, a stereo-linked true-peak limiter with oversampled detection, and delay-aligned global bypass.
- Eight host-automatable parameters, saved parameter state, genre/prompt metadata, native editor, and block sample-peak readouts.
- Six genre starting points, exact keyword rules, and optional semantic interpretation by the bundled full-precision MiniLM model on Windows x64.
- Analyze & Build runs a full-project pass in REAPER and reports before/after integrated LUFS, true peak, RMS, and sample peak.
- Fixed-memory processing with smoothed targets; no inference, file access, allocation, or locks in the DSP callback.

**Planned:** balancing tracks with effects and complex routing, one combined mix-and-master pass, per-track EQ and dynamics, reference analysis, and DAW export control.

## Build on a fully installed Windows toolchain

Install Visual Studio 2022 with **Desktop development with C++** and a Windows SDK, CMake 3.22+, Python 3.11+, and Git. From PowerShell:

```powershell
./build.ps1
```

The first build downloads the pinned JUCE 8.0.6 source and language assets. With an existing JUCE checkout and a prepared language asset folder:

```powershell
./build.ps1 -JuceSourceDir C:/dev/JUCE -LanguageAssetsDir C:/dev/intent-language
```

`build.ps1` publishes the Windows Release build to `../releases/windows-x64/` after the tests pass. Keep that folder in REAPER's VST scan paths. Each publish verifies the replacement bundle and backs up the preceding release outside the scan folder. If REAPER is holding the plugin file open, quit REAPER before publishing again.

Manual commands:

```powershell
python ./fetch-language-assets.py ./build/intent-language
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure
```

Once the manual build and tests pass, publish the results from this directory:

```powershell
./publish-windows.ps1 -ArtifactsDir ./build/Intent_artefacts/Release
```

On macOS, the CMake project also requests AU alongside VST3/standalone. Those targets have not been tested here. JUCE 8 does not support the MinGW toolchain; use MSVC on Windows.

## Try the development build

The current compiled artifacts are in `../releases/windows-x64/`. Point REAPER's scanner at that directory. On this machine that path is already configured. Restart REAPER to load a freshly published build if it has the preceding version loaded.

The Windows binaries dynamically link Microsoft's Visual C++ x64 runtime (`MSVCP140.dll`, `VCRUNTIME140.dll`, and `VCRUNTIME140_1.dll`). A compatible runtime must be present on the playback machine. It is present on the build/validation machine; the package does not install it.

Insert Intent on a stereo master bus or an individual track, choose a genre, type keywords, and press **Build direction**. Audio must already be playing through the insert. The standalone application uses selected live audio inputs; it is not a WAV file player. The standalone effect may mute input initially to avoid feedback; configure its audio settings before enabling live input.

Startup is neutral. Choose a genre and click **Reset to genre defaults** to restore all six knobs and both switches. **Build direction** applies keywords to Warmth, Punch, Width, and Air while preserving the output and switch settings. Reset retains the prompt so you can reapply it whenever you want. Peak meters show the most recent audio block.

## Balance tracks

On the REAPER master track, click **Balance tracks** to measure individual audio tracks and lower the loudest ones. This first pass reads each eligible track before its FX chain and compares integrated LUFS with the median of the eligible tracks. It only lowers faders. The calculated targets are limited to 6 dB below unity, and faders already below target keep their current setting.

The pass skips muted tracks, folders, and tracks with FX, envelopes, or explicit sends or receives. At least two tracks with audible media are required. Canceling or editing the project during measurement discards the balance proposal. The fader changes use one REAPER undo step and save as ordinary REAPER fader settings when you save the project. Use **Analyze & Build** afterwards to master the new mix.

## Analyze & Build

In REAPER, insert Intent on the master bus, choose a genre, enter a description, and click **Analyze & Build**. Intent starts playback at project zero, ignores the repeat range for the scan, and captures up to REAPER's project end. Keep the project open and let the scan finish. Intent stops the scan playback and restores the previous edit cursor and repeat setting. The pass runs at playback speed. Full-project analysis uses REAPER's integrated VST3 host, so the standalone and other hosts can still use the text-based **Build direction**.

The planner uses the genre and text as a starting point, then applies bounded adjustments based on low and high energy, crest factor, and stereo correlation. A background worker uses temporary local storage for the full scan. Intent previews the actual DSP on the captured input. It uses integrated LUFS to level-match, falls back to RMS if the signal is too short or quiet for a gated reading, and uses true peak to budget headroom. The preview measures the same limiter used during playback, with the look-ahead delay removed from the analysis. Later FX and faders can change the final output.

The adjusted tone, dynamics, stereo width, and output trim remain editable. **Analyze & Build** enables the limiter automatically and keeps the current ceiling. Toggle **Bypass** to compare with the input. The plugin receives the audio presented at the insert point, so a stereo master scan cannot independently rebalance its sources.

The scan timestamps the first input sample above 0 dBFS and the first repeated flat peak that suggests existing hard clipping. Flat peaks can also come from intentionally shaped sounds, and summing other tracks can mask the shape. **True-peak limiter** uses gain reduction ahead of peaks at Intent's output. The meter shows when it is reducing gain. To address clipping already present at the input, reduce gain before the clipping stage or replace the clipped source with a clean take.

| Genre | Warmth | Punch | Width | Air | Output (dB) | Ceiling (dBTP) |
|---|---:|---:|---:|---:|---:|---:|
| Electronic | 50% | 65% | 65% | 60% | −1 | −1 |
| Hip-hop | 65% | 70% | 45% | 45% | −1.5 | −1 |
| Pop | 45% | 60% | 60% | 65% | −1 | −1 |
| Rock | 55% | 65% | 45% | 50% | −0.5 | −1 |
| Jazz | 55% | 30% | 45% | 45% | 0 | −1.5 |
| Ambient | 60% | 20% | 80% | 65% | −1 | −1.5 |

Every genre starts with the limiter and bypass off. Projects saved with the previous peak guard enabled load with the true-peak limiter enabled.

## How Build direction interprets text

Try “Give the sub bass more heft and stretch the stereo field.” Intent splits the prompt into phrases and uses exact rules where they apply. MiniLM compares the other phrases with examples for raising or lowering Warmth, Punch, Width, and Air. A confident match applies a bounded offset to the genre default. Inferred changes appear by name below the controls. Opposing inferred directions for the same knob are skipped, and recognized negation restores the genre sound settings.

The model and tokenizer are bundled with the Windows plugin. Inference runs on a worker after you click **Build direction**. The first semantic request also loads the model and computes the reference embeddings. The audio thread uses the resolved knob values. Project reload and offline rendering use the saved knob values directly.

The model is English and trained for general sentence similarity. Unsupported or ambiguous phrases can be skipped or misunderstood. Applying a text direction preserves the output trim, ceiling, limiter, and bypass. Mac and other non-Windows builds use the keyword planner.

## Tests and boundaries

`IntentDspTests` checks neutral passthrough across five sample rates, stereo-linked compression, bypass convergence, guard sample bounds, invalid-input recovery, mono operation, and block partitioning. It also checks keyword matching, conflict notices, and negation fallback.

`IntentProcessorTests` checks host-state round trip, prompt/genre restoration, rejection of malformed and out-of-range state, processing through the JUCE processor, meter updates, and editor construction. Pass an optional PNG path to render the editor snapshot. External plugin validation and real DAW testing are reported separately; neither unit suite establishes mastering quality.

Use a single enrolled instance per track for this slice. There is no session coordinator or automatic cross-track balancing yet.

## Dependencies

JUCE is pinned to commit `51a8a6d7aeae7326956d747737ccf1575e61e209` (8.0.6), fetched during configuration or supplied locally. Dependency source is not included in this project archive. See `THIRD_PARTY.md` for licensing and engineering references.
