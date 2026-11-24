cmake_minimum_required(VERSION 3.22)
if(POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
endif()
project(Intent VERSION 0.8.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

set(intent_ebur128_root "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libebur128")
add_library(intent_ebur128 STATIC "${intent_ebur128_root}/ebur128/ebur128.c")
set_target_properties(intent_ebur128 PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(intent_ebur128 PUBLIC "${intent_ebur128_root}/ebur128"
    PRIVATE "${intent_ebur128_root}/ebur128/queue")
if(MSVC)
    target_compile_definitions(intent_ebur128 PRIVATE _USE_MATH_DEFINES)
endif()

# Set JUCE_SOURCE_DIR for an offline/local dependency, otherwise fetch a pinned revision.
set(JUCE_SOURCE_DIR "" CACHE PATH "Existing JUCE 8.0.6 source directory")
if(JUCE_SOURCE_DIR)
    add_subdirectory("${JUCE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")
    set(intent_juce_root "${JUCE_SOURCE_DIR}")
else()
    include(FetchContent)
    FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 51a8a6d7aeae7326956d747737ccf1575e61e209 GIT_SHALLOW FALSE)
    FetchContent_MakeAvailable(JUCE)
    set(intent_juce_root "${juce_SOURCE_DIR}")
endif()

set(intent_formats VST3 Standalone)
if(APPLE)
    list(APPEND intent_formats AU)
endif()
juce_add_plugin(Intent
    COMPANY_NAME "Intent Audio"
    BUNDLE_ID "audio.intent.plugin"
    PLUGIN_MANUFACTURER_CODE Itau
    PLUGIN_CODE Intt
    FORMATS ${intent_formats}
    PRODUCT_NAME "Intent"
    DESCRIPTION "Genre-guided tone and dynamics prototype"
    IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE VST3_CATEGORIES Fx Mastering)
target_sources(Intent PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/LanguagePlanner.cpp
    Source/ReaperTransport.cpp Source/TrackBalance.cpp Source/FullScan.cpp)
target_include_directories(Intent PUBLIC Source)
target_include_directories(Intent PRIVATE
    "${intent_juce_root}/modules/juce_audio_processors/format_types/VST3_SDK"
    "${intent_juce_root}/examples/Plugins/extern")
target_compile_definitions(Intent PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(Intent PUBLIC juce::juce_audio_utils
    PRIVATE intent_ebur128 juce::juce_recommended_config_flags)

set(INTENT_LANGUAGE_ASSETS "${CMAKE_CURRENT_BINARY_DIR}/intent-language" CACHE PATH
    "Full-precision MiniLM and ONNX Runtime assets prepared by fetch-language-assets.py")
if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8 AND NOT CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64"
        AND NOT CMAKE_GENERATOR_PLATFORM MATCHES "ARM64")
    if(NOT EXISTS "${INTENT_LANGUAGE_ASSETS}/model/model.onnx"
            OR NOT EXISTS "${INTENT_LANGUAGE_ASSETS}/include/onnxruntime_cxx_api.h")
        message(FATAL_ERROR "Prepare the language assets with fetch-language-assets.py, or use build.ps1.")
    endif()
    target_compile_definitions(Intent PUBLIC INTENT_USE_MINILM=1)
    target_include_directories(Intent PRIVATE "${INTENT_LANGUAGE_ASSETS}/include")
    add_custom_command(TARGET Intent_VST3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${INTENT_LANGUAGE_ASSETS}/model"
            "$<TARGET_FILE_DIR:Intent_VST3>/../Resources/IntentLanguage"
        VERBATIM)
    function(intent_copy_language_assets target)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${INTENT_LANGUAGE_ASSETS}/model"
                "$<TARGET_FILE_DIR:${target}>/IntentLanguage"
            VERBATIM)
    endfunction()
    intent_copy_language_assets(Intent_Standalone)
else()
    target_compile_definitions(Intent PUBLIC INTENT_USE_MINILM=0)
endif()

enable_testing()
add_executable(IntentDspTests Tests/DspTests.cpp)
target_include_directories(IntentDspTests PRIVATE Source)
add_test(NAME dsp COMMAND IntentDspTests)
juce_add_console_app(IntentProcessorTests PRODUCT_NAME "Intent Processor Tests")
target_sources(IntentProcessorTests PRIVATE Tests/ProcessorTests.cpp)
target_link_libraries(IntentProcessorTests PRIVATE Intent intent_ebur128)
if(COMMAND intent_copy_language_assets)
    intent_copy_language_assets(IntentProcessorTests)
endif()
add_test(NAME processor COMMAND IntentProcessorTests)
