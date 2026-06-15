#pragma once

#include <cstddef>
#include <mutex>
#include <vector>

struct mg_connection;

namespace pgstream
{
struct WebSocketBroadcastResult
{
    int successfulSends = 0;
    int sendFailures = 0;
};

class WebSocketHub
{
public:
    void add(mg_connection* connection);
    void remove(mg_connection* connection);
    void clear();
    int count() const;
    WebSocketBroadcastResult broadcastBinary(const void* data, size_t bytes);

private:
    mutable std::mutex mutex;
    std::vector<mg_connection*> clients;
};
}
