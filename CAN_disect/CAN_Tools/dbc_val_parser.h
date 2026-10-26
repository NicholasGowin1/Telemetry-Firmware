// dbc_val_parser.h
//
// Parses VAL_ statements out of a .dbc file and builds a MsgHashTable
// (message id -> per-signal label tables) from them. DbcDatabase
// (dbc_parser.h/.cpp) skips VAL_ lines entirely, so this reads the raw
// file text separately, the same way dbc_playground.cpp's
// collectMessageIds() re-scans for BO_ lines.
//
// Usage:
//   vector<int> keys;
//   auto records = readValRecords(path, keys);
//   MsgHashTable table = buildTable(records, keys);
//
//   // per frame:
//   const MessageLabels *m = table.find(msg.id);   // masked, decimal id
//   if (m) { ... walk m->signals, find by name, look up raw value ... }
//
#pragma once

#include "msg_hash_table.h"

#include <string>
#include <vector>

using namespace std;
// One parsed VAL_ statement, before it's folded into the hash table:
//   VAL_ <ID> <name> <signal[0]> "<messages[0]>" <signal[1]> "<messages[1]>" ... ;
struct DbcValueLocation
{
    unsigned long ID = 0;
    std::string name;
    std::vector<long long> signal;     // raw values
    std::vector<std::string> messages; // matching labels, same order/length as signal
};

// Parses one already-joined VAL_ statement (a single line, or several
// lines already concatenated - see readValRecords). Returns false for
// anything that isn't a well-formed VAL_ statement, including
// environment-variable VAL_ lines (no numeric message ID) and statements
// with an unterminated quoted label. May throw std::invalid_argument
// (via std::stoll) if a value token isn't numeric - callers doing bulk
// loading should catch that per-statement (see readValRecords).
bool parseValLine(const std::string &line, DbcValueLocation &valLoc);

// Pass 1: reads `path` once, joins any VAL_ statement that wraps across
// multiple lines, and parses each one with parseValLine. `keys` receives
// the masked (29-bit) message id for each successfully parsed record, in
// the same order as the returned vector, so keys[i] belongs to records[i].
// Lines that fail to parse (env-var VAL_, malformed statements, a
// non-numeric value token) are skipped rather than aborting the load.
std::vector<DbcValueLocation> readValRecords(const std::string &path, std::vector<int> &keys);

// Pass 2: counts the distinct keys, sizes a MsgHashTable to fit them at a
// ~0.75 load factor, then inserts every record. Call this after
// readValRecords with the same records/keys pair.
MsgHashTable buildTable(const std::vector<DbcValueLocation> &records, const std::vector<int> &keys);
