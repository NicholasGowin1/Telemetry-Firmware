#!/usr/bin/env python3
import sys
import json
import math
import time
import argparse
import threading
import subprocess
from dataclasses import dataclass, field

import pygame

SCREEN_W, SCREEN_H = 800, 480
FPS = 30
CAN_TIMEOUT_S = 0.500  # 500 ms

BLACK = (10, 10, 10)
WHITE = (240, 240, 240)
GREEN = (60, 220, 90)
YELLOW = (240, 200, 40)
AMBER = (255, 150, 0)
RED = (220, 50, 50)
GREY = (60, 60, 60)
LIGHT_OFF = (50, 50, 50)
TILE_BG = (22, 22, 22)

# ---- Speed calculation (SET THESE FOR YOUR CAR, or use the CLI flags) ----
SPEED_CFG = {
    "pole_pairs": 5.0,          # motor pole pairs (ERPM = mechanical RPM * pole pairs)
    "gear_ratio": 4.0,          # motor revs per wheel rev
    "wheel_diameter_in": 18.0,  # rolling diameter of the tire, inches
}

# ---- Warning light thresholds ----
TEMP_WARN_C = 80.0
TEMP_FAULT_C = 100.0
LIGHT_HOLD_S = 2.0  # keep a light on this long after a fault clears so blips aren't missed

# Warning light severity
OFF, WARN, FAULT = 0, 1, 2

LIGHT_ORDER = ["powertrain", "battery", "temp", "pdu", "can"]
LIGHT_LABELS = {
    "powertrain": "POWERTRAIN",
    "battery": "BATTERY",
    "temp": "TEMP",
    "pdu": "PDU",
    "can": "CAN",
}

# Inverter FaultCode table (matches the DBC VAL_ table) so numeric and
# string faults are handled identically.
INVERTER_FAULTS = {
    0: "NO FAULTS",
    1: "Overvoltage",
    2: "Undervoltage",
    3: "DRV",
    4: "ABS Overcurrent",
    5: "CTLR Overtemp",
    6: "Motor Overtemp",
    7: "Sensor wire fault",
    8: "Sensor general fault",
    9: "CAN Command error",
    10: "Analog input error",
}

# Which light each named inverter fault turns on. Anything not listed
# here falls through to the powertrain light.
INVERTER_FAULT_LIGHT = {
    "overvoltage": "battery",
    "undervoltage": "battery",
    "ctlr overtemp": "temp",
    "motor overtemp": "temp",
}

PDU_FAULTS = {
    0: "OK",
    1: "Open Circuit",
    2: "Current Limit Exceeded",
    4: "Short Circuit",
}

# Values that mean "no fault". NOTE: the inverter DBC calls code 0 "NO FAULTS".
CLEAR_NAMES = {"", "0", "0.0", "OK", "NO FAULTS", "NONE", "NORMAL"}


@dataclass
class Telemetry:
    erpm: list[float] = field(default_factory=lambda: [0.0] * 4)
    inverter_voltage: list[float] = field(default_factory=lambda: [0.0] * 4)
    motor_temp: list[float] = field(default_factory=lambda: [0.0] * 4)
    controller_temp: list[float] = field(default_factory=lambda: [0.0] * 4)
    fault_code: list[object] = field(default_factory=lambda: [0] * 4)

    pdu_current: list[float] = field(default_factory=lambda: [0.0] * 8)
    pdu_fault: list[object] = field(default_factory=lambda: ["OK"] * 8)

    # Last CAN frame received for each inverter / PDU channel.
    inverter_last_rx: list[float] = field(default_factory=lambda: [0.0] * 4)
    pdu_last_rx: list[float] = field(default_factory=lambda: [0.0] * 8)


telemetry = Telemetry()
telemetry_lock = threading.Lock()

INVERTERS = {
    "INV_FL": 0,
    "INV_FR": 1,
    "INV_RL": 2,
    "INV_RR": 3,
}

PDU_CURRENT = [f"PDMMeasuredCurrent{i:02d}" for i in range(1, 9)]
PDU_FAULT = [f"PDMErrorStatus{i:02d}" for i in range(1, 9)]


def update_from_frame(obj, source_name):
    """Update telemetry. A bad signal in one frame must never kill the CAN reader."""
    signals = obj.get("signals", {})
    message_name = obj.get("name", "")
    now = time.monotonic()

    with telemetry_lock:
        if source_name == "inverter":
            for prefix, i in INVERTERS.items():
                if not message_name.startswith(prefix + "_"):
                    continue

                # Any frame from this inverter proves that its CAN link is alive.
                telemetry.inverter_last_rx[i] = now

                if "ERPM" in signals:
                    telemetry.erpm[i] = float(signals["ERPM"])
                if "InputVoltage" in signals:
                    telemetry.inverter_voltage[i] = float(signals["InputVoltage"])
                if "MotorTemp" in signals:
                    telemetry.motor_temp[i] = float(signals["MotorTemp"])
                if "ControllerTemp" in signals:
                    telemetry.controller_temp[i] = float(signals["ControllerTemp"])

                # FaultCode may be numeric OR a DBC VAL_ string such as
                # "Undervoltage". Never cast a string fault to float.
                if "FaultCode" in signals:
                    telemetry.fault_code[i] = signals["FaultCode"]
                break

        elif source_name == "pdu":
            for i, name in enumerate(PDU_CURRENT):
                if name in signals:
                    telemetry.pdu_current[i] = float(signals[name])
                    telemetry.pdu_last_rx[i] = now

            for i, name in enumerate(PDU_FAULT):
                if name in signals:
                    telemetry.pdu_fault[i] = signals[name]
                    telemetry.pdu_last_rx[i] = now


def monitor_bridge_thread(monitor_bin, dbc, demo, channel, source_name):
    cmd = [monitor_bin, "--dbc", dbc, "--json"]
    if demo:
        cmd += ["--demo"]
    else:
        cmd += ["--socketcan", "--channel", channel]

    print(f"[dashboard] starting {source_name}: {' '.join(cmd)}", file=sys.stderr)

    try:
        proc = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
        )
    except FileNotFoundError:
        print(f"[dashboard] couldn't run '{monitor_bin}'", file=sys.stderr)
        return

    def relay_stderr():
        for line in proc.stderr:
            print(f"[{source_name}] {line.rstrip()}", file=sys.stderr)

    threading.Thread(target=relay_stderr, daemon=True).start()

    try:
        for line in proc.stdout:
            line = line.strip()
            if not line:
                continue
            try:
                obj = json.loads(line)
                update_from_frame(obj, source_name)
            except (json.JSONDecodeError, TypeError, ValueError) as exc:
                # One malformed frame must not stop all telemetry.
                print(f"[{source_name}] ignored frame: {exc}", file=sys.stderr)
    finally:
        proc.kill()


def inverter_alive(data, i, now):
    return (now - data.inverter_last_rx[i]) <= CAN_TIMEOUT_S


def pdu_alive(data, i, now):
    return (now - data.pdu_last_rx[i]) <= CAN_TIMEOUT_S


# --------------------------------------------------------------------------
# Speed
# --------------------------------------------------------------------------
def erpm_to_mph(erpm):
    wheel_rpm = abs(erpm) / SPEED_CFG["pole_pairs"] / SPEED_CFG["gear_ratio"]
    circumference_in = math.pi * SPEED_CFG["wheel_diameter_in"]
    inches_per_min = wheel_rpm * circumference_in
    return inches_per_min * 60.0 / 63360.0  # 63360 inches per mile


# --------------------------------------------------------------------------
# Fault evaluation
# --------------------------------------------------------------------------
def fault_name(code, table):
    """Turn a numeric or string fault into a readable name."""
    if isinstance(code, (int, float)) and not isinstance(code, bool):
        return table.get(int(code), f"Code {int(code)}")
    s = str(code).strip()
    try:
        return table.get(int(float(s)), f"Code {s}")
    except ValueError:
        return s


def fault_is_clear(name):
    return name.strip().upper() in CLEAR_NAMES


def evaluate_lights(data, now):
    """Return {light: (level, [reasons])} for the current telemetry snapshot."""
    lights = {k: [OFF, []] for k in LIGHT_ORDER}

    def raise_light(light, level, reason):
        entry = lights[light]
        entry[0] = max(entry[0], level)
        entry[1].append(reason)

    labels = ["FL", "FR", "RL", "RR"]
    for i in range(4):
        if not inverter_alive(data, i, now):
            raise_light("can", FAULT, f"{labels[i]} inverter CAN timeout")
            continue  # stale data can't be trusted for anything else

        # Inverter-reported fault code.
        name = fault_name(data.fault_code[i], INVERTER_FAULTS)
        if not fault_is_clear(name):
            light = INVERTER_FAULT_LIGHT.get(name.lower(), "powertrain")
            raise_light(light, FAULT, f"{labels[i]} {name}")

        # Temperature: warn before the inverter itself trips.
        hottest = max(data.motor_temp[i], data.controller_temp[i])
        if hottest >= TEMP_FAULT_C:
            raise_light("temp", FAULT, f"{labels[i]} {hottest:.0f}C")
        elif hottest >= TEMP_WARN_C:
            raise_light("temp", WARN, f"{labels[i]} {hottest:.0f}C")

    # PDU
    stale = [i + 1 for i in range(8) if not pdu_alive(data, i, now)]
    if stale:
        raise_light("can", FAULT, "PDU CAN timeout")

    for i in range(8):
        if not pdu_alive(data, i, now):
            continue
        name = fault_name(data.pdu_fault[i], PDU_FAULTS)
        if fault_is_clear(name):
            continue
        level = WARN if name.lower() == "open circuit" else FAULT
        raise_light("pdu", level, f"PDU CH{i + 1} {name}")

    return {k: (v[0], v[1]) for k, v in lights.items()}


def apply_hold(raw, hold, now):
    """Latch each light on for LIGHT_HOLD_S after its fault clears."""
    out = {}
    for key, (level, reasons) in raw.items():
        if level > OFF:
            hold[key] = (level, now, reasons)
            out[key] = (level, reasons)
            continue
        prev = hold.get(key)
        if prev and (now - prev[1]) < LIGHT_HOLD_S:
            out[key] = (prev[0], prev[2])
        else:
            out[key] = (OFF, [])
    return out


# --------------------------------------------------------------------------
# Warning light icons
# --------------------------------------------------------------------------
def icon_gear(s, cx, cy, r, color):
    teeth = 8
    pts = []
    for k in range(teeth * 4):
        ang = 2 * math.pi * k / (teeth * 4)
        rad = r if (k // 2) % 2 == 0 else r * 0.72
        pts.append((cx + rad * math.cos(ang), cy + rad * math.sin(ang)))
    pygame.draw.polygon(s, color, pts)
    pygame.draw.circle(s, TILE_BG, (cx, cy), int(r * 0.35))


def icon_battery(s, cx, cy, r, color):
    body = pygame.Rect(0, 0, int(r * 2), int(r * 1.3))
    body.center = (cx - 2, cy)
    pygame.draw.rect(s, color, body, 3, border_radius=3)
    nub = pygame.Rect(body.right, cy - int(r * 0.25), 4, int(r * 0.5))
    pygame.draw.rect(s, color, nub)
    # + on the left, - on the right
    px, py = body.left + int(r * 0.5), cy
    pygame.draw.line(s, color, (px - 5, py), (px + 5, py), 3)
    pygame.draw.line(s, color, (px, py - 5), (px, py + 5), 3)
    mx = body.right - int(r * 0.5)
    pygame.draw.line(s, color, (mx - 5, cy), (mx + 5, cy), 3)


def icon_thermometer(s, cx, cy, r, color):
    stem = pygame.Rect(0, 0, int(r * 0.45), int(r * 1.6))
    stem.center = (cx - 3, cy - int(r * 0.2))
    pygame.draw.rect(s, color, stem, border_radius=5)
    pygame.draw.circle(s, color, (cx - 3, cy + int(r * 0.65)), int(r * 0.45))
    for k in range(3):
        y = stem.top + 4 + k * int(r * 0.4)
        pygame.draw.line(s, color, (cx + int(r * 0.35), y), (cx + int(r * 0.7), y), 3)


def icon_bolt(s, cx, cy, r, color):
    rel = [(0.15, -1.0), (-0.55, 0.1), (-0.05, 0.1), (-0.25, 1.0), (0.55, -0.2), (0.05, -0.2)]
    pts = [(cx + x * r, cy + y * r) for x, y in rel]
    pygame.draw.polygon(s, color, pts)


def icon_can(s, cx, cy, r, color, font):
    box = pygame.Rect(0, 0, int(r * 2.1), int(r * 1.3))
    box.center = (cx, cy)
    pygame.draw.rect(s, color, box, 3, border_radius=4)
    txt = font.render("CAN", True, color)
    s.blit(txt, txt.get_rect(center=box.center))


ICONS = {
    "powertrain": icon_gear,
    "battery": icon_battery,
    "temp": icon_thermometer,
    "pdu": icon_bolt,
}


def draw_light(screen, font_small, key, cx, level):
    color = LIGHT_OFF if level == OFF else (AMBER if level == WARN else RED)
    tile = pygame.Rect(0, 0, 60, 60)
    tile.center = (cx, 38)
    pygame.draw.rect(screen, TILE_BG, tile, border_radius=10)
    if level != OFF:
        pygame.draw.rect(screen, color, tile, 2, border_radius=10)

    if key == "can":
        icon_can(screen, tile.centerx, tile.centery, 17, color, font_small)
    else:
        ICONS[key](screen, tile.centerx, tile.centery, 17, color)

    label = font_small.render(LIGHT_LABELS[key], True, color if level else GREY)
    screen.blit(label, label.get_rect(center=(cx, 82)))


# --------------------------------------------------------------------------
# Main draw
# --------------------------------------------------------------------------
def draw_dashboard(screen, fonts, data, blink_on, hold):
    screen.fill(BLACK)
    font_huge, font_med, font_small = fonts
    now = time.monotonic()

    # ---- Warning lights (top row) ----
    lights = apply_hold(evaluate_lights(data, now), hold, now)
    spacing = SCREEN_W // len(LIGHT_ORDER)
    for n, key in enumerate(LIGHT_ORDER):
        draw_light(screen, font_small, key, spacing // 2 + n * spacing, lights[key][0])

    # ---- Speed (from live inverters only) ----
    live_erpm = [
        abs(data.erpm[i])
        for i in range(4)
        if inverter_alive(data, i, now)
    ]

    if live_erpm:
        avg_erpm = sum(live_erpm) / len(live_erpm)
        mph = erpm_to_mph(avg_erpm)

        speed = font_huge.render(f"{mph:.0f}", True, WHITE)
        screen.blit(speed, speed.get_rect(center=(SCREEN_W // 2, 145)))
        mph_lbl = font_small.render("MPH", True, GREY)
        screen.blit(mph_lbl, mph_lbl.get_rect(center=(SCREEN_W // 2, 200)))

        # Average ERPM, demoted to a small readout on the right.
        screen.blit(font_small.render("AVG ERPM", True, GREY), (610, 130))
        screen.blit(font_small.render(f"{avg_erpm:.0f}", True, WHITE), (610, 152))
    else:
        dash = font_huge.render("---", True, RED)
        screen.blit(dash, dash.get_rect(center=(SCREEN_W // 2, 145)))
        msg = font_small.render("NO INVERTER CAN DATA", True, RED)
        screen.blit(msg, msg.get_rect(center=(SCREEN_W // 2, 200)))

    # ---- Per-inverter readouts ----
    labels = ["FL", "FR", "RL", "RR"]
    for i, label in enumerate(labels):
        x = 25 + i * 195
        alive = inverter_alive(data, i, now)

        screen.blit(font_small.render(label, True, GREY), (x, 222))

        if alive:
            temp = data.motor_temp[i]
            temp_color = RED if temp >= TEMP_FAULT_C else (YELLOW if temp >= TEMP_WARN_C else WHITE)
            screen.blit(font_med.render(f"{temp:.0f} C", True, temp_color), (x, 244))
            screen.blit(
                font_small.render(f"{abs(data.erpm[i]):.0f} RPM", True, WHITE),
                (x, 276),
            )
            screen.blit(
                font_small.render(f"{data.inverter_voltage[i]:.0f} V", True, WHITE),
                (x, 298),
            )

            name = fault_name(data.fault_code[i], INVERTER_FAULTS)
            if not fault_is_clear(name):
                screen.blit(font_small.render(name[:16], True, RED), (x, 320))
        else:
            screen.blit(font_med.render("STALE", True, RED), (x, 248))
            screen.blit(font_small.render(">500 ms", True, RED), (x, 284))

    # ---- PDU currents ----
    screen.blit(font_small.render("PDU CURRENT", True, WHITE), (25, 355))
    for i, current in enumerate(data.pdu_current):
        col = i % 4
        row = i // 4
        x = 25 + col * 195
        y = 378 + row * 26

        if pdu_alive(data, i, now):
            txt = f"CH{i + 1}: {current:5.1f} A"
            color = WHITE
        else:
            txt = f"CH{i + 1}: STALE"
            color = RED

        screen.blit(font_small.render(txt, True, color), (x, y))

    # ---- Fault message strip (bottom) ----
    fault_msgs = []
    warn_msgs = []
    for key in LIGHT_ORDER:
        level, reasons = lights[key]
        if level == FAULT:
            fault_msgs += reasons
        elif level == WARN:
            warn_msgs += reasons

    if fault_msgs and blink_on:
        text, color = " | ".join(fault_msgs), RED
    elif warn_msgs and not fault_msgs:
        text, color = " | ".join(warn_msgs), AMBER
    else:
        text, color = "", RED

    if text:
        f = font_small.render(text[:70], True, color)
        screen.blit(f, f.get_rect(center=(SCREEN_W // 2, 458)))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--monitor-bin", default="./monitor")
    parser.add_argument("--dbc1", required=True, help="inverter DBC")
    parser.add_argument("--dbc2", required=True, help="PDU DBC")
    parser.add_argument("--demo", action="store_true")
    parser.add_argument("--socketcan", action="store_true")
    parser.add_argument("--channel1", default="can0")
    parser.add_argument("--channel2", default="can1")
    parser.add_argument("--fullscreen", action="store_true")
    parser.add_argument("--pole-pairs", type=float, default=SPEED_CFG["pole_pairs"],
                        help="motor pole pairs (ERPM / pole pairs = mechanical RPM)")
    parser.add_argument("--gear-ratio", type=float, default=SPEED_CFG["gear_ratio"],
                        help="motor revolutions per wheel revolution")
    parser.add_argument("--wheel-diameter-in", type=float, default=SPEED_CFG["wheel_diameter_in"],
                        help="tire rolling diameter in inches")
    args = parser.parse_args()

    if args.demo == args.socketcan:
        parser.error("choose exactly one of --demo or --socketcan")

    SPEED_CFG["pole_pairs"] = args.pole_pairs
    SPEED_CFG["gear_ratio"] = args.gear_ratio
    SPEED_CFG["wheel_diameter_in"] = args.wheel_diameter_in
    print(f"[dashboard] speed config: {SPEED_CFG}", file=sys.stderr)

    pygame.init()
    flags = pygame.FULLSCREEN if args.fullscreen else 0
    screen = pygame.display.set_mode((SCREEN_W, SCREEN_H), flags)
    pygame.display.set_caption("Formula SAE EV Dashboard")

    # Comma-separated fallbacks: works on macOS (menlo) and Linux/Pi (DejaVu).
    mono = "menlo,dejavusansmono,liberationmono,couriernew"
    fonts = (
        pygame.font.SysFont(mono, 96, bold=True),
        pygame.font.SysFont(mono, 25),
        pygame.font.SysFont(mono, 17),
    )

    t1 = threading.Thread(
        target=monitor_bridge_thread,
        args=(args.monitor_bin, args.dbc1, args.demo, args.channel1, "inverter"),
        daemon=True,
    )
    t2 = threading.Thread(
        target=monitor_bridge_thread,
        args=(args.monitor_bin, args.dbc2, args.demo, args.channel2, "pdu"),
        daemon=True,
    )
    t1.start()
    t2.start()

    clock = pygame.time.Clock()
    blink_timer = 0.0
    blink_on = True
    light_hold = {}
    running = True

    while running:
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
            elif event.type == pygame.KEYDOWN and event.key == pygame.K_ESCAPE:
                running = False

        dt = clock.tick(FPS) / 1000.0
        blink_timer += dt
        if blink_timer >= 0.4:
            blink_timer = 0.0
            blink_on = not blink_on

        with telemetry_lock:
            data = Telemetry(
                erpm=list(telemetry.erpm),
                inverter_voltage=list(telemetry.inverter_voltage),
                motor_temp=list(telemetry.motor_temp),
                controller_temp=list(telemetry.controller_temp),
                fault_code=list(telemetry.fault_code),
                pdu_current=list(telemetry.pdu_current),
                pdu_fault=list(telemetry.pdu_fault),
                inverter_last_rx=list(telemetry.inverter_last_rx),
                pdu_last_rx=list(telemetry.pdu_last_rx),
            )

        draw_dashboard(screen, fonts, data, blink_on, light_hold)
        pygame.display.flip()

    pygame.quit()


if __name__ == "__main__":
    main()