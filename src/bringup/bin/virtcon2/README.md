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
* Splash screen mode for startup and shutdown.
* Runtime product configuration.
* Flicker free single framebuffer mode.

### Roadmap

1. Improve splash screen mode.
2. Implement all features of legacy Virtcon.
3. Enable new Virtcon by default.
4. Remove legacy Virtcon code and dependencies.

## Using new Virtcon

Configure, and build

    fx set core.x64 --args use_legacy_virtcon=false # or similar
    fx build

then pave and boot.

## Testing

Configure

    fx set core.x64 --with //src/binrgup/bin/virtcon2:tests

Then test

    fx test virtual_console_tests
