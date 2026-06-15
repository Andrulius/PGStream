cmake_minimum_required(VERSION 3.22)

project(PGStream VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
set(JUCE_COPY_PLUGIN_AFTER_BUILD OFF CACHE BOOL "" FORCE)

add_subdirectory(external/JUCE)

find_path(PGSTREAM_OPENSSL_INCLUDE_DIR
    NAMES openssl/ssl.h
    PATHS
        "C:/Program Files (x86)/Microsoft Visual Studio/2019/Community/Common7/IDE/Extensions/Microsoft/Python/Miniconda/Miniconda3-x64/Library/include"
        "$ENV{OPENSSL_ROOT_DIR}/include"
        "C:/Program Files/OpenSSL-Win64/include"
        "C:/OpenSSL-Win64/include"
    REQUIRED)

find_library(PGSTREAM_OPENSSL_SSL_LIBRARY
    NAMES libssl_static libssl
    PATHS
        "$ENV{OPENSSL_ROOT_DIR}/lib"
        "C:/Program Files/OpenSSL-Win64/lib"
        "C:/OpenSSL-Win64/lib"
    REQUIRED)

find_library(PGSTREAM_OPENSSL_CRYPTO_LIBRARY
    NAMES libcrypto_static libcrypto
    PATHS
        "$ENV{OPENSSL_ROOT_DIR}/lib"
        "C:/Program Files/OpenSSL-Win64/lib"
        "C:/OpenSSL-Win64/lib"
    REQUIRED)

add_library(civetweb_static STATIC
    external/civetweb/src/civetweb.c)

target_include_directories(civetweb_static
    PUBLIC
        external/civetweb/include
    PRIVATE
        external/civetweb/src
        ${PGSTREAM_OPENSSL_INCLUDE_DIR})

target_compile_definitions(civetweb_static
    PRIVATE
        USE_WEBSOCKET
        USE_IPV6
        NO_CGI
        NO_FILES
        NO_CACHING
        NO_SSL_DL
        OPENSSL_API_1_1
        USE_STACK_SIZE=102400
        _CRT_SECURE_NO_WARNINGS
        WIN32_LEAN_AND_MEAN
        NOMINMAX)

if(MSVC)
    target_compile_options(civetweb_static PRIVATE /W0)
endif()

juce_add_binary_data(PGStreamAssets
    HEADER_NAME BinaryData.h
    NAMESPACE PGStreamBinaryData
    SOURCES
        assets/web/index.html
        assets/web/app.js
        assets/web/audio-worklet.js
        assets/web/style.css
        assets/pgs.png
        assets/certs/dev-cert.pem
        assets/certs/dev-key.pem)

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
        src/WebSocketHub.cpp
        src/Resampler.cpp
        src/CertificateManager.cpp
        src/HtmlAssets.cpp)

target_compile_definitions(PGStream
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS)

if(MSVC)
    target_compile_options(PGStream
        PRIVATE
            /MP1
            $<$<CONFIG:Release>:/Od>
            $<$<CONFIG:Release>:/Ob0>)
endif()

target_include_directories(PGStream
    PRIVATE
        src
        ${PGSTREAM_OPENSSL_INCLUDE_DIR})

target_link_libraries(PGStream
    PRIVATE
        PGStreamAssets
        civetweb_static
        ${PGSTREAM_OPENSSL_SSL_LIBRARY}
        ${PGSTREAM_OPENSSL_CRYPTO_LIBRARY}
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
