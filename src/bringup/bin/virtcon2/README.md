# Virtcon

Virtcon is the system terminal. It is a critical part of bringup and
provides graphical output with minimal hardware requrements.

## Legacy Virtcon

The legacy implementation is written in C++ and graphical output is
limited to bitmap text and basic primitives such as pixel aligned
rectangles. Legacy Virtcon can be found in [virtcon/](/src/bringup/bin/virtcon).

## New Virtcon

The new version of Virtcon is written in Rust and powered by Carnelian.
Carnelian enable advanced vector graphics and truetype text rendering
while maintaining minimal hardware requirements.

### Goals

* Minimal resource usage.
* Maximize code reuse with Terminal app.
* Boot animation for startup and shutdown.
* Runtime product configuration.
* Flicker free single framebuffer mode.

### Roadmap

1. Boot animation chime support.
2. Silent boot system for runtime suppression of chime.

## Testing

Configure

    fx set core.x64 --with //src/bringup/bin/virtcon2:tests

Then test

    fx test virtual_console_tests
