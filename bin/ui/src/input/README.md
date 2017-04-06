# Mozart Input Injection Tool

This directory contains input, a simple tool to inject input events into Mozart.

## USAGE

    input keyevent hid_usage (int)
    input tap x y
    input swipe x0 y0 x1 y1

The x and y coordinates are in the range 0 to 1000 and will be proportionally
transformed to the current display.
