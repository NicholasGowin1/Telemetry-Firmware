// dbc_scan.h
//
// DbcDatabase deliberately hides iteration over its messages (see the
// note in dbc_parser.h: "a caller that needs every message definition
// re-scans the .dbc itself"). A simulator is exactly that caller - it
// needs to know every message ID that exists so it can send all of
// them - so this does the same lightweight BO_-only rescan that
// dbc_val_parser.h's readValRecords() does for VAL_ lines.
//
// Code generated/assisted by Anthropic Claude (Claude Sonnet 5)
// Date: September 24, 2026
#pragma once

#include <string>
#include <vector>

struct ScannedMessage
{
    int id = 0;
    std::string name;
    int dlc = 0;
    bool isExtended = false;
};

// Re-scans `path` for BO_ lines only. Returns one entry per message in
// file order; duplicate IDs (a malformed DBC) are returned as-is and
// left for the caller to deal with.
std::vector<ScannedMessage> collectMessageIds(const std::string &path);
