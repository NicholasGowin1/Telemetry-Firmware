
// Simulator main · CPP
//  simulator_main.cpp
//  CAN traffic simulator: sends every BO_ message in a DBC file at a
//  fixed cycle time, with slowly changing placeholder values. C++
//  equivalent of Simulator.py - see TUTORIAL.md.
//
//  Examples:
//    ./simulator --dbc example.dbc --demo                        # in-process bus - just exercises simulate(), see note below
//    ./simulator --dbc example.dbc --socketcan --channel vcan0   # kernel virtual CAN (needs `ip link add vcan0 type vcan`)
//    ./simulator --dbc example.dbc --socketcan --channel can0    # real bus (Linux, e.g. Raspberry Pi)
//
//  NOTE on --demo: SharedVirtualCanBus's registry lives in this process's
//  memory, so a `simulator --demo` and a separately launched `monitor
//  --demo` are two different registries and will NOT see each other -
//  `monitor --demo` works standalone because it starts its own simulate()
//  thread inside the same process (see Monitor.cpp). To have this
//  simulator and a monitor talk to each other as two separate programs,
//  use --socketcan with a real or kernel-virtual (vcan0) interface.
//
//  Code generated/assisted by Anthropic Claude (Claude Sonnet 5)
//  Date: September 24, 2026
#include "Simulator.h"
#include "can_bus.h"
#include "dbc_parser.h"
#include "dbc_scan.h"
#include "dbc_val_parser.h"
#include "msg_hash_table.h"

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>

namespace
{
    std::atomic<bool> g_stop{false};
    void onSigint(int) { g_stop.store(true); }

    void printUsage()
    {
        std::cout << "Usage: simulator --dbc <file> (--demo | --socketcan --channel <if>) [--period-ms <n>] [--duration <sec>]\n"
                     "  --dbc <file>       DBC file to simulate (required)\n"
                     "  --demo             send on the in-process \"demo\" bus - pairs with `monitor --demo`, no root needed\n"
                     "  --socketcan        send on a real/kernel SocketCAN interface instead\n"
                     "  --channel <if>     SocketCAN interface name, e.g. can0 or vcan0 (default: can0)\n"
                     "  --period-ms <n>    cycle time for every message, in ms (default: 100)\n"
                     "  --duration <sec>   stop after N seconds (default: run until Ctrl+C)\n";
    }
}

int main(int argc, char **argv)
{
    std::string dbcPath;
    std::string channel = "can0";
    bool useSocketCan = false;
    bool demo = false;
    int periodMs = 100;
    double duration = 0;

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
        else if (arg == "--duration" && i + 1 < argc)
            duration = std::atof(argv[++i]);
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
    auto scanned = collectMessageIds(dbcPath);

    std::signal(SIGINT, onSigint);
    std::signal(SIGTERM, onSigint); // so `timeout`, systemd, or a plain `kill` stop it cleanly too

    std::unique_ptr<CanBus> bus;
    if (demo)
    {
        bus = std::make_unique<SharedVirtualCanBus>("demo");
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

    std::cout << "Simulating " << scanned.size() << " messages on "
              << (demo ? "demo" : channel) << ". Ctrl+C to stop.\n";

    simulate(*bus, db, scanned, &labels, periodMs, duration, &g_stop);
    return 0;
}
