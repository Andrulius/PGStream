#pragma once

#include "AudioTapFifo.h"
#include "NetworkInterfaceUtils.h"
#include "Resampler.h"
#include "StreamTypes.h"
#include "WebSocketHub.h"
#include <juce_core/juce_core.h>
#include <atomic>
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
    StreamStats getStats() const;

private:
    void run() override;

    bool startServer(const StreamConfig& newConfig);
    void stopServer();
    void updateStatus(juce::String text);
    void refreshLanSelection(int port);

    int handleHttpRequest(mg_connection* connection);
    int handleInitSsl(void* sslContext);

    static int requestHandler(mg_connection* connection, void* cbdata);
    static int wsConnectHandler(const mg_connection* connection, void* cbdata);
    static void wsReadyHandler(mg_connection* connection, void* cbdata);
    static int wsDataHandler(mg_connection* connection, int bits, char* data, size_t dataLen, void* cbdata);
    static void wsCloseHandler(const mg_connection* connection, void* cbdata);
    static int initSslHandler(void* sslContext, void* userData);
    static int logHandler(const mg_connection*, const char*);

    void buildAndBroadcastFrame(const float* interleavedStereo,
                                size_t frameCount,
                                const StreamConfig& frameConfig,
                                int sampleRate,
                                bool silence);
    void appendAndBroadcastPackets(const float* interleavedStereo,
                                   size_t frameCount,
                                   const StreamConfig& frameConfig,
                                   int sampleRate,
                                   size_t packetFrames);
    void resetPacketAccumulator();
    void appendU16(uint16_t value);
    void appendU32(uint32_t value);

    AudioTapFifo& fifo;
    mutable std::mutex configMutex;
    StreamConfig config;

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
    std::atomic<uint32_t> sequence { 0 };
    std::atomic<uint64_t> framesSent { 0 };
    std::atomic<uint64_t> serverFifoUnderruns { 0 };
    std::atomic<uint64_t> networkPacketsSent { 0 };
    std::atomic<uint64_t> websocketSendFailures { 0 };

    mg_context* context = nullptr;
    WebSocketHub hub;
    Resampler resampler;

    std::vector<float> readBuffer;
    std::vector<float> resampledBuffer;
    std::vector<float> packetBuffer;
    size_t packetBufferFrames = 0;
    std::vector<float> silenceBuffer;
    std::vector<uint8_t> frameBuffer;
};
}
