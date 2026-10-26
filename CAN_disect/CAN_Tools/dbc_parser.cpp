// dbc_parser.cpp
// Parses BO_ lines (e.g. "BO_ 123 MyMessage: 8 MySender") <(tag) (ID) (name) (number of bytes in message) (sender)>
// Parses SG_ lines (e.g. "SG_ MySignal m: 0|16@1+ (1,0) [0|1] \"MyUnit\" 0") <(tag) (name) (multiplexer) (start|length@byte_order+/-) (factor,offset) (min|max) (unit) (receiver)>
// Parses VAL_ lines (e.g. "VAL_ MyValue 0 1 0 100") <(tag) (name) (min) (max) (default) (unit)>
#include "dbc_parser.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <istream>
#include <sstream>
#include <utility>

using namespace std;

namespace
{
    // ---- BO_ line: "BO_ <id> <name>: <dlc> <sender>" -----------------
    bool parseBoLine(const string &line, DbcMessage &msg)
    {
        istringstream iss(line);
        string tag;
        string name;
        string sender;
        unsigned long rawId = 0;
        int dlc = 0;

        iss >> tag;       // sets BO_ (assuming this is a BO line)
        if (tag != "BO_") // not a BO_ line
            return false;

        iss >> rawId >> name >> dlc >> sender;   // extracts ID, then <name>, number of bytes, and sender
        if (!name.empty() && name.back() == ':') // checks for nonempty names and removes trailing ':'
            name.pop_back();

        // Standard DBC convention: bit 31 of the raw id marks an extended
        // (29-bit) frame; the real arbitration id is the low 29 bits.
        msg.id = static_cast<int>(rawId & 0x1FFFFFFFu); // extracts the 29-bit ID
        msg.isExtended = (rawId & 0x80000000u) != 0;    // checks if the frame is extended
        // assign the values to the message
        msg.name = name;
        msg.dlc = dlc;
        msg.sender = sender;
        msg.signals.clear();
        return true;
    }

    // ---- SG_ line: "SG_ <name> [M|mN] : <start>|<len>@<order><sign>
    //                 (<factor>,<offset>) [<min>|<max>] "<unit>" <rx>" ---
    bool parseSgLine(const string &line, DbcSignal &sig)
    {
        istringstream iss(line);
        string tag;
        iss >> tag;
        if (tag != "SG_")
            return false;

        iss >> sig.name;

        string tok;
        iss >> tok;
        if (tok == "M")
        {
            sig.muxType = DbcSignal::Mux::Switch;
            iss >> tok; // consume ":"
        }
        else if (tok.size() > 1 && tok[0] == 'm' && isdigit(static_cast<unsigned char>(tok[1])))
        {
            sig.muxType = DbcSignal::Mux::Value;
            sig.muxValue = atoi(tok.c_str() + 1);
            iss >> tok; // consume ":"
        }
        // else tok is already ":" - a plain, non-multiplexed signal.
        if (tok != ":")
            return false;

        string layout;
        iss >> layout;                                                                                       // e.g. "0|16@1+"
        size_t barPos = layout.find('|');                                                                    // gives location of '|'
        size_t atPos = layout.find('@');                                                                     // gives location of '@'
        if (barPos == string::npos || atPos == string::npos || atPos < barPos || atPos + 2 >= layout.size()) // checks for valid layout format ('<start>|<len>@<order><sign>')
            return false;

        sig.startBit = atoi(layout.substr(0, barPos).c_str());                    // extracts the start bit
        sig.length = atoi(layout.substr(barPos + 1, atPos - barPos - 1).c_str()); // extracts the length (this is essentially a zone where the signal is read from)
        sig.bigEndian = (layout[atPos + 1] == '0');                               // checks if the signal is big-endian or motorola (reads from left to right) If not its little-endian or Intel and reads from right to left
        sig.isSigned = (layout[atPos + 2] == '-');                                // checks if the signal is signed or unsigned

        string factorOffset;
        iss >> factorOffset;                                                                       // "(0.25,0)" factor allows for scaling the signal value likecoolant temperature
                                                                                                   // from a sensor ranging form -40 to 215 also allows for fractional values with the factor
        if (factorOffset.size() >= 2 && factorOffset.front() == '(' && factorOffset.back() == ')') // checks for valid factor/offset format
            factorOffset = factorOffset.substr(1, factorOffset.size() - 2);
        size_t comma = factorOffset.find(',');
        if (comma == string::npos)
            return false;
        sig.factor = atof(factorOffset.substr(0, comma).c_str());  // sets the scaling factor
        sig.offset = atof(factorOffset.substr(comma + 1).c_str()); // sets the offset

        string minMax;
        iss >> minMax; // set the min and max values [0,0] means no bounds
        if (minMax.size() >= 2 && minMax.front() == '[' && minMax.back() == ']')
        {
            minMax = minMax.substr(1, minMax.size() - 2); // takes the values in the middle with '|'
            size_t bar2 = minMax.find('|');
            if (bar2 != string::npos) // assigns min and max values assuming they are correct
            {
                sig.minValue = atof(minMax.substr(0, bar2).c_str());
                sig.maxValue = atof(minMax.substr(bar2 + 1).c_str());
            }
        }
        // Unit is the quoted token; whatever follows (receiver names) is
        // ignored - decoding doesn't need it.
        string rest;
        getline(iss, rest);
        size_t q1 = rest.find('"');
        size_t q2 = (q1 == string::npos) ? string::npos : rest.find('"', q1 + 1); // if q1 is npos, q2 is also npos;
                                                                                  // otherwise, find the next quote after q1
        sig.unit = (q1 != string::npos && q2 != string::npos && q2 > q1)
                       ? rest.substr(q1 + 1, q2 - q1 - 1)
                       : string();

        return true;
    }

    // Code generated/assisted by Anthropic Claude (Claude Sonnet 5)
    // Date: September 22, 2026
    // Extracts length bits starting at DBC bit `startBit` out of `data`
    // (dataLen bytes), honoring Intel (@1) vs Motorola (@0) bit order.
    // Returns false if the signal's bit range doesn't fit inside the bytes
    // actually available (caller skips the signal rather than reading
    // garbage or crashing).
    bool extractBits(const uint8_t *data, uint8_t dataLen, int startBit, int length, bool bigEndian, uint64_t &result)
    {
        result = 0;
        if (length <= 0 || length > 64) // outside of valid range
            return false;

        int pos = startBit;
        for (int i = 0; i < length; ++i)
        {
            int byteIdx = pos / 8;
            int bitIdx = pos % 8;

            if (byteIdx < 0 || byteIdx >= dataLen) // out of bounds
                return false;

            uint8_t bit = (data[byteIdx] >> bitIdx) & 0x1; // shifts bit down to position 0 and masks it

            if (bigEndian)
            {
                // Motorola: walk MSB to LSB within a byte (bit 7 down to 0),
                // then jump to bit 7 of the next byte.
                result = (result << 1) | bit;
                pos += (bitIdx == 0) ? 15 : -1;
            }
            else
            {
                // Intel: bits run LSB first straight through the byte
                // array, so this is just a little-endian bit read.
                result |= (static_cast<uint64_t>(bit) << i);
                pos += 1;
            }
        }
        return true;
    }

    double toPhysical(const DbcSignal &sig, uint64_t raw)
    {
        if (sig.isSigned && sig.length < 64 && (raw & (uint64_t(1) << (sig.length - 1)))) // confirms that signal is signed and is <64 bits to avoid sign extension issues
        {
            int64_t signedRaw = static_cast<int64_t>(raw) - static_cast<int64_t>(uint64_t(1) << sig.length);
            return static_cast<double>(signedRaw) * sig.factor + sig.offset;
        }
        return static_cast<double>(raw) * sig.factor + sig.offset;
    }

}

bool DbcDatabase::loadFromStream(istream &in)
{
    messages_.clear(); // Clear existing messages
    string line;
    DbcMessage *current = nullptr;

    while (getline(in, line))
    {
        size_t start = line.find_first_not_of(" \t\r\n"); // Find first non-whitespace character
        if (start == string::npos)
            continue; // blank line

        if (line.compare(start, 3, "BO_") == 0) // finds first "BO_" line
        {
            DbcMessage msg;
            if (parseBoLine(line.substr(start), msg))
            {
                messages_.push_back(msg);    // adds BO_ message
                current = &messages_.back(); // adds current pointer to the new message
            }
            else
            {
                current = nullptr;
            }
        }
        else if (current != nullptr && line.compare(start, 3, "SG_") == 0)
        {
            DbcSignal sig;
            if (parseSgLine(line.substr(start), sig))
                current->signals.push_back(sig);
        }
        // Everything else (BU_, CM_, BA_, VAL_, VERSION, comments, ...) is
        // skipped - not needed to decode signal values.
    }

    return !messages_.empty();
}

bool DbcDatabase::load(const string &path) // load DBC file from on disk
{
    ifstream in(path);
    if (!in.is_open())
        return false;
    return loadFromStream(in);
}

bool DbcDatabase::loadFromString(const string &dbcText) // load DBC file from string
{
    istringstream iss(dbcText);
    return loadFromStream(iss);
}

const DbcMessage *DbcDatabase::findMessage(int id) // find message by ID
{
    for (const auto &m : messages_)
        if (m.id == id)
            return &m;
    return nullptr;
}

vector<DecodedSignal> DbcDatabase::decode(const DbcMessage &def, const uint8_t *data, uint8_t length)
{
    vector<DecodedSignal> out;
    if (data == nullptr || length == 0)
        return out;

    // A multiplexed message has exactly one switch 'M' signal; decode it
    // first so 'mN' signals can be filtered against its current value.
    bool hasMux = false;
    uint64_t muxValue = 0;
    for (const auto &sig : def.signals) // Finds if there's a multiplexed signal
    {
        if (sig.muxType == DbcSignal::Mux::Switch)
        {
            hasMux = extractBits(data, length, sig.startBit, sig.length, sig.bigEndian, muxValue);
            break;
        }
    }

    out.reserve(def.signals.size()); // reserves memory for decoded signals
    for (const auto &sig : def.signals)
    {
        if (sig.muxType == DbcSignal::Mux::Value && (!hasMux || muxValue != static_cast<uint64_t>(sig.muxValue))) // checks if the signal is multiplexed and if the current mux value matches the signal's mux value
        {
            continue; // this multiplexed branch isn't selected for this frame
        }

        uint64_t raw;
        if (!extractBits(data, length, sig.startBit, sig.length, sig.bigEndian, raw))
            continue; // payload too short for this signal - skip, don't crash

        DecodedSignal ds;
        ds.name = sig.name;
        ds.value = toPhysical(sig, raw);
        ds.unit = sig.unit;
        out.push_back(move(ds));
    }

    return out;
}