# OpenMV Cam H7+ -> ESP32
# Pillar colours (upper ROI) and mat direction lines (bottom ROI)
# ------------------------------------------------------------------
# Protocol, one line per detection, newline terminated:
#     R,<cx>,<area>    red pillar      (upper ROI)
#     G,<cx>,<area>    green pillar    (upper ROI)
#     O,<cx>,<area>    ORANGE mat line (bottom ROI - corner marker)
#     B,<cx>,<area>    BLUE mat line   (bottom ROI - corner marker)
#     N,0,0            nothing seen
#     F,<fps>          frame rate, ~1 Hz
#
# WHY TWO REGIONS
#   Orange and red are only ~35 units apart in LAB, which is close enough
#   that lighting variation would confuse them. They are separated
#   GEOMETRICALLY instead: the mat lines are on the floor and only ever
#   appear low in the frame, the pillars are upright and appear higher.
#   Searching each colour only where it can physically be removes the
#   ambiguity entirely.
#
# WIRING: OpenMV P4 (TX) -> ESP32 GPIO16 (RX). Common ground REQUIRED.

import sensor
import time
from pyb import UART, LED

led_red, led_green, led_blue = LED(1), LED(2), LED(3)
for _l in (led_red, led_green, led_blue):
    _l.off()

uart = UART(3, 115200, timeout_char=1000)

MANUAL_GAIN_DB = 12.0
SATURATION = 3

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)            # 320 x 240

sensor.set_auto_gain(True)
sensor.set_auto_whitebal(True)
sensor.set_auto_exposure(True)
sensor.skip_frames(time=2500)

settled_gain = sensor.get_gain_db()
settled_exp = sensor.get_exposure_us()
settled_rgb = sensor.get_rgb_gain_db()
use_gain = settled_gain if MANUAL_GAIN_DB is None else MANUAL_GAIN_DB

sensor.set_auto_gain(False, gain_db=use_gain)
sensor.set_auto_exposure(False, exposure_us=int(settled_exp))
sensor.set_auto_whitebal(False, rgb_gain_db=settled_rgb)

for _name, _fn, _v in (("saturation", sensor.set_saturation, SATURATION),
                       ("contrast", sensor.set_contrast, 1),
                       ("brightness", sensor.set_brightness, 1)):
    try:
        _fn(_v)
    except Exception:
        print("%s not supported" % _name)

sensor.skip_frames(time=500)
print("gain %.1fdB exp %dus" % (sensor.get_gain_db(), sensor.get_exposure_us()))

# ------------------------------------------------------------------
# LAB thresholds  (L min, L max, A min, A max, B min, B max)
#
# Pillars, from the rules: red RGB(238,39,55), green RGB(68,214,44)
# Lines, from rules 13.9:  orange CMYK(0,60,100,0) -> RGB(255,102,0)
#                          blue   CMYK(100,80,0,0) -> RGB(0,51,255)
#
# Measured LAB:  orange L62 A55 B71      blue L37 A65 B-100
#                red    L52 A72 B43      green L76 A-67 B66
#
# Orange and red overlap on A; they are kept apart by ROI, not threshold.
# Blue is unmistakable - B is strongly negative and nothing else is.
# ------------------------------------------------------------------
RED_THRESHOLD    = (12,  75,   30, 127,    8,  90)
GREEN_THRESHOLD  = (20, 100, -100, -20,   15, 110)
ORANGE_THRESHOLD = (34,  92,   33, 100,   41, 116)
BLUE_THRESHOLD   = (15,  82,   30, 100, -128, -70)

# Pillars live in the upper part of the frame
PILLAR_ROI = (0, 40, 320, 150)

# Mat lines only count in the BOTTOM 20% of the frame. A line higher up is
# still far ahead; down here it is under the robot, which is the moment a
# corner is actually being crossed.
LINE_ROI_TOP = int(240 * 0.80)               # y = 192
LINE_ROI = (0, LINE_ROI_TOP, 320, 240 - LINE_ROI_TOP)

MIN_PILLAR_PX = 300
MIN_LINE_PX   = 200

SEND_INTERVAL_MS = 50                        # ~20 Hz
FPS_INTERVAL_MS  = 1000

clock = time.clock()
last_send = time.ticks_ms()
last_fps = time.ticks_ms()
heartbeat = 0


def val(attr):
    """Blob accessors are properties on newer firmware, methods on older."""
    return attr() if callable(attr) else attr


def best_blob(img, threshold, roi, min_px):
    """Largest blob of a colour inside a region, or (None, 0, 0)."""
    blobs = img.find_blobs([threshold], roi=roi,
                           pixels_threshold=100, area_threshold=100,
                           merge=True, margin=10)
    best = None
    best_px = 0
    for b in blobs:
        px = val(b.pixels)
        if px > best_px:
            best, best_px = b, px
    if best is None or best_px < min_px:
        return None, 0, 0
    return best, best_px, int(val(best.cx))


try:
  while True:
    clock.tick()
    img = sensor.snapshot()

    # ---- mat lines, bottom strip only ----
    o_blob, o_px, o_cx = best_blob(img, ORANGE_THRESHOLD, LINE_ROI, MIN_LINE_PX)
    b_blob, b_px, b_cx = best_blob(img, BLUE_THRESHOLD,   LINE_ROI, MIN_LINE_PX)

    line_col, line_cx, line_px = None, 0, 0
    if o_px or b_px:
        if o_px >= b_px:
            line_col, line_cx, line_px = "O", o_cx, o_px
        else:
            line_col, line_cx, line_px = "B", b_cx, b_px

    # ---- pillars, upper region ----
    r_blob, r_px, r_cx = best_blob(img, RED_THRESHOLD,   PILLAR_ROI, MIN_PILLAR_PX)
    g_blob, g_px, g_cx = best_blob(img, GREEN_THRESHOLD, PILLAR_ROI, MIN_PILLAR_PX)

    pil_col, pil_cx, pil_px = None, 0, 0
    if r_px or g_px:
        if r_px >= g_px:
            pil_col, pil_cx, pil_px = "R", r_cx, r_px
        else:
            pil_col, pil_cx, pil_px = "G", g_cx, g_px

    # ---- overlay, cosmetic only, must never crash the loop ----
    try:
        img.draw_rectangle(LINE_ROI, color=(90, 90, 90))
        img.draw_rectangle(PILLAR_ROI, color=(50, 50, 50))
        if line_col:
            c = (255, 128, 0) if line_col == "O" else (0, 80, 255)
            img.draw_cross(line_cx, LINE_ROI_TOP + 20, color=c, size=15)
            img.draw_string(4, LINE_ROI_TOP + 2,
                            "%s %d" % (line_col, line_px), color=c)
        if pil_col:
            c = (255, 0, 0) if pil_col == "R" else (0, 255, 0)
            img.draw_cross(pil_cx, 110, color=c, size=15)
            img.draw_string(4, 44, "%s %d" % (pil_col, pil_px), color=c)
    except Exception:
        pass

    # ---- status LEDs ----
    led_red.off()
    led_green.off()
    if pil_col == "R":
        led_red.on()
    elif pil_col == "G":
        led_green.on()
    if line_col:
        led_blue.on()
    else:
        heartbeat = (heartbeat + 1) % 15
        if heartbeat == 0:
            led_blue.toggle()

    # ---- transmit ----
    now = time.ticks_ms()
    if time.ticks_diff(now, last_send) >= SEND_INTERVAL_MS:
        last_send = now
        sent = False
        # Lines first - they are the corner trigger and matter most
        if line_col:
            uart.write("%s,%d,%d\n" % (line_col, line_cx, line_px))
            sent = True
        if pil_col:
            uart.write("%s,%d,%d\n" % (pil_col, pil_cx, pil_px))
            sent = True
        if not sent:
            uart.write("N,0,0\n")

    if time.ticks_diff(now, last_fps) >= FPS_INTERVAL_MS:
        last_fps = now
        uart.write("F,%.1f\n" % clock.fps())

    # Uncomment to debug in the IDE. Costs fps - leave off on the robot.
    # print(line_col, pil_col, clock.fps())

except Exception as e:
    try:
        uart.write("N,0,0\n")
    except Exception:
        pass
    led_green.off()
    led_blue.off()
    while True:
        led_red.toggle()
        time.sleep_ms(120)
