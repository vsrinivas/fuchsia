# Scenic
This document introduces Scenic, the Fuchsia graphics engine.

## Overview
Scenic is a system service that composes graphical objects from multiple processes into a
shared scene graph. Scenic has two APIs for compositing client output: Gfx and Flatland.
Gfx is the legacy 3D compositor, and Flatland is a 2D compositor.

Scenic is responsible for providing composition, rendering, scheduling, and diagnostics for its
clients.
