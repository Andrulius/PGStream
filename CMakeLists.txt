cmake_minimum_required(VERSION 3.22)

project(PGStream VERSION 0.9.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
set(JUCE_COPY_PLUGIN_AFTER_BUILD OFF CACHE BOOL "" FORCE)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(CMAKE_SKIP_INSTALL_RULES ON CACHE BOOL "" FORCE)

add_subdirectory(external/JUCE)

set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
set(MBEDTLS_USER_CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/mbedtls_pgstream_config.h" CACHE FILEPATH "" FORCE)
set(USE_STATIC_MBEDTLS_LIBRARY ON CACHE BOOL "" FORCE)
set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)
add_subdirectory(external/mbedtls EXCLUDE_FROM_ALL)
if(TARGET mbedtls AND NOT TARGET MbedTLS::MbedTLS)
    add_library(MbedTLS::MbedTLS ALIAS mbedtls)
endif()
if(TARGET mbedx509 AND NOT TARGET MbedTLS::X509)
    add_library(MbedTLS::X509 ALIAS mbedx509)
endif()
if(TARGET mbedcrypto AND NOT TARGET MbedTLS::Crypto)
    add_library(MbedTLS::Crypto ALIAS mbedcrypto)
endif()

set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
add_subdirectory(external/opus EXCLUDE_FROM_ALL)

set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
set(NO_TESTS ON CACHE BOOL "" FORCE)
set(NO_WEBSOCKET ON CACHE BOOL "" FORCE)
set(NO_MEDIA OFF CACHE BOOL "" FORCE)
set(USE_GNUTLS OFF CACHE BOOL "" FORCE)
set(USE_MBEDTLS ON CACHE BOOL "" FORCE)
set(USE_NICE OFF CACHE BOOL "" FORCE)
set(PREFER_SYSTEM_LIB OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_DEPS_LIBS OFF CACHE BOOL "" FORCE)
set(ENABLE_OPENSSL OFF CACHE BOOL "" FORCE)
set(ENABLE_MBEDTLS OFF CACHE BOOL "" FORCE)
set(ENABLE_NSS OFF CACHE BOOL "" FORCE)
set(OPENSSL OFF CACHE BOOL "" FORCE)
set(MBEDTLS OFF CACHE BOOL "" FORCE)
set(NSS OFF CACHE BOOL "" FORCE)
set(GCM OFF CACHE BOOL "" FORCE)
set(LIBSRTP_TEST_APPS OFF CACHE BOOL "" FORCE)
set(ENABLE_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
add_subdirectory(external/libdatachannel EXCLUDE_FROM_ALL)

add_library(civetweb_static STATIC
    external/civetweb/src/civetweb.c)

target_include_directories(civetweb_static
    PUBLIC
        external/civetweb/include
    PRIVATE
        external/civetweb/src)

target_compile_definitions(civetweb_static
    PRIVATE
        USE_WEBSOCKET
        USE_IPV6
        NO_CGI
        NO_FILES
        NO_CACHING
        USE_MBEDTLS
        USE_STACK_SIZE=102400
        _CRT_SECURE_NO_WARNINGS
        WIN32_LEAN_AND_MEAN
        NOMINMAX)

target_link_libraries(civetweb_static
    PRIVATE
        MbedTLS::MbedTLS
        MbedTLS::X509
        MbedTLS::Crypto)

if(MSVC)
    target_compile_options(civetweb_static PRIVATE /W0)
endif()

juce_add_binary_data(PGStreamAssets
    HEADER_NAME BinaryData.h
    NAMESPACE PGStreamBinaryData
    SOURCES
        assets/web/index.html
        assets/web/app.js
        assets/web/pcm-worklet.js
        assets/web/style.css
        assets/logo.png
        assets/pgs.png)

juce_add_plugin(PGStream
    COMPANY_NAME "PigeonStream"
    COMPANY_WEBSITE "https://pigeonstream.local"
    COMPANY_EMAIL "support@pigeonstream.local"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_AUTO_MANIFEST TRUE
    VST3_CATEGORIES Fx Analyzer
    PLUGIN_MANUFACTURER_CODE Pstr
    PLUGIN_CODE Pgst
    FORMATS VST3
    PRODUCT_NAME "PGStream")

target_sources(PGStream
    PRIVATE
        src/PluginProcessor.cpp
        src/PluginEditor.cpp
        src/PluginState.cpp
        src/AudioTapFifo.cpp
        src/NetworkServer.cpp
        src/NetworkInterfaceUtils.cpp
        src/WebRtcAudioSender.cpp
        src/SelfSignedCertificate.cpp
        src/Resampler.cpp
        src/HtmlAssets.cpp)

target_compile_definitions(PGStream
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS)

target_include_directories(PGStream
    PRIVATE
        src)

target_link_libraries(PGStream
    PRIVATE
        PGStreamAssets
        civetweb_static
        Opus::opus
        LibDataChannel::LibDataChannelStatic
        MbedTLS::MbedTLS
        MbedTLS::X509
        MbedTLS::Crypto
        ws2_32
        iphlpapi
        crypt32
        advapi32
        user32
        gdi32
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
