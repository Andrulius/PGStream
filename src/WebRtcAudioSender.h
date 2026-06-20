#pragma once

#include "StreamTypes.h"
#include <juce_core/juce_core.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

struct mg_connection;
struct OpusEncoder;

namespace rtc
{
class PeerConnection;
class Track;
}

namespace pgstream
{
class WebRtcAudioSender
{
public:
    WebRtcAudioSender();
    ~WebRtcAudioSender();

    void applyConfig(const StreamConfig& newConfig);
    void handleOffer(mg_connection* connection, const juce::var& message);
    void handleRemoteCandidate(mg_connection* connection, const juce::var& message);
    void removeConnection(mg_connection* connection);
    void clear();

    void encodeAndSend(const float* interleavedStereo48k, size_t frameCount);
    void resetAudio();

    int peerCount() const;
    int openTrackCount() const;
    bool hasOpenTrack() const;
    size_t opusFrameCount() const;

    void fillStats(StreamStats& stats) const;

private:
    struct Peer;

    using PeerPtr = std::shared_ptr<Peer>;

    void configureEncoderIfNeeded();
    void recreateEncoder();
    void sendEncodedFrame(const uint8_t* data, size_t bytes, uint64_t frameStartSample);
    void sendEncodedFrameLocked(const uint8_t* data, size_t bytes, uint64_t frameStartSample);
    void sendJson(mg_connection* connection, const juce::String& json) const;
    void closePeer(PeerPtr peer);
    PeerPtr findPeer(mg_connection* connection) const;

    static juce::String stateName(int state);
    static juce::String iceStateName(int state);

    mutable std::mutex mutex;
    StreamConfig config;
    std::unordered_map<mg_connection*, PeerPtr> peers;

    std::unique_ptr<OpusEncoder, void (*)(OpusEncoder*)> encoder;
    int encoderBitrateBps = 0;
    int encoderFrameMs = 0;
    size_t encoderFrameFrames = 960;
    std::vector<float> pendingPcm;
    size_t pendingFrames = 0;
    std::vector<uint8_t> opusPacket;
    uint64_t encodedSampleCursor = 0;

    std::atomic<uint64_t> encodedFrames { 0 };
    std::atomic<uint64_t> encodedPackets { 0 };
    std::atomic<uint64_t> encodedBytes { 0 };
    std::atomic<uint64_t> sendCalls { 0 };
    std::atomic<uint64_t> encoderOverloadWarnings { 0 };
};
}
