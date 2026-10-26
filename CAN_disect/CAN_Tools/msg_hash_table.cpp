#include "msg_hash_table.h"

#include <cstdint>

size_t nextPow2(size_t n)
{
    size_t p = 16; // sensible minimum
    while (p < n)
        p <<= 1;
    return p;
}

MsgHashTable::MsgHashTable(size_t n) : buckets_(n == 0 ? 1 : n) {}
// Code assisted by Anthropic Claude (Claude Sonnet 5)
// Date: September 21, 2026
size_t MsgHashTable::index(int key) const
{
    // murmur3 finalizer: mixes every input bit into every output bit, so
    // CAN IDs that only differ in a few bit positions (common with
    // J1939-style extended IDs) don't all land in the same bucket the way
    // a plain `key % size` would.
    uint32_t h = static_cast<uint32_t>(key);
    h ^= h >> 16;
    h *= 0x85ebca6bu;
    h ^= h >> 13;
    h *= 0xc2b2ae35u;
    h ^= h >> 16;
    return h % buckets_.size();
}

MessageLabels &MsgHashTable::getOrCreate(int key)
{
    auto &chain = buckets_[index(key)];
    for (auto &e : chain)
        if (e.key == key)
            return e.value;
    chain.push_back({key, {}});
    return chain.back().value;
}

const MessageLabels *MsgHashTable::find(int key) const
{
    for (const auto &e : buckets_[index(key)])
        if (e.key == key)
            return &e.value;
    return nullptr;
}
