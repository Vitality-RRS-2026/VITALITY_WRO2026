# OpenMV Cam H7+ -> ESP32 : dominant colour detection
# ---------------------------------------------------
# Finds which pillar colour occupies the most of the frame and tells the
# ESP32 about it. The ESP32 then steers.
#
# Protocol, newline terminated:
#     R,<cx>,<total_px>     red is dominant    -> ESP32 steers RIGHT
#     G,<cx>,<total_px>     green is dominant  -> ESP32 steers LEFT
#     M,<cx>,<total_px>     magenta (parking marker) - no steering
#     N,0,0                 nothing above threshold
#     F,<fps>               frame rate, about once a second
#
# WHY TWO NUMBERS
#   total_px decides WHICH colour wins (how much of that colour is on
#   screen). cx is the centre of the BIGGEST single blob of that colour,
#   which is the nearest pillar and the one worth steering around.
#   Using total_px for position would average two pillars into a point
#   between them, and the robot would drive at the gap.
#
# WIRING: OpenMV P4 (TX) -> ESP32 GPIO16 (RX). Common ground REQUIRED.

import sensor
import time
from pyb import UART, LED

led_red = LED(1)
led_green = LED(2)
led_blue = LED(3)
for _l in (led_red, led_green, led_blue):
    _l.off()

uart = UART(3, 115200, timeout_char=1000)

# ---------------- camera ----------------
MANUAL_GAIN_DB = 12.0     # None = use whatever auto settles on
SATURATION = 3            # stretches the A/B axes the thresholds key on

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)          # 320 x 240

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

# ---------------- thresholds ----------------
# (L min, L max, A min, A max, B min, B max)
# Widened for a dim frame. Re-tune with Tools > Machine Vision >
# Threshold Editor once your lighting is sorted.
RED_THRESHOLD     = (12,  75,   30, 127,    8,  90)
GREEN_THRESHOLD   = (20, 100, -100, -20,   15, 110)
MAGENTA_THRESHOLD = (12,  85,   40, 127, -105, -15)

MIN_TOTAL_PX = 400        # ignore a colour below this total area
SEND_INTERVAL_MS = 50     # ~20 Hz
FPS_INTERVAL_MS = 1000

# Region of interest (x, y, w, h). Crops off the far wall.
ROI = (0, 60, 320, 180)

clock = time.clock()
last_send = time.ticks_ms()
last_fps = time.ticks_ms()
heartbeat = 0


def val(attr):
    """Blob accessors are properties on newer firmware, methods on older."""
    return attr() if callable(attr) else attr


def measure(img, threshold):
    """Return (total_pixels, cx_of_largest_blob).

    total_pixels answers 'how much of this colour is on screen'.
    cx comes from the single biggest blob - the nearest pillar."""
    kwargs = dict(pixels_threshold=100, area_threshold=100,
                  merge=True, margin=10)
    if ROI:
        kwargs['roi'] = ROI
    blobs = img.find_blobs([threshold], **kwargs)

    total = 0
    best_px = 0
    best_cx = 160
    for b in blobs:
        px = val(b.pixels)
        total += px
        if px > best_px:
            best_px = px
            best_cx = int(val(b.cx))
    return total, best_cx


try:
  while True:
    clock.tick()
    img = sensor.snapshot()

    r_total, r_cx = measure(img, RED_THRESHOLD)
    g_total, g_cx = measure(img, GREEN_THRESHOLD)
    m_total, m_cx = measure(img, MAGENTA_THRESHOLD)

    # Dominant colour = the one covering the most of the frame
    colour = 'N'
    cx = 0
    total = 0
    if r_total >= g_total and r_total >= m_total and r_total >= MIN_TOTAL_PX:
        colour, cx, total = 'R', r_cx, r_total
    elif g_total >= m_total and g_total >= MIN_TOTAL_PX:
        colour, cx, total = 'G', g_cx, g_total
    elif m_total >= MIN_TOTAL_PX:
        colour, cx, total = 'M', m_cx, m_total

    # ---- overlay, cosmetic only, must never crash the loop ----
    try:
        if colour != 'N':
            c = {'R': (255, 0, 0),
                 'G': (0, 255, 0),
                 'M': (255, 0, 255)}[colour]
            img.draw_cross(cx, 120, color=c, size=20)
            img.draw_string(4, 4, "%s %d" % (colour, total), color=c)
        if ROI:
            img.draw_rectangle(ROI, color=(40, 40, 40))
    except Exception:
        pass

    # ---- status LEDs, readable with no laptop attached ----
    led_red.off()
    led_green.off()
    if colour == 'R':
        led_red.on()
    elif colour == 'G':
        led_green.on()
    elif colour == 'M':
        led_red.on()
        led_blue.on()

    heartbeat = (heartbeat + 1) % 15
    if heartbeat == 0 and colour != 'M':
        led_blue.toggle()

    # ---- transmit ----
    now = time.ticks_ms()
    if time.ticks_diff(now, last_send) >= SEND_INTERVAL_MS:
        last_send = now
        if colour == 'N':
            uart.write("N,0,0\n")
        else:
            uart.write("%s,%d,%d\n" % (colour, cx, total))

    if time.ticks_diff(now, last_fps) >= FPS_INTERVAL_MS:
        last_fps = now
        uart.write("F,%.1f\n" % clock.fps())

    # Uncomment to debug in the IDE. Costs fps - leave off on the robot.
    # print(colour, total, cx, clock.fps())

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
