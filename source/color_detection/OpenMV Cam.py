import sensor, image, time
from pyb import UART

# Initialize UART 3 at 115200 baud rate (P4=TX, P5=RX)
uart = UART(3, 115200)

# (Insert the color thresholds from the previous code here)
red_threshold   = (30, 100, 15, 127, 15, 127)
green_threshold = (30, 100, -64, -8, -32, 32)

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.skip_frames(time = 2000)
sensor.set_auto_gain(False)
sensor.set_auto_whitebal(False)

clock = time.clock()

while(True):
    clock.tick()
    img = sensor.snapshot()

    # Detect Red Lights
    for blob in img.find_blobs([red_threshold], pixels_threshold=150, area_threshold=150, merge=True):
        img.draw_rectangle(blob.rect(), color=(255, 0, 0))
        img.draw_cross(blob.cx(), blob.cy(), color=(255, 0, 0))

        # Create a string: "R,X,Y\n"
        data_string = "R,%d,%d,%d\n" % (blob.cx(), blob.cy(), blob.pixels())
        uart.write(data_string)
        print("Sent:", data_string.strip())

    # Detect Green Lights
    for blob in img.find_blobs([green_threshold], pixels_threshold=150, area_threshold=150, merge=True):
        img.draw_rectangle(blob.rect(), color=(0, 255, 0))
        img.draw_cross(blob.cx(), blob.cy(), color=(0, 255, 0))

        # Create a string: "G,X,Y\n"
        data_string = "G,%d,%d,%d\n" % (blob.cx(), blob.cy(), blob.pixels())
        uart.write(data_string)
        print("Sent:", data_string.strip())
