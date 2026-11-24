# Third-party components and references

## MiniLM and ONNX Runtime

- Full-precision `sentence-transformers/all-MiniLM-L6-v2` ONNX model and vocabulary, Apache 2.0, pinned to revision `1110a243fdf4706b3f48f1d95db1a4f5529b4d41`. https://huggingface.co/sentence-transformers/all-MiniLM-L6-v2/tree/1110a243fdf4706b3f48f1d95db1a4f5529b4d41
- Microsoft ONNX Runtime CPU x64, version 1.23.2, MIT. https://www.nuget.org/packages/Microsoft.ML.OnnxRuntime/1.23.2

The asset fetcher verifies SHA-256 hashes and bundles the applicable license and third-party notice files alongside the model.

## libebur128 1.2.6

The native full-project analyzer statically links libebur128 for integrated loudness and true-peak measurement. The source is vendored from upstream commit `67b33abe1558160ed76ada1322329b0e9e058b02`. See the [pinned upstream source](https://github.com/jiixyj/libebur128/tree/67b33abe1558160ed76ada1322329b0e9e058b02).

The library is MIT licensed. Its bundled queue compatibility header has a three-clause BSD notice. Both notices are included in the Windows release bundle.

## JUCE 8.0.6

Source: https://github.com/juce-framework/JUCE/tree/8.0.6

License: JUCE is offered under AGPLv3 or a commercial JUCE license. Select and comply with the applicable license before distributing this plugin publicly. This development project does not grant a JUCE commercial license or select a product distribution license on the user's behalf.

JUCE license text: https://github.com/juce-framework/JUCE/blob/8.0.6/LICENSE.md

JUCE incorporates additional third-party components. See the pinned repository's license and module notices for their terms; the native artifact should not be treated as cleared for public redistribution solely because it compiles.

## VST3

The VST3 support used here comes through the pinned JUCE dependency. Preserve its bundled SDK licensing and notices when preparing a distribution. The development code identifies the effect as a VST3 audio effect; it does not imply Steinberg endorsement.

## Engineering references

- CMake API: https://github.com/juce-framework/JUCE/blob/8.0.6/docs/CMake%20API.md
- Parameter state: https://juce.com/tutorials/tutorial_audio_processor_value_tree_state/
- Real-time processing and host latency: https://steinbergmedia.github.io/vst3_dev_portal/pages/FAQ/Processing.html
- Plugin validator: https://github.com/Tracktion/pluginval

## Build tools used for local verification

CMake, Ninja, Microsoft's MSVC 14.36 packages, the installed Windows 10 SDK, and pluginval are development tools, not bundled project source. Missing compiler headers/libraries were extracted from SHA256-verified packages referenced by Microsoft's official Visual Studio channel manifest into this chat's `work/` folder. No Visual Studio installation or system configuration was changed.

LLVM-MinGW was used only for an independent DSP test run. It is not the toolchain used for the JUCE plugin, which requires MSVC on Windows.
