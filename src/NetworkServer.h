#pragma once

#include "AudioTapFifo.h"
#include "NetworkInterfaceUtils.h"
#include "Resampler.h"
#include "StreamTypes.h"
#include "WebRtcAudioSender.h"
#include <juce_core/juce_core.h>
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

struct mg_connection;
struct mg_context;

namespace pgstream
{
class NetworkServer final : private juce::Thread
{
public:
    explicit NetworkServer(AudioTapFifo& fifoToRead);
    ~NetworkServer() override;

    void applyConfig(const StreamConfig& newConfig);
    void setSessionSampleRate(double sampleRate);
    void setActiveProfileCallback(std::function<void(OpusBitrateMode, LatencyMode)> callback);
    StreamStats getStats() const;

private:
    void run() override;

    bool startServer(const StreamConfig& newConfig);
    void stopServer();
    void updateStatus(juce::String text);
    void refreshLanSelection(int port);

    int handleHttpRequest(mg_connection* connection);

    static int requestHandler(mg_connection* connection, void* cbdata);
    static int wsConnectHandler(const mg_connection* connection, void* cbdata);
    static void wsReadyHandler(mg_connection* connection, void* cbdata);
    static int wsDataHandler(mg_connection* connection, int bits, char* data, size_t dataLen, void* cbdata);
    static void wsCloseHandler(const mg_connection* connection, void* cbdata);
    static int logHandler(const mg_connection*, const char*);

    void handleWebSocketText(mg_connection* connection, const char* data, size_t dataLen);
    void notifyActiveProfileChanged(OpusBitrateMode bitrateMode, LatencyMode latencyMode);

    AudioTapFifo& fifo;
    mutable std::mutex configMutex;
    StreamConfig config;

    mutable std::mutex callbackMutex;
    std::function<void(OpusBitrateMode, LatencyMode)> activeProfileCallback;

    mutable std::mutex statusMutex;
    juce::String statusText { "disabled" };
    juce::String lanUrl;
    juce::String currentLanIp;
    juce::String listenAddress { "0.0.0.0" };
    juce::StringArray candidateLanUrls;
    juce::StringArray candidateLanDescriptions;

    std::atomic<bool> enabled { false };
    std::atomic<bool> contextRunning { false };
    std::atomic<double> sessionSampleRate { 48000.0 };
    std::atomic<uint64_t> framesSent { 0 };
    std::atomic<uint64_t> serverFifoUnderruns { 0 };

    mg_context* context = nullptr;
    WebRtcAudioSender webrtcSender;
    Resampler webrtcResampler;

    std::vector<float> readBuffer;
    std::vector<float> webrtcResampledBuffer;
    std::vector<float> webrtcSilenceBuffer;
};
}
