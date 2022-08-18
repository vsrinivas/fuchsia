# Screen Recording Example

To run the example:

    fx set workstation_eng.qemu-x64 --with-base //sdk/bundles:tools --with //src/ui/examples/screen_recording
    fx build
    // Start the emulator and log in
    ffx emu start
    ffx session add fuchsia-pkg://fuchsia.com/screen_recording#meta/screen_recording.cm

In this example, you should see a bouncing square on the left side and a screen recording of this on
the right.
