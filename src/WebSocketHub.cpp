#include "WebSocketHub.h"
#include <civetweb.h>
#include <algorithm>

namespace pgstream
{
void WebSocketHub::add(mg_connection* connection)
{
    if (connection == nullptr)
        return;

    std::lock_guard<std::mutex> lock(mutex);
    if (std::find(clients.begin(), clients.end(), connection) == clients.end())
        clients.push_back(connection);
}

void WebSocketHub::remove(mg_connection* connection)
{
    std::lock_guard<std::mutex> lock(mutex);
    clients.erase(std::remove(clients.begin(), clients.end(), connection), clients.end());
}

void WebSocketHub::clear()
{
    std::lock_guard<std::mutex> lock(mutex);
    clients.clear();
}

int WebSocketHub::count() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return static_cast<int> (clients.size());
}

WebSocketBroadcastResult WebSocketHub::broadcastBinary(const void* data, size_t bytes)
{
    if (data == nullptr || bytes == 0)
        return {};

    WebSocketBroadcastResult result;
    std::lock_guard<std::mutex> lock(mutex);

    auto it = clients.begin();
    while (it != clients.end())
    {
        auto* connection = *it;
        const auto written = mg_websocket_write(connection,
                                               MG_WEBSOCKET_OPCODE_BINARY,
                                               static_cast<const char*> (data),
                                               bytes);
        if (written <= 0)
        {
            it = clients.erase(it);
            ++result.sendFailures;
            continue;
        }

        ++result.successfulSends;
        ++it;
    }

    return result;
}
}
