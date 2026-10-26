// Build + run walkthrough, macOS
// --------------------------------
// Folder layout assumed:
//   Can_disect/CAN_Tools/   - this project (CMakeLists.txt, Monitor.cpp, etc.)
//   Can_disect/dbc/         - .dbc files
//   Dash/                   - the Python dashboard (dashboard_mac.py)
//
// 1. Build the C++ tools (from CAN_Tools):
//      cd Can_disect/CAN_Tools
//      cmake -B build
//      cmake --build build
//
// 2. Confirm what DBC files actually exist (filenames are easy to get wrong):
//      ls ../dbc
//
// 3. Sanity-check monitor by itself before involving the dashboard.
//    Swap <your.dbc> for whatever step 2 showed:
//      ./build/monitor --dbc ../dbc/<your.dbc> --demo --json
//    Expect scrolling JSON lines, one per frame. Ctrl+C to stop once confirmed.
// 3.5. If you want to view the can strictly in the terminal in a slightly more readable format.
//      ./build/monitor --dbc ../dbc/<your.dbc> --demo
//      Expect scrolling lines of the following <date/time> <CANID> <Source of message> <Name of Message>...
//
// 4. Run the dashboard (from Dash):
//      cd ../../Dash
//      python3 dashboard_mac.py   --monitor-bin ../Can_disect/CAN_Tools/build/monitor   --dbc1 ../Can_disect/dbc/can1_inverters.dbc   --dbc2 ../Can_disect/dbc/can2_pdu.dbc   --demo
//
// Notes:
//   - --demo needs no hardware and no root: monitor starts its own
//     in-process simulator thread and feeds itself fake traffic.
//   - Add --fullscreen to the dashboard command to fill the NHD screen
//     instead of opening in a window.
//   - If gauges don't move, check the terminal running the dashboard for
//     [monitor] / [dashboard] lines - errors from the subprocess surface
//     there, not on the screen itself.
// Code generated/assisted by Anthropic Claude (Claude Sonnet 5)
// Date: September 26, 2026 (added --json output mode)
#include "Simulator.h"
#include "can_bus.h"
#include "dbc_parser.h"
#include "dbc_scan.h"
#include "dbc_val_parser.h"
#include "msg_hash_table.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <iostream>
#include <memory>
#include <set>
#include <thread>

namespace
{
    std::atomic<bool> g_stop{false};
    void onSigint(int) { g_stop.store(true); }

    std::string timestamp()
    {
        using namespace std::chrono;
        auto now = system_clock::now();
        auto ms = duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000;
        std::time_t t = system_clock::to_time_t(now);
        std::tm tmv{};
        localtime_r(&t, &tmv);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmv);
        char out[40];
        std::snprintf(out, sizeof(out), "%s.%03lld", buf, static_cast<long long>(ms));
        return out;
    }

    // Same rendering as Python's fmt(): compact, 4 significant digits.
    std::string fmtValue(double v)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.4g", v);
        return buf;
    }

    // Minimal JSON string escaping - just enough for signal/message names
    // and VAL_ labels, which are ordinary DBC identifiers/text and won't
    // normally contain anything exotic, but this keeps a stray quote or
    // backslash from producing invalid JSON.
    std::string jsonEscape(const std::string &s)
    {
        std::string out;
        out.reserve(s.size());
        for (char c : s)
        {
            switch (c)
            {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            default:
                out += c;
            }
        }
        return out;
    }

    // Looks up a VAL_ label for this decoded value. Note: DbcDatabase::decode()
    // only returns the physical (factor/offset-applied) value, not the raw
    // one VAL_ tables are keyed by, so this only matches correctly for
    // enum-style signals with factor 1 / offset 0 - the overwhelming common
    // case, but not guaranteed in general.
    bool lookupLabel(const MessageLabels *msgLabels, const std::string &signalName, double value, std::string &label)
    {
        if (msgLabels == nullptr)
            return false;
        for (const auto &s : msgLabels->signals)
        {
            if (s.signalName != signalName)
                continue;
            auto it = s.labels.find(static_cast<long long>(value));
            if (it == s.labels.end())
                return false;
            label = it->second;
            return true;
        }
        return false;
    }

    void printUsage()
    {
        std::cout << "Usage: monitor --dbc <file> (--demo | --socketcan --channel <if>) [--period-ms <n>] [--json]\n"
                     "  --dbc <file>       DBC file for the bus you are listening to (required)\n"
                     "  --demo             also start an in-process simulator feeding this monitor - no root needed\n"
                     "  --socketcan        listen on a real/kernel SocketCAN interface instead\n"
                     "  --channel <if>     SocketCAN interface name, e.g. can0 or vcan0 (default: can0)\n"
                     "  --period-ms <n>    simulator cycle time, only used with --demo (default: 100)\n"
                     "  --json             emit one JSON object per line instead of the human-readable format,\n"
                     "                     e.g. for piping into another program (a dashboard, a logger, ...)\n";
    }
}

int main(int argc, char **argv)
{
    std::string dbcPath;
    std::string channel = "can0";
    bool useSocketCan = false;
    bool demo = false;
    int periodMs = 100;
    bool jsonMode = false;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--dbc" && i + 1 < argc)
            dbcPath = argv[++i];
        else if (arg == "--channel" && i + 1 < argc)
            channel = argv[++i];
        else if (arg == "--socketcan")
            useSocketCan = true;
        else if (arg == "--demo")
            demo = true;
        else if (arg == "--period-ms" && i + 1 < argc)
            periodMs = std::atoi(argv[++i]);
        else if (arg == "--json")
            jsonMode = true;
        else
        {
            printUsage();
            return (arg == "--help" || arg == "-h") ? 0 : 1;
        }
    }

    if (dbcPath.empty() || (!demo && !useSocketCan))
    {
        printUsage();
        return 1;
    }

    DbcDatabase db;
    if (!db.load(dbcPath))
    {
        std::cerr << "Failed to load DBC file: " << dbcPath << "\n";
        return 1;
    }

    std::vector<int> keys;
    auto valRecords = readValRecords(dbcPath, keys);
    MsgHashTable labels = buildTable(valRecords, keys);

    std::signal(SIGINT, onSigint);
    std::signal(SIGTERM, onSigint); // so `timeout`, systemd, or a plain `kill` stop it cleanly too

    std::unique_ptr<CanBus> bus;
    std::thread simThread;

    if (demo)
    {
        auto scanned = collectMessageIds(dbcPath);
        bus = std::make_unique<SharedVirtualCanBus>("demo");
        auto simBus = std::make_shared<SharedVirtualCanBus>("demo");
        simThread = std::thread([simBus, &db, scanned, &labels, periodMs]
                                { simulate(*simBus, db, scanned, &labels, periodMs, 0.0, &g_stop); });
    }
    else
    {
        auto scb = std::make_unique<SocketCanBus>(channel);
        if (!scb->isOpen())
        {
            std::cerr << "Could not open SocketCAN interface '" << channel
                      << "'. Is it up? (sudo ip link set " << channel << " up)\n";
            return 1;
        }
        bus = std::move(scb);
    }

    (jsonMode ? std::cerr : std::cout)
        << "Listening on " << (demo ? "demo" : channel) << " with " << dbcPath
        << " (" << db.messageCount() << " messages). Ctrl+C to stop.\n";

    std::set<uint32_t> unknown;
    while (!g_stop.load())
    {
        CanFrame frame;
        if (!bus->receive(frame, 200)) // timeout lets us re-check g_stop periodically
            continue;

        const DbcMessage *def = db.findMessage(static_cast<int>(frame.id));
        if (def == nullptr)
        {
            if (!jsonMode && unknown.insert(frame.id).second)
            {
                std::cout << timestamp() << "  0x" << std::hex << frame.id << std::dec
                          << "  <not in DBC>\n";
            }
            // Unknown-id frames are skipped entirely in --json mode: a
            // consumer only cares about frames it can put a name to.
            continue;
        }

        auto decoded = DbcDatabase::decode(*def, frame.data, frame.dlc);
        const MessageLabels *msgLabels = labels.find(def->id);

        if (jsonMode)
        {
            // One line, one frame:
            //   {"ts":"...","id":401,"name":"BMS_Status","signals":{"PackSOC":87,"Fault":"OK"}}
            // Enum signals (a matching VAL_ label) come through as JSON
            // strings; everything else as a JSON number. A consumer that
            // only wants raw numbers can special-case the string ones.
            std::cout << "{\"ts\":\"" << jsonEscape(timestamp()) << "\","
                      << "\"id\":" << def->id << ","
                      << "\"name\":\"" << jsonEscape(def->name) << "\","
                      << "\"signals\":{";
            for (size_t i = 0; i < decoded.size(); ++i)
            {
                const auto &d = decoded[i];
                std::string label;
                std::cout << "\"" << jsonEscape(d.name) << "\":";
                if (lookupLabel(msgLabels, d.name, d.value, label))
                    std::cout << "\"" << jsonEscape(label) << "\"";
                else
                    std::cout << fmtValue(d.value);
                if (i + 1 < decoded.size())
                    std::cout << ",";
            }
            std::cout << "}}\n";
        }
        else
        {
            std::cout << timestamp() << "  0x" << std::hex << frame.id << std::dec
                      << "  " << def->name << "  ";
            for (size_t i = 0; i < decoded.size(); ++i)
            {
                const auto &d = decoded[i];
                std::string label;
                std::cout << d.name << "=";
                if (lookupLabel(msgLabels, d.name, d.value, label))
                    std::cout << label;
                else
                    std::cout << fmtValue(d.value);
                if (!d.unit.empty())
                    std::cout << d.unit;
                if (i + 1 < decoded.size())
                    std::cout << "  ";
            }
            std::cout << "\n";
        }
    }

    g_stop.store(true);
    if (simThread.joinable())
        simThread.join();
    return 0;
}