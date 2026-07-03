/**
 * PaperColor — RSS XML Parser (P0)
 *
 * Simple tag-based RSS parser. Extracts <item> blocks then
 * pulls <title>, <description>, <source>, <pubDate>, <link>.
 *
 * Not a full XML parser — works for well-formed RSS 2.0.
 */

#pragma once

#include <cstdint>
#include <vector>
#include <string>

struct NewsItem;  // forward decl from news_app.h

/**
 * @brief Parse RSS XML text into a list of NewsItem.
 * @param xml      NUL-terminated RSS XML.
 * @param max_items  Max items to extract.
 * @return Vector of parsed items (empty on failure).
 */
std::vector<NewsItem> news_parse_rss(const char* xml, int max_items = 50);
