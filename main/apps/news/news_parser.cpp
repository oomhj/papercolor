/**
 * PaperColor — RSS XML Parser Implementation (P0)
 *
 * Parses RSS 2.0 XML by finding <item>...</item> blocks
 * and extracting title/description/source/pubDate/link via
 * simple tag matching. Fast and low-memory.
 */
#include "news_parser.h"
#include "news_app.h"
#include <cstring>
#include <cstdio>
#include <esp_log.h>

static const char* TAG = "NewsParse";

// ── Simple tag content extractor ─────────────────────────────
// Given a string like "...<title>Foo</title>..." returns "Foo".
// Returns empty string if tag not found in [start, end).

static std::string extract_tag(const char* start, const char* end,
                                const char* tag_open, const char* tag_close)
{
    const char* o = strstr(start, tag_open);
    if (!o || o >= end) return {};

    const char* content = o + strlen(tag_open);
    // Skip leading whitespace
    while (content < end && (*content == ' ' || *content == '\n' || *content == '\r' || *content == '\t'))
        content++;

    const char* c = strstr(content, tag_close);
    if (!c || c > end) return {};

    size_t len = c - content;
    // Trim trailing whitespace
    while (len > 0 && (content[len-1] == ' ' || content[len-1] == '\n' ||
                       content[len-1] == '\r' || content[len-1] == '\t'))
        len--;

    return std::string(content, len);
}

// ── CDATA stripper ───────────────────────────────────────────
static std::string strip_cdata(const std::string& s)
{
    std::string r = s;
    // Remove <![CDATA[ ... ]]>
    const char* cdata_start = "<![CDATA[";
    const char* cdata_end   = "]]>";
    size_t p;
    while ((p = r.find(cdata_start)) != std::string::npos) {
        size_t q = r.find(cdata_end, p);
        if (q == std::string::npos) break;
        r = r.substr(0, p) + r.substr(q + 3);
    }
    // Decode common entities
    auto replace_all = [](std::string& s, const char* from, const char* to) {
        size_t p;
        while ((p = s.find(from)) != std::string::npos)
            s.replace(p, strlen(from), to);
    };
    replace_all(r, "&amp;",  "&");
    replace_all(r, "&lt;",   "<");
    replace_all(r, "&gt;",   ">");
    replace_all(r, "&quot;", "\"");
    replace_all(r, "&apos;", "'");
    return r;
}

// ── Main parser ──────────────────────────────────────────────

std::vector<NewsItem> news_parse_rss(const char* xml, int max_items)
{
    std::vector<NewsItem> items;

    if (!xml || !*xml) {
        ESP_LOGW(TAG, "Empty XML");
        return items;
    }

    const char* item_tag   = "<item>";
    const char* item_end   = "</item>";
    const char* ptr = xml;

    while (items.size() < (size_t)max_items) {
        const char* item_start = strstr(ptr, item_tag);
        if (!item_start) break;

        const char* item_stop = strstr(item_start + 6, item_end);
        if (!item_stop) break;

        const char* block_start = item_start + 6;  // after <item>
        const char* block_end   = item_stop;

        std::string title = strip_cdata(extract_tag(block_start, block_end,
            "<title>", "</title>"));
        std::string desc  = strip_cdata(extract_tag(block_start, block_end,
            "<description>", "</description>"));
        std::string date  = extract_tag(block_start, block_end,
            "<pubDate>", "</pubDate>");
        std::string link  = extract_tag(block_start, block_end,
            "<link>", "</link>");
        std::string src   = extract_tag(block_start, block_end,
            "<source>", "</source>");

        // Fallback: try <dc:creator> for author
        if (src.empty()) {
            src = extract_tag(block_start, block_end,
                "<dc:creator>", "</dc:creator>");
        }

        if (!title.empty()) {
            // Truncate long titles for display
            if (title.length() > 120) title = title.substr(0, 117) + "...";
            if (desc.length() > 300)  desc  = desc.substr(0, 297) + "...";

            items.push_back({title, desc, src, date, link});
            ESP_LOGD(TAG, "[%zu] %s", items.size() - 1, title.c_str());
        }

        ptr = item_stop + 7;  // after </item>
    }

    ESP_LOGI(TAG, "Parsed %zu news items", items.size());
    return items;
}
