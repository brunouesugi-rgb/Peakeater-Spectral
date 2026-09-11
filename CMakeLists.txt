cmake_minimum_required(VERSION 3.15)
project(${CMAKE_PROJECT_NAME} VERSION ${CMAKE_PROJECT_VERSION})

include(FetchContent)
include(CTest)

# Export compile_commands.json
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

find_package(JUCE CONFIG REQUIRED)

FetchContent_Declare(
    clap-juce-extensions
    GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
    GIT_TAG b6cdda9f1fad06ba14e74e036f737d2ed11db2e7
)
FetchContent_MakeAvailable(clap-juce-extensions)

set(PEAKEATER_SHARED_SOURCES

    # DSP
    source/processor/Clipper.cpp
    source/processor/SpectralMaximizer.cpp
    source/processor/DynamicsMeter.cpp
    source/processor/OscilloscopeBuffer.cpp
    source/processor/LevelMeter.cpp

    # GUI
    source/editor/clipmeter/ClipMeter.cpp
    source/editor/editableparameter/EditableParameterComponentLookAndFeel.cpp
    source/editor/editableparameter/EditableLabelAttachment.cpp
    source/editor/editableparameter/EditableParameterComponent.cpp
    source/editor/dial/DialLookAndFeel.cpp
    source/editor/dial/Dial.cpp
    source/editor/dial/gaindial/GainDialLookAndFeel.cpp
    source/editor/dial/gaindial/GainDial.cpp
    source/editor/dial/ceilingdial/CeilingDial.cpp
    source/editor/dial/ceilingdial/CeilingDialLookAndFeel.cpp
    source/editor/linkinout/LinkInOut.cpp
    source/editor/bypass/BypassButton.cpp
    source/editor/scaling/ScalingSwitch.cpp
    source/editor/levelmeter/LevelMeterComponent.cpp
    source/editor/analyser/peakanalyzer/PeakMeter.cpp
    source/editor/analyser/peakanalyzer/PeakAnalyzerComponent.cpp
    source/editor/analyser/cliptype/ClipTypeComponent.cpp
    source/editor/analyser/DynamicsStatsComponent.cpp
    source/editor/analyser/AnalyserComponent.cpp
    source/editor/ControlPanel.cpp
    source/editor/LinkingPanel.cpp
    source/editor/LeftPanel.cpp
    source/editor/CentralPanel.cpp
    source/editor/RightPanel.cpp
    source/editor/Header.cpp
    source/editor/WorkingPanel.cpp
    source/editor/MainComponent.cpp

    # Entry point
    source/PluginEditor.cpp
    source/PluginProcessor.cpp)

function(configure_peakeater_target target_name product_name plugin_code clap_id)
    cmake_parse_arguments(PE_TARGET "SMALL_VARIANT" "" "FORMATS;EXTRA_DEFINES" ${ARGN})

    juce_add_plugin(${target_name}
        PLUGIN_MANUFACTURER_CODE AXLR
        PLUGIN_CODE ${plugin_code}
        LV2URI ${CONAN_PROJECT_URL}
        FORMATS ${PE_TARGET_FORMATS}
        VST3_CATEGORIES Fx Distortion Dynamics
        AU_MAIN_TYPE kAudioUnitType_Effect
        HARDENED_RUNTIME_ENABLED True
        COMPANY_NAME ${CONAN_PROJECT_COMPANY}
        PRODUCT_NAME ${product_name})

    target_sources(${target_name}
        PRIVATE
        ${PEAKEATER_SHARED_SOURCES})

    target_include_directories(${target_name} PUBLIC source)

    target_compile_definitions(${target_name}
        PUBLIC
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        ${PE_TARGET_EXTRA_DEFINES})

    juce_generate_juce_header(${target_name})

    clap_juce_extensions_plugin(TARGET ${target_name}
        CLAP_ID "${clap_id}"
        CLAP_FEATURES audio-effect)

    target_link_libraries(${target_name}
        PRIVATE
        AudioPluginData
        juce::juce_dsp
        juce::juce_audio_utils
        juce::juce_audio_plugin_client
        PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
endfunction()

configure_peakeater_target(${CMAKE_PROJECT_NAME}
    "${CONAN_PROJECT_PRODUCT_NAME}"
    PkSp
    "audio.axlrtr.peakeater-spectral-beta"
    FORMATS AU VST3 LV2 Standalone)

configure_peakeater_target(peakeater_spectral_small
    "Peakeater Spectral Small"
    PkSm
    "audio.axlrtr.peakeater-spectral-small"
    FORMATS VST3 Standalone
    EXTRA_DEFINES PEAKEATER_SMALL_VARIANT=1)

configure_peakeater_target(peakeater_spectral_2
    "Peakeater Spectral 2"
    PkS2
    "audio.axlrtr.peakeater-spectral-2"
    FORMATS AU VST3 Standalone
    EXTRA_DEFINES PEAKEATER_SPECTRAL_2_VARIANT=1)

configure_peakeater_target(peakeater_spectral_3
    "Peakeater Spectral 3"
    PkS3
    "audio.axlrtr.peakeater-spectral-3"
    FORMATS VST3 Standalone
    EXTRA_DEFINES PEAKEATER_SPECTRAL_2_VARIANT=1 PEAKEATER_SPECTRAL_3_VARIANT=1)

juce_add_binary_data(AudioPluginData SOURCES
    assets/link.png
    assets/bypass.png
    assets/icon.png
    assets/logo_full.png
    assets/logo_plugin.png
    assets/WalkwayUpperBold.ttf)

set_target_properties(AudioPluginData PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_executable(peakeater_cpu_benchmark EXCLUDE_FROM_ALL
    tools/PeakeaterCpuBenchmark.cpp
    source/processor/SpectralMaximizer.cpp)

add_executable(peakeater_scheduler_test EXCLUDE_FROM_ALL tools/LookaheadSchedulerTest.cpp)
target_include_directories(peakeater_scheduler_test PRIVATE source)
target_compile_features(peakeater_scheduler_test PRIVATE cxx_std_20)
add_test(NAME LookaheadScheduler COMMAND peakeater_scheduler_test)

add_executable(pe_recovery_test EXCLUDE_FROM_ALL tools/LookaheadRecoveryTest.cpp)
target_compile_features(pe_recovery_test PRIVATE cxx_std_20)
add_test(NAME LookaheadRecovery COMMAND pe_recovery_test)

add_executable(pe_host_test EXCLUDE_FROM_ALL tools/ProcessorSchedulerTest.cpp)
target_link_libraries(pe_host_test PRIVATE peakeater_spectral_3 juce::juce_audio_utils juce::juce_dsp)
target_compile_features(pe_host_test PRIVATE cxx_std_20)
add_test(NAME ProcessorScheduler COMMAND pe_host_test)

add_executable(peakeater_cpu_benchmark_reference EXCLUDE_FROM_ALL
    tools/PeakeaterCpuBenchmark.cpp
    source/processor/SpectralMaximizer.cpp)

target_include_directories(peakeater_cpu_benchmark PRIVATE source)
target_include_directories(peakeater_cpu_benchmark_reference PRIVATE source)

target_compile_definitions(peakeater_cpu_benchmark PRIVATE
    PEAKEATER_SPECTRAL_2_VARIANT=1
    PEAKEATER_SPECTRAL_3_VARIANT=1
    PEAKEATER_CPU_BENCHMARK=1)

if(MSVC)
    target_compile_options(peakeater_spectral_3 PRIVATE /arch:AVX2)
    target_compile_options(peakeater_cpu_benchmark PRIVATE /arch:AVX2)
endif()

target_link_libraries(peakeater_cpu_benchmark
    PRIVATE
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags)

target_compile_definitions(peakeater_cpu_benchmark_reference PRIVATE
    PEAKEATER_SPECTRAL_3_VARIANT=1
    PEAKEATER_DISABLE_SETTLED_FAST_PATH=1
    PEAKEATER_REFERENCE_MATH=1)
target_link_libraries(peakeater_cpu_benchmark_reference
    PRIVATE
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags)

# Where to store the result of the Pluginval run
set(_PLUGINVAL_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}_artefacts/${CMAKE_BUILD_TYPE}/Pluginval")

# Adding targets for testing, signing and packaging which are very platform-specific
# NOTE: only macOS supports signing at the moment
if(APPLE)
    # Test we haven't missed any compiler options. Makes sense only on macOS at the moment
    add_test(NAME CompileOptions COMMAND python3 ${PROJECT_SOURCE_DIR}/test/test_compile_options.py --compile_commands=${CMAKE_BINARY_DIR}/compile_commands.json)

    # Marks VST and AU as CFBundle. Without it, TARGET_BUNDLE_DIR won't work and we need it
    # to get paths to sign and package them
    set_target_properties(${CMAKE_PROJECT_NAME}_AU ${CMAKE_PROJECT_NAME}_VST3 PROPERTIES BUNDLE TRUE)

    # Test VST3 and AU with Pluginval
    add_test(NAME Pluginval_VST3
        COMMAND pluginval --strictness-level 10 --verbose --skip-gui-tests --validate-in-process --output-dir ${_PLUGINVAL_OUT_DIR} $<TARGET_BUNDLE_DIR:${CMAKE_PROJECT_NAME}_VST3>)
    add_test(NAME Pluginval_AU
        COMMAND pluginval --strictness-level 10 --verbose --skip-gui-tests --validate-in-process --output-dir ${_PLUGINVAL_OUT_DIR} $<TARGET_BUNDLE_DIR:${CMAKE_PROJECT_NAME}_AU>)

    # Optionally, codesign all plugins before we've bundled them(ensure it with tests)
    if(DEFINED ENV{MACOS_APPLE_DEVELOPER_ID})
        add_custom_command(TARGET ${CMAKE_PROJECT_NAME}_AU VERBATIM POST_BUILD
            COMMAND codesign --force -s $ENV{MACOS_APPLE_DEVELOPER_ID} -v $<TARGET_BUNDLE_DIR:${CMAKE_PROJECT_NAME}_AU> --deep --strict --options=runtime --timestamp
            COMMENT "Codesigning AU")
        add_test(NAME Codesign_AU COMMAND codesign -dv --verify --verbose=4 $<TARGET_BUNDLE_DIR:${CMAKE_PROJECT_NAME}_AU>)

        add_custom_command(TARGET ${CMAKE_PROJECT_NAME}_CLAP VERBATIM POST_BUILD
            COMMAND codesign --force -s $ENV{MACOS_APPLE_DEVELOPER_ID} -v $<TARGET_BUNDLE_DIR:${CMAKE_PROJECT_NAME}_CLAP> --deep --strict --options=runtime --timestamp
            COMMENT "Codesigning CLAP")
        add_test(NAME Codesign_CLAP COMMAND codesign -dv --verify --verbose=4 $<TARGET_BUNDLE_DIR:${CMAKE_PROJECT_NAME}_CLAP>)

        add_custom_command(TARGET ${CMAKE_PROJECT_NAME}_LV2 VERBATIM POST_BUILD # lv2 is special, sign only TARGET_FILE
            COMMAND codesign --force -s $ENV{MACOS_APPLE_DEVELOPER_ID} -v $<TARGET_FILE:${CMAKE_PROJECT_NAME}_LV2> --deep --strict --options=runtime --timestamp
            COMMENT "Codesigning LV2")
        add_test(NAME Codesign_LV2 COMMAND codesign -dv --verify --verbose=4 $<TARGET_FILE:${CMAKE_PROJECT_NAME}_LV2>)

        add_custom_command(TARGET ${CMAKE_PROJECT_NAME}_VST3 VERBATIM POST_BUILD
            COMMAND codesign --force -s $ENV{MACOS_APPLE_DEVELOPER_ID} -v $<TARGET_BUNDLE_DIR:${CMAKE_PROJECT_NAME}_VST3> --deep --strict --options=runtime --timestamp
            COMMENT "Codesigning VST3")
        add_test(NAME Codesign_VST3 COMMAND codesign -dv --verify --verbose=4 $<TARGET_BUNDLE_DIR:${CMAKE_PROJECT_NAME}_VST3>)
    endif()

    # Build DMG
    set(_OSX_DMG_DIR "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}_artefacts/${CMAKE_BUILD_TYPE}/DMG")
    set(_OSX_DMG_FILE "${_OSX_DMG_DIR}/${CMAKE_PROJECT_NAME}-v${CMAKE_PROJECT_VERSION}-macOS-universal.dmg")
    get_target_property(_LV2_OUT_DIR ${CMAKE_PROJECT_NAME}_LV2 LIBRARY_OUTPUT_DIRECTORY) # Getting it like this since lv2 folder is not a bundle, so TARGET_BUNDLE_DIR wont work
    add_custom_target(DMG ALL
        BYPRODUCTS ${_OSX_DMG_FILE}
        VERBATIM
        COMMAND ${CMAKE_COMMAND} -E make_directory ${_OSX_DMG_DIR}
        COMMAND dmgbuild -s ${PROJECT_SOURCE_DIR}/config/dmgbuild.py -D au=$<TARGET_BUNDLE_DIR:${CMAKE_PROJECT_NAME}_AU> -D clap=$<TARGET_BUNDLE_DIR:${CMAKE_PROJECT_NAME}_CLAP> -D lv2=${_LV2_OUT_DIR} -D vst3=$<TARGET_BUNDLE_DIR:${CMAKE_PROJECT_NAME}_VST3> "${CMAKE_PROJECT_NAME}-${CMAKE_PROJECT_VERSION}" ${_OSX_DMG_FILE}
        COMMENT "Building DMG")
    add_dependencies(DMG ${CMAKE_PROJECT_NAME}_AU ${CMAKE_PROJECT_NAME}_CLAP ${CMAKE_PROJECT_NAME}_LV2 ${CMAKE_PROJECT_NAME}_VST3)

    # Optionally, codesign, notarize and stable the DMG(ensure it with test)
    if(DEFINED ENV{MACOS_APPLE_DEVELOPER_ID})
        add_custom_command(TARGET DMG VERBATIM POST_BUILD
            COMMAND codesign --force -s $ENV{MACOS_APPLE_DEVELOPER_ID} -v ${_OSX_DMG_FILE} --deep --strict --options=runtime --timestamp
            COMMAND xcrun notarytool submit ${_OSX_DMG_FILE} --keychain-profile "APPLE_SIGN_PROFILE" --wait
            COMMAND xcrun stapler staple ${_OSX_DMG_FILE}
            COMMENT "Codesigning, notarizing and stapling DMG")
        add_test(NAME Codesign_DMG COMMAND codesign -dv --verify --verbose=4 ${_OSX_DMG_FILE})
    endif()
else()
    # The way of getting path to .vst3 is tricky since actual bin is inside ARCHIVE_OUTPUT_DIRECTORY
    set(_VST3_BUNDLE "$<GENEX_EVAL:$<TARGET_PROPERTY:${CMAKE_PROJECT_NAME}_VST3,ARCHIVE_OUTPUT_DIRECTORY>>/${CONAN_PROJECT_PRODUCT_NAME}.vst3")

    # Getting it like this since lv2 folder is not a bundle, so TARGET_BUNDLE_DIR wont work
    get_target_property(_LV2_OUT_DIR ${CMAKE_PROJECT_NAME}_LV2 LIBRARY_OUTPUT_DIRECTORY)

    # Archive everything into ZIP
    set(_ARCHIVE_DIR "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}_artefacts/${CMAKE_BUILD_TYPE}/Archive")
    set(_ARCHIVE_FILE "${_ARCHIVE_DIR}/${CMAKE_PROJECT_NAME}-v${CMAKE_PROJECT_VERSION}-${CMAKE_HOST_SYSTEM_NAME}-x86_64.zip")
    add_custom_target(archive ALL
        VERBATIM
        COMMAND ${CMAKE_COMMAND} -E make_directory ${_ARCHIVE_DIR}
        COMMAND ${CMAKE_COMMAND} -E tar cf ${_ARCHIVE_FILE} --format=zip $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}_CLAP> ${_LV2_OUT_DIR} ${_VST3_BUNDLE}
        COMMENT "Archiving everything")
    add_dependencies(archive ${CMAKE_PROJECT_NAME}_CLAP ${CMAKE_PROJECT_NAME}_LV2 ${CMAKE_PROJECT_NAME}_VST3)

    # Test VST3 with Pluginval(only non-macOS-specific format supported)
    # NOTE#1: Unlike macOS, TARGET_FILE_DIR is used because unlike macOS is a file here
    # NOTE#2: Only Windows supports GUI on a CI, thus check for it
    add_test(NAME Pluginval_VST3
        COMMAND pluginval --strictness-level 10 --verbose $<$<NOT:$<BOOL:${WIN32}>>:--skip-gui-tests> --validate-in-process --output-dir ${_PLUGINVAL_OUT_DIR} ${_VST3_BUNDLE})
endif()

