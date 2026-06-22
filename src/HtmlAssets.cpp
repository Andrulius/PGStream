#include "HtmlAssets.h"
#include <BinaryData.h>
#include <cstring>

namespace pgstream
{
bool getHtmlAsset(const char* uri, HtmlAsset& asset)
{
    if (uri == nullptr || std::strcmp(uri, "/") == 0 || std::strcmp(uri, "/index.html") == 0)
    {
        asset = { PGStreamBinaryData::index_html, static_cast<size_t> (PGStreamBinaryData::index_htmlSize), "text/html; charset=utf-8" };
        return true;
    }

    if (std::strcmp(uri, "/app.js") == 0)
    {
        asset = { PGStreamBinaryData::app_js, static_cast<size_t> (PGStreamBinaryData::app_jsSize), "application/javascript; charset=utf-8" };
        return true;
    }

    if (std::strcmp(uri, "/style.css") == 0)
    {
        asset = { PGStreamBinaryData::style_css, static_cast<size_t> (PGStreamBinaryData::style_cssSize), "text/css; charset=utf-8" };
        return true;
    }

    if (std::strcmp(uri, "/pcm-worklet.js") == 0)
    {
        asset = { PGStreamBinaryData::pcmworklet_js, static_cast<size_t> (PGStreamBinaryData::pcmworklet_jsSize), "application/javascript; charset=utf-8" };
        return true;
    }

    if (std::strcmp(uri, "/pgs.png") == 0)
    {
        asset = { reinterpret_cast<const char*> (PGStreamBinaryData::pgs_png),
                  static_cast<size_t> (PGStreamBinaryData::pgs_pngSize),
                  "image/png" };
        return true;
    }

    return false;
}
}
