import sensor, image, time

# --- 1. Color Tracking Thresholds (L Min, L Max, A Min, A Max, B Min, B Max) ---
# IMPORTANT: You MUST tune these in the OpenMV IDE using the Threshold Editor
# based on the exact lighting conditions of your WRO competition mat!
red_threshold = (30, 100, 15, 127, 15, 127)   # Generic Red
green_threshold = (30, 100, -64, -8, -32, 32)   # Generic Green

# --- 2. Camera Setup ---
sensor.reset()
sensor.set_pixformat(sensor.RGB565) # Use RGB for color tracking
sensor.set_framesize(sensor.QVGA)   # 320x240 resolution balances speed and accuracy
sensor.skip_frames(time = 2000)     # Let the camera adjust to light

# CRITICAL FOR COLOR TRACKING: Turn off auto-gain and auto-white-balance
# Otherwise, the camera will change how it sees colors as you move around the track!
sensor.set_auto_gain(False)
sensor.set_auto_whitebal(False)

clock = time.clock()

# --- 3. Main Vision Loop ---
while (True):
    clock.tick()
    img = sensor.snapshot() # Take a picture

    # Detect Red Lights
    # pixels_threshold and area_threshold filter out tiny specks of noise
    for blob in img.find_blobs([red_threshold], pixels_threshold=150, area_threshold=150, merge=True):
        # Draw a box and a crosshair on the screen for debugging
        img.draw_rectangle(blob.rect(), color=(255, 0, 0))
        img.draw_cross(blob.cx(), blob.cy(), color=(255, 0, 0))

        # Output the exact position
        # cx() and cy() give you the X and Y coordinates of the center of the light
        print("RED LIGHT detected | X: %d, Y: %d, Area: %d" % (blob.cx(), blob.cy(), blob.pixels()))

    # Detect Green Lights
    for blob in img.find_blobs([green_threshold], pixels_threshold=150, area_threshold=150, merge=True):
        img.draw_rectangle(blob.rect(), color=(0, 255, 0))
        img.draw_cross(blob.cx(), blob.cy(), color=(0, 255, 0))

        print("GREEN LIGHT detected | X: %d, Y: %d, Area: %d" % (blob.cx(), blob.cy(), blob.pixels()))

    # Optional: Print FPS to ensure your code is running fast enough for the robot
    # print(clock.fps())
