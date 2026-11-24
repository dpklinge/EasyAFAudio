# Intent — AI mix & master

Intent turns a genre and a short description into an editable mixing or mastering direction. This project now includes a working C++ audio effect alongside the original product design and browser concept.

## Native development build

Version 0.5.0 makes **Analyze & Build** a full-project scan in REAPER. Insert Intent on the master bus, choose a genre, describe the sound, and click the button. Intent plays the project from start to end, then applies editable settings and shows the measured RMS and sample peak before and after. REAPER scans the stable release folder at `releases/windows-x64/` on this machine.

Version 0.7.0 automatically enables a look-ahead true-peak limiter after **Analyze & Build**. The full scan shows integrated LUFS and true-peak comparisons using the same processing as playback. **Build direction** uses the local MiniLM classifier for text-only planning. See `native/README.md` for usage and build instructions.

Version 0.8.0 adds **Balance tracks** on the REAPER master track. It measures eligible audio tracks individually and makes conservative fader cuts. REAPER saves the fader changes with the project.

## Open the design

Open `index.html` in a browser. No installation, server, account, or network access is needed. Select a genre, enter keywords, and choose **Build a direction**. Adjust the four macros, bypass individual modules, inspect original settings, and export a JSON processing plan.

**Browser concept status:** design prototype. This HTML view does not load or process audio. Curves and values are illustrative suggestions. The exported JSON is a design contract, not a DAW preset. The project is a local folder, not a registered Codex sidebar project.

Supported preview keywords include warm, analog, bright, airy, dark, mellow, punchy, gentle, natural, wide, spacious, intimate, and focused. Genre selection and prompt edits apply when **Build a direction** is pressed. The preview reports unsupported directions and basic conflicts; it cannot understand general language or negation.

## Project files

- `index.html`, `style.css`, `app.js`: responsive plugin interface concept.
- `planner.js`: explicit keyword rules; intentionally separate from the future AI service.
- `planner.test.js`: checks bounds, genre defaults, conflicts, negation notices, and workflow behavior.
- `DESIGN.md`: product behavior, signal flow, AI architecture, and implementation milestones.
- `plan.schema.json`: portable preview plan format.

Run verification with `node planner.test.js`. JavaScript uses only browser and Node built-ins.

## Intended implementation

Windows VST3 first; macOS VST3/AU afterward. Use a C++ JUCE processor with a separate analysis and inference worker. JUCE documents VST3, AudioUnit, and standalone plugin development: https://juce.com/tutorials/tutorial_create_projucer_basic_plugin/

The real-time callback must avoid allocation, file/network operations, and UI work; see Steinberg's processing guidance: https://steinbergmedia.github.io/vst3_dev_portal/pages/FAQ/Processing.html

The first native implementation of this architecture is in `native/`. The browser concept and native effect currently have separate control mappings; the native README specifies the implemented behavior.
