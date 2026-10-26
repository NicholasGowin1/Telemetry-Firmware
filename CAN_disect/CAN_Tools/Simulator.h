// Simulator.h
//
// CAN traffic simulator: sends every message from collectMessageIds() on
// a CanBus at a fixed cycle time, with slowly changing placeholder
// values - the C++ equivalent of Simulator.py's simulate()/raw_value().
// This is a library function, not a program: both Monitor's --demo mode
// and the standalone simulator CLI (simulator_main.cpp) call it, the
// same way Monitor.py imports and threads Simulator.simulate().
//
// Code generated/assisted by Anthropic Claude (Claude Sonnet 5)
// Date: September 24, 2026
#pragma once

#include "can_bus.h"
#include "dbc_parser.h"
#include "dbc_scan.h"
#include "msg_hash_table.h"

#include <atomic>
#include <vector>

// Sends every message in `messages` on `bus` at `periodMs`. Each
// signal's placeholder value is a slow sine wave sized to its bit
// width/sign, or - when `labels` has VAL_ entries for it - a slow cycle
// through its known enum values, so Monitor's label lookup has
// something to show. A multiplexed message has one branch encoded per
// cycle so the frame decodes back to something consistent.
//
// Runs until `duration` seconds elapse (0 = forever) or `*stop` becomes
// true, whichever happens first. Pass `stop = nullptr` to only rely on
// `duration`.
void simulate(CanBus &bus, DbcDatabase &db, const std::vector<ScannedMessage> &messages,
              const MsgHashTable *labels, int periodMs, double duration, std::atomic<bool> *stop);
