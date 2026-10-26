#!/usr/bin/env python3
"""Code generated/assisted by Anthropic Claude (Claude Sonnet 5)
        Date: September 21, 2026

CAN traffic simulator: sends every message in one or more DBC files at its
cycle time, with slowly changing placeholder values.

Works on any OS by default (python-can's UDP multicast bus, which needs an active network
connection), so the monitor can be tested on a laptop with no CAN hardware.
If multicast gives "No route to host", use `python3 monitor.py --dbc <file> --demo` instead.

  Terminal 1:  python3 simulator.py --dbc can2_pdu.dbc
  Terminal 2:  python3 monitor.py --dbc can2_pdu.dbc --interface udp_multicast --channel 239.74.163.2

On a Pi with a real or virtual SocketCAN interface:
  python3 simulator.py --dbc can2_pdu.dbc --interface socketcan --channel vcan0

Install:  pip install python-can cantools msgpack
"""
import argparse
import math
import time

import can
import cantools

DEFAULT_PERIOD_MS = 100     # used when a message has no cycle time in the DBC


def raw_value(sig, t, phase):
    """Placeholder RAW value for a signal at time t (seconds)."""
    if sig.choices:                                   # cycle through the named values slowly
        keys = list(sig.choices.keys())
        return keys[int(t / 3 + phase) % len(keys)]
    if sig.length == 1:
        return int(t / 2 + phase) % 2
    top = (1 << (sig.length - 1)) - 1 if sig.is_signed else (1 << sig.length) - 1
    cap = min(top, 1000)                              # keep values plausible, not billions
    lo = -cap if sig.is_signed else 0
    mid, amp = (lo + cap) / 2, (cap - lo) / 2
    return int(mid + amp * math.sin(t / 4 + phase))


def simulate(bus, messages, duration=0, stop=None):
    """Send every message in `messages` on `bus` at its cycle time until duration/stop/Ctrl+C."""
    period = {m.frame_id: (m.cycle_time or DEFAULT_PERIOD_MS) / 1000 for m in messages}
    start = time.monotonic()
    next_send = {m.frame_id: start for m in messages}
    while not (stop is not None and stop.is_set()):
        now = time.monotonic()
        if duration and now - start > duration:
            break
        for i, m in enumerate(messages):
            if now >= next_send[m.frame_id]:
                t = now - start
                data = m.encode({s.name: raw_value(s, t, i * 0.7 + j * 0.35) for j, s in enumerate(m.signals)},
                                scaling=False, strict=False)
                bus.send(can.Message(arbitration_id=m.frame_id, is_extended_id=m.is_extended_frame, data=data))
                next_send[m.frame_id] += period[m.frame_id]
        time.sleep(0.001)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dbc", required=True, nargs="+", help="one or more DBC files to simulate")
    ap.add_argument("--interface", default="udp_multicast", help="python-can interface (default: udp_multicast)")
    ap.add_argument("--channel", default="239.74.163.2", help="channel / multicast group (default: 239.74.163.2)")
    ap.add_argument("--duration", type=float, default=0, help="stop after N seconds (default: run until Ctrl+C)")
    args = ap.parse_args()

    messages = [m for f in args.dbc for m in cantools.database.load_file(f).messages]
    with can.Bus(channel=args.channel, interface=args.interface) as bus:
        print(f"Simulating {len(messages)} messages on {args.interface}:{args.channel}. Ctrl+C to stop.")
        try:
            simulate(bus, messages, args.duration)
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    main()