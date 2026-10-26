// Code generated/assisted by Anthropic Claude (Claude Sonnet 5)
// Date: September 21, 2026
// dbc_parser.h
//
// Minimal .dbc parser and CAN signal decoder: covers BO_ (message) and
// SG_ (signal) records, Intel (@1) and Motorola (@0) byte order,
// signed/unsigned values, scale/offset, and simple multiplexing (an `M`
// switch signal plus `mN` signals selected by the switch's value). Other
// DBC sections (BU_, CM_, BA_, VAL_, comments, etc.) are skipped - they
// aren't needed to decode signal values.
//
// Deliberately exposes only ID-based lookup (findMessage), not iteration -
// a caller that needs every message definition (e.g. a demo simulator)
// re-scans the .dbc itself; see monitor.cpp for that pattern.
#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

using namespace std;

struct DbcSignal
{
    enum class Mux
    {
        None = 0,   // not part of any multiplex group
        Switch = 1, // the `M` signal - its value selects which `mN` signals apply
        Value = 2   // an `mN` signal - only decoded when the switch equals N
    };

    string name;
    int startBit = 0;
    int length = 0;
    bool bigEndian = false; // true = Motorola (@0), false = Intel (@1)
    bool isSigned = false;
    double factor = 1.0;
    double offset = 0.0;
    double minValue = 0.0;
    double maxValue = 0.0;
    string unit;

    Mux muxType = Mux::None;
    int muxValue = 0; // meaningful only when muxType == Mux::Value
};

struct DbcMessage
{
    int id = 0;
    string name;
    int dlc = 0;
    bool isExtended = false;
    string sender;
    vector<DbcSignal> signals;
};

struct DecodedSignal
{
    string name;
    double value = 0.0;
    string unit;
};

class DbcDatabase
{
public:
    // Loads from a .dbc file on disk. Needs a filesystem - fine on
    // Linux/desktop, not usable as-is on a target with no filesystem.
    bool load(const string &path);

    // Loads from a .dbc file already in memory - e.g. a DBC converted to a
    // C string literal or byte array at build time (`xxd -i sample.dbc`).
    // This is the one to use on a target with no filesystem.
    bool loadFromString(const string &dbcText);

    bool empty() const { return messages_.empty(); }
    size_t messageCount() const { return messages_.size(); }

    const DbcMessage *findMessage(int id);

    // Decodes one frame's payload against a message definition. A signal
    // whose bit range doesn't fit within `length` bytes is silently
    // skipped, not a crash - callers can detect "nothing decoded" the way
    // telemetry.cpp does: decoded.empty() && !def.signals.empty(). An
    // inactive multiplexed branch (an `mN` whose N doesn't match the
    // frame's current switch value) is skipped the same way.
    static vector<DecodedSignal> decode(const DbcMessage &def, const uint8_t *data, uint8_t length);

private:
    vector<DbcMessage> messages_;

    bool loadFromStream(istream &in);
};
