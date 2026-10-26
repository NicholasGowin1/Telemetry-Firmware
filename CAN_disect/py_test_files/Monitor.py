#!/usr/bin/env python3
"""Live CAN monitor: prints every frame decoded with a DBC file.

Examples:
  python3 monitor.py --dbc can2_pdu.dbc --channel can0        # real bus (Linux / Raspberry Pi)
  python3 monitor.py --dbc can2_pdu.dbc --demo                # fake car in the same process, no hardware or network

Install:  pip install python-can cantools
"""
import argparse
import threading
from datetime import datetime


import can
import cantools




def fmt(value):

    return f"{value:.4g}" if isinstance(value, float) else str(value)

def decode_frame(db, msg):

    try:
        message = db.get_message_by_frame_id(msg.arbitration_id) #checks for ID in DBC file
    except KeyError:
        return None
    try:
        return message.name, message.decode(bytes(msg.data))
    except cantools.database.DecodeError:
        return message.name, {"<decode error>": bytes(msg.data).hex(" ")}

def stamp(msg):
    return datetime.fromtimestamp(msg.timestamp).strftime("%Y-%m-%d %H:%M:%S.%f")[:-3] #current time stamp

def format_line(msg, name, signals):
    body = "  ".join(f"{k}={fmt(v)}" for k, v in signals.items())
    return f"{stamp(msg)}  0x{msg.arbitration_id:X}  {name}  {body}"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dbc", required=True, help="DBC file for the bus you are listening to")
    ap.add_argument("--channel", default="can0", help="CAN interface, e.g. can0 (default: can0)")
    ap.add_argument("--interface", default="socketcan", help="python-can interface (default: socketcan)")
    ap.add_argument("--demo", action="store_true",
                    help="run a built-in simulator on a virtual bus (no CAN hardware or network needed)")
    args = ap.parse_args()

    db = cantools.database.load_file(args.dbc)
    unknown = set()
    stop = threading.Event()
    """ Code assisted by Anthropic Claude (Claude Sonnet 5)
        Date: September 19, 2026
    """
    if args.demo:  # Start the simulator of running car's can outputs
        import Simulator
        tx = can.Bus(channel="demo", interface="virtual")
        threading.Thread(target=Simulator.simulate, args=(tx, db.messages, 0, stop), daemon=True).start()
        args.channel, args.interface = "demo", "virtual"

    with can.Bus(channel=args.channel, interface=args.interface) as bus:
        print(f"Listening on {args.channel} with {args.dbc} ({len(db.messages)} messages). Ctrl+C to stop.")
        try:
            for msg in bus:
                result = decode_frame(db, msg)
                if result is not None:
                    print(format_line(msg, *result))
                elif msg.arbitration_id not in unknown:
                    unknown.add(msg.arbitration_id)
                    print(f"{stamp(msg)}  0x{msg.arbitration_id:X}  <not in DBC>  {bytes(msg.data).hex(' ')}")
        except KeyboardInterrupt:
            pass
        finally:
            stop.set()


if __name__ == "__main__":
    main()