# Intent / Product and engineering design

Development has started: the first native Windows effect is implemented in `native/`. Read `DEVELOPMENT.md` for actual capabilities and verified build results. The rest of this document describes the broader intended product, including features beyond this slice.

## Promise

Describe the sound you want, hear a proposed direction, and keep control of every decision. Example: “Hip-hop, warm bass, punchy drums, intimate vocal, keep the dynamics.” Genre supplies a prior; the actual recording and user constraints determine the processing. The genre is never proof that the mix needs a particular correction.

The concept uses a restrained charcoal and acid-green interface: Direction → Shape → Refine → Delivery. The main controls are Warmth, Punch, Width, and Air. Each macro exposes underlying parameters in an advanced view in the native implementation. One overall processing-amount macro can be added after user testing; it is not in this preview.

## Two workflows

**Master:** place Intent on the stereo master bus after the mix. Capture a representative passage, inspect the proposal, audition with gain matching, then measure the entire song. The plugin adjusts stereo tone, dynamics, image, and peak containment. It cannot independently change the source vocal or kick from the stereo signal. Source separation is outside the MVP.

**Mix:** place lightweight Intent Link inserts on the tracks or buses the user wants managed, then designate one Intent instance as the session coordinator. Each link has a user-assigned role, such as vocal, bass, kick, drums, or other. Roles inferred by analysis need confirmation. Only enrolled inserts can be changed. Adjust gain, EQ, and dynamics inside those inserts; never assume access to DAW faders, arbitrary plugins, tracks, or timeline content. No enrollment means an actionable “Add Link inserts” state. The final loudness/limiting stage belongs on the master, not every track.

For DAWs that isolate plugin instances into separate processes, implement a scoped local IPC coordinator. Pair with an explicit session token, not only a track name. Same-process instances can use shared services; they must not rely on a global singleton as the only communication path. Saving/reopening, duplicated DAW sessions, missing inserts, and multiple open DAWs must not cross-control sessions.

## Native user journey

1. Choose Mix or Master and identify audio inputs. Mix shows each enrolled track and missing connections.
2. Choose genre and describe the desired result. Optional reference audio is locally analyzed with explicit user selection.
3. Press Listen, then play a representative section in the DAW. Show capture progress and insufficient-signal/clipping states. Silence does not count as valid analysis. Ask for another section when confidence is poor.
4. Build a proposed plan from measured features and the prompt. Present per-module reasons and any unmet requests before activation.
5. Apply with a short ramp, edit macros or individual parameters, and lock modules that should stay fixed. A regeneration preserves locked settings.
6. Compare bypass and processed audio at matched perceived loudness. Distinguish audition compensation from render gain. Provide mono check and per-module bypass.
7. For mastering, play or offline-render the whole track to measure integrated loudness and true peak. Show measured values separately from targets. The host performs the normal audio export; the plugin does not imply control of DAW export settings.

The prototype covers direction building, macros, module toggles, settings comparison, and plan export. Capture, references, locks, actual A/B audio, mono check, metering, and enrollment are planned native features.

## Signal and decision flow

Audio input → input trim → corrective EQ → dynamic EQ → compressor → subtle saturation → stereo control → look-ahead limiter → output meter.

Start with trim, EQ, compression, stereo control, and limiting. Dynamic EQ and saturation are later modules. Macro mappings should be constrained combinations of parameters, not arbitrary sound generation. Protect low-frequency mono compatibility, cap EQ boosts, and prefer gain reduction over aggressive enhancement when headroom is limited.

Sidechain analysis → feature summary + genre + prompt → intent parser → constrained proposal → validation → user approval/apply → immutable DSP parameter snapshot.

The analysis worker measures spectrum, crest factor, stereo correlation, peaks, and loudness with confidence and coverage. Whole-song loudness needs whole-song coverage. A streaming fragment is insufficient to promise final integrated loudness. Distinguish target LUFS-I, measured LUFS-I, and true-peak ceiling. A ceiling value alone does not establish that intersample peaks are controlled.

## AI design

Use a small structured language model to translate natural language into perceptual targets, priorities, and exclusions. A planner combines these with audio features and calibrated genre distributions to propose bounded DSP parameters. A deterministic validator rejects invalid plans. DSP remains deterministic and separately testable.

Initial rule-based intent mapping is a useful baseline, but must be described as such. This project's mapping is only that baseline. A trained model, inference runtime, weights, training data, and evaluation results are not included.

The parser returns schema-versioned intent: genre, attributes, constraints, ambiguity flags, confidence, and explanations. Support negation and conflicting requests in the native parser. Resolve ambiguous prompts by presenting the interpretation and editable controls; do not silently invent precise settings. Artist references can describe broad sonic traits without promising an exact match.

Training and calibration require licensed or otherwise authorized audio and expert-reviewed mix decisions. Evaluate on held-out artists and tracks, with genre balance and separation from training audio. Begin with existing suitable models after a license/runtime evaluation rather than assuming that a custom model will be available.

Local-first is the design default. A future cloud option must separately disclose exactly what leaves the machine and obtain consent. Raw tracks never upload merely because a user types a prompt. Presets retain the resolved parameters so playback and offline rendering do not need inference, connectivity, or the original model version.

## Runtime architecture

- C++ JUCE processor/editor, CMake build, Windows VST3 first; AU/VST3 on macOS next. Verify licenses and distribution/signing requirements before shipping.
- Audio thread uses preallocated buffers and DSP state only. No model inference, network, file access, UI calls, locks, allocation, or deallocation in the callback.
- Analysis data crosses a bounded, preallocated single-producer/single-consumer queue. If the worker is slow, drop analysis frames and mark incomplete coverage; never block audio.
- UI/planner worker validates a new parameter block and publishes via a preallocated handoff. Reclamation occurs away from the callback. Use appropriate lock-free primitives for supported platforms; do not assume all atomics or shared-pointer operations are lock-free.
- Smooth gain and coefficients. Crossfade topology changes. Bypass and A/B paths must compensate for processing delay. Allocate delay storage during preparation, not during playback.
- Limiter look-ahead and oversampling determine reported latency. Notify the host when latency changes outside the real-time callback; keep it fixed during an audition/render. Provide a low-latency tracking mode with explicit feature changes.
- Persist parameters, intent, model version, role, link identity, locks, and plan version in host state. Restore audio processing without a model call. Preserve host automation semantics; applying a proposal should be a grouped undoable operation where the host supports it.
- Failures retain the last valid processing state. NaN/Inf and corrupt presets cannot reach output. Analyze cancellation, transport restarts, sample-rate changes, and offline renders explicitly.

## Guardrails and delivery decisions

Proposed preview targets span -24 to -6 LUFS-I and -3 to -0.1 dBTP. They are product design ranges, not distribution rules. Defaults are creative starting points. If reaching a target would require excessive limiting, report the tradeoff and allow a lower loudness target. Never imply “louder” means “better.”

Width macros must report mono compatibility and avoid unintended sub-bass widening. Punch must balance attack and release with the program material; a ratio alone does not define punch. Warmth must avoid simply adding uncontrolled low-frequency level. Explain frequency-specific decisions using measured evidence in the native version.

## Milestones and release gates

1. **Concept: completed.** Local interactive design, rule-based planning, JSON export, and mapping tests.
2. **Native vertical slice: in progress.** Windows VST3 and standalone compile and process audio with saved state. Native tests and pluginval level 5 pass. REAPER 7.57 scan/load, native controls, state recall, and offline song-excerpt renders at 48/96 kHz pass. A second DAW, longer real-time sessions, and sustained callback profiling remain before this milestone is complete.
3. **Mastering baseline.** Full-track measurement, true-peak limiting, gain-matched A/B, reference features, and rule-based suggestions. Gate: compare meters against trusted test signals/tools; test intersample peaks, mono, clipping, silence, very short tracks, and dynamic program material.
4. **AI intent.** Structured inference worker and plan validation. Gate: negation, conflicting words, unknown genres, invalid output, cancellation, absent model, and identical saved-state playback across model updates. Blind, loudness-matched listening against expert and rule-based baselines is required before quality claims.
5. **Linked mixing.** Track enrollment, roles, coordinator, IPC, and constraints. Gate: multiple sessions, isolated host processes, stale links, session restore, and no unintended track control.
6. **Release candidate.** Performance profiling and native accessibility, signing, installers, licensing review, and macOS builds. Define supported DAWs/versions from tested evidence. Do not claim universal host support.

## Technical references

- JUCE basic audio plugin tutorial: https://juce.com/tutorials/tutorial_create_projucer_basic_plugin/
- Steinberg processing and real-time guidance: https://steinbergmedia.github.io/vst3_dev_portal/pages/FAQ/Processing.html
- Steinberg IAudioProcessor and latency reporting: https://steinbergmedia.github.io/vst3_doc/vstinterfaces/classSteinberg_1_1Vst_1_1IAudioProcessor.html

The architecture above is a proposed design informed by these primary sources, not a claim that the implementation exists.
