#pragma once

#include <cstddef>

namespace pgstream
{
struct HtmlAsset
{
    const char* data = nullptr;
    size_t size = 0;
    const char* mimeType = "application/octet-stream";
};

bool getHtmlAsset(const char* uri, HtmlAsset& asset);
}

