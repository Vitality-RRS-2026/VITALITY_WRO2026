# MINIMAL camera test - no locking, no thresholds, nothing clever.
# Run this from the OpenMV IDE and LOOK AT THE FRAMEBUFFER.
#
# This is pure factory-default auto everything. If the picture is still
# dark and dull here, the problem is NOT in any of our code -- it is the
# lens, the light, or the hardware.

import sensor
import time

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.skip_frames(time=3000)      # auto gain/exposure/WB all left ON

clock = time.clock()

while True:
    clock.tick()
    img = sensor.snapshot()
    st = img.get_statistics()
    try:
        l = st.l_mean() if callable(st.l_mean) else st.l_mean
        a = st.a_mean() if callable(st.a_mean) else st.a_mean
        b = st.b_mean() if callable(st.b_mean) else st.b_mean
        print("Lmean %5.1f   A %6.1f   B %6.1f   fps %4.1f" %
              (l, a, b, clock.fps()))
    except Exception as e:
        print("stats unavailable:", e)
    time.sleep_ms(300)
