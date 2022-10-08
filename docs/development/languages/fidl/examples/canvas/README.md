# FIDL case study: canvas

In this example, we start by creating a 2D line-drawing canvas, then proceed to
augment its functionality with various data flow patterns commonly used in FIDL,
such as implementing flow control on both sides of the connection, and improving
performance by reducing the number of message round trips.

## Creating an unthrottled canvas

<<_baseline_tutorial.md>>
