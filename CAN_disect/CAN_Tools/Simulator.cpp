// Simulator.cpp - see Simulator.h.
//
// packBits() below is the encode-side twin of dbc_parser.cpp's private
// extractBits(): same bit-walking rules for Intel (@1) vs Motorola (@0)
// layouts, just writing instead of reading. It's re-derived here rather
// than reused because extractBits is file-local to dbc_parser.cpp.
//
// Code generated/assisted by Anthropic Claude (Claude Sonnet 5)
// Date: September 24, 2026
#include "Simulator.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>

namespace
{
    // Writes `length` bits of `value` into `data` starting at DBC bit
    // `startBit`. Bits that would fall outside `dataLen` are silently
    // dropped, mirroring extractBits()'s tolerance for short payloads.
    void packBits(uint8_t *data, uint8_t dataLen, int startBit, int length, bool bigEndian, uint64_t value)
    {
        int pos = startBit;
        for (int i = 0; i < length; ++i)
        {
            int byteIdx = pos / 8;
            int bitIdx = pos % 8;

            if (byteIdx >= 0 && byteIdx < dataLen)
            {
                // Motorola walks a signal's bits MSB-first (bit length-1
                // down to 0); Intel walks LSB-first, straight through -
                // same order extractBits() reads them back in.
                int bit = bigEndian ? static_cast<int>((value >> (length - 1 - i)) & 0x1)
                                    : static_cast<int>((value >> i) & 0x1);
                if (bit)
                    data[byteIdx] |= static_cast<uint8_t>(1u << bitIdx);
                else
                    data[byteIdx] &= static_cast<uint8_t>(~(1u << bitIdx));
            }

            pos += bigEndian ? ((bitIdx == 0) ? 15 : -1) : 1;
        }
    }

    // Two's-complement-encodes `value` into the low `length` bits.
    uint64_t toRawBits(long long value, int length)
    {
        if (length <= 0)
            return 0;
        uint64_t mask = (length >= 64) ? ~0ULL : ((1ULL << length) - 1);
        return static_cast<uint64_t>(value) & mask;
    }

    // Placeholder RAW value for a signal at time t (seconds), mirroring
    // Simulator.py's raw_value(): mostly holds a plausible nominal value,
    // with an occasional brief excursion for enum/fault-style signals, or
    // a slow sine wave sized to its bit width/sign for continuous ones.
    //
    // Every DBC-enum and single-bit signal here uses the same "mostly
    // nominal, rare short blip" shape rather than evenly cycling through
    // every possible state: a demo dashboard that flashes FAULT on every
    // inverter continuously isn't a useful demo of the fault-handling UI,
    // it's just noise. `phase` staggers *when* each signal's blip lands so
    // multiple inverters/channels don't all fault in lockstep either.
    long long rawValue(const DbcSignal &sig, double t, double phase, const MessageLabels *msgLabels)
    {
        constexpr double kCycleSeconds = 25.0; // how often a blip CAN occur
        constexpr double kBlipSeconds = 1.5;   // how long a blip lasts once it does

        if (msgLabels != nullptr)
        {
            for (const auto &s : msgLabels->signals)
            {
                if (s.signalName == sig.name && !s.labels.empty())
                {
                    std::vector<long long> keys;
                    keys.reserve(s.labels.size());
                    for (const auto &kv : s.labels)
                        keys.push_back(kv.first);
                    std::sort(keys.begin(), keys.end());

                    // By DBC convention the smallest enum value is the
                    // nominal one (0 = "OK"/"Normal"/"Off" is near-universal
                    // for status/fault signals) - spend almost all of the
                    // time there instead of cycling through every label
                    // (including every fault label) equally.
                    long long nominal = keys.front();
                    if (keys.size() == 1)
                        return nominal;

                    double cycleOffset = phase * kCycleSeconds;
                    double cyclePos = std::fmod(t + cycleOffset, kCycleSeconds);
                    if (cyclePos < 0)
                        cyclePos += kCycleSeconds;

                    if (cyclePos < kBlipSeconds)
                    {
                        size_t cycleIndex = static_cast<size_t>((t + cycleOffset) / kCycleSeconds);
                        size_t otherCount = keys.size() - 1;
                        size_t pick = 1 + (cycleIndex % otherCount);
                        return keys[pick];
                    }
                    return nominal;
                }
            }
        }

        // InputVoltage is a physical DC-bus voltage and should never be
        // generated as a negative value in the demo. The DBC declares the
        // field signed, but real inverter bus voltage is positive.
        if (sig.name == "InputVoltage")
        {
            // Give each inverter a slightly different nominal voltage while
            // keeping it in a realistic demo range.
            double base = 330.0 + 20.0 * std::sin(t / 5.0 + phase);
            return static_cast<long long>(std::max(0.0, base));
        }

        if (sig.length <= 0)
            return 0;
        if (sig.length == 1)
        {
            // No VAL_ labels for this bit, so we don't know for certain it
            // means "fault" - but a bare status/fault bit's 0 state is the
            // overwhelmingly common case in real operation, so use the same
            // mostly-nominal shape as the labelled case above rather than a
            // 50/50 toggle every 2 seconds.
            double cyclePos = std::fmod(t + phase * kCycleSeconds, kCycleSeconds);
            if (cyclePos < 0)
                cyclePos += kCycleSeconds;
            return (cyclePos < kBlipSeconds) ? 1 : 0;
        }

        long long top = sig.isSigned ? (1LL << (sig.length - 1)) - 1 : (1LL << sig.length) - 1;
        long long cap = std::min<long long>(top, 1000); // keep values plausible, not billions
        long long lo = sig.isSigned ? -cap : 0;
        double mid = (lo + cap) / 2.0;
        double amp = (cap - lo) / 2.0;
        return static_cast<long long>(mid + amp * std::sin(t / 4 + phase));
    }
}

void simulate(CanBus &bus, DbcDatabase &db, const std::vector<ScannedMessage> &messages,
              const MsgHashTable *labels, int periodMs, double duration, std::atomic<bool> *stop)
{
    using Clock = std::chrono::steady_clock;
    auto start = Clock::now();
    std::vector<Clock::time_point> nextSend(messages.size(), start);

    while (stop == nullptr || !stop->load())
    {
        auto now = Clock::now();
        double elapsed = std::chrono::duration<double>(now - start).count();
        if (duration > 0 && elapsed > duration)
            break;

        for (size_t i = 0; i < messages.size(); ++i)
        {
            if (now < nextSend[i])
                continue;

            const DbcMessage *def = db.findMessage(messages[i].id);
            if (def == nullptr)
                continue; // shouldn't happen - collectMessageIds() and findMessage() read the same file

            CanFrame frame;
            frame.id = static_cast<uint32_t>(def->id);
            frame.isExtended = def->isExtended;
            frame.dlc = static_cast<uint8_t>(std::min(def->dlc, 8));
            std::memset(frame.data, 0, sizeof(frame.data));

            const MessageLabels *msgLabels = (labels != nullptr) ? labels->find(def->id) : nullptr;

            // A multiplexed message has one 'M' switch signal and several
            // 'mN' branches; pick one branch per cycle (rotating through
            // whichever branches actually exist) so the frame decodes
            // back to a self-consistent value instead of a garbage mix.
            std::vector<int> branches;
            for (const auto &sig : def->signals)
                if (sig.muxType == DbcSignal::Mux::Value)
                    branches.push_back(sig.muxValue);
            std::sort(branches.begin(), branches.end());
            branches.erase(std::unique(branches.begin(), branches.end()), branches.end());
            long long activeMux = branches.empty() ? 0 : branches[static_cast<size_t>(elapsed / 3) % branches.size()];

            for (size_t j = 0; j < def->signals.size(); ++j)
            {
                const DbcSignal &sig = def->signals[j];
                if (sig.muxType == DbcSignal::Mux::Value && sig.muxValue != activeMux)
                    continue; // branch not selected this cycle

                long long raw = (sig.muxType == DbcSignal::Mux::Switch)
                                    ? activeMux
                                    : rawValue(sig, elapsed, i * 0.7 + j * 0.35, msgLabels);
                packBits(frame.data, frame.dlc, sig.startBit, sig.length, sig.bigEndian, toRawBits(raw, sig.length));
            }

            bus.send(frame);
            nextSend[i] = now + std::chrono::milliseconds(periodMs);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}