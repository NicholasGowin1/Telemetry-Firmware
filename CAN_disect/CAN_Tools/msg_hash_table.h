// msg_hash_table.h
//
// A small hash table mapping an int key (a CAN message ID) to a
// MessageLabels: the set of VAL_ label tables for that message's signals.
// Knows nothing about DBC syntax - dbc_val_parser.h/.cpp builds one of
// these from parsed VAL_ statements. Separate chaining (vector-of-vectors
// per bucket), sized up front by the caller (see nextPow2 / buildTable in
// dbc_val_parser.h).
//
// Code generated/assisted by Anthropic Claude (Claude Sonnet 5)
// Date: September 24, 2026
#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <vector>

using namespace std;

// One signal's VAL_ table: raw value -> label, e.g. {0: "Off", 1: "On"}.
struct SignalLabels
{
    std::string signalName;
    std::map<long long, std::string> labels;
};

// All labelled signals belonging to one message (one CAN ID).
struct MessageLabels
{
    std::vector<SignalLabels> signals;
};

// Rounds n up to the next power of two, with a floor of 16. Used to size
// MsgHashTable from a count of distinct message IDs.
size_t nextPow2(size_t n);

class MsgHashTable
{
public:
    // n is the bucket count - see nextPow2 for how to choose it from the
    // number of distinct keys you're about to insert.
    explicit MsgHashTable(size_t n = 64);

    // Returns the MessageLabels for `key`, creating an empty one if this
    // is the first time `key` has been seen. Used while building the table.
    MessageLabels &getOrCreate(int key);

    // Returns a pointer to the MessageLabels for `key`, or nullptr if the
    // message has no VAL_ tables at all. Used on the per-frame hot path.
    const MessageLabels *find(int key) const;

    size_t bucketCount() const { return buckets_.size(); }

private:
    struct Entry
    {
        int key;
        MessageLabels value;
    };

    std::vector<std::vector<Entry>> buckets_; // separate chaining

    size_t index(int key) const;
};
