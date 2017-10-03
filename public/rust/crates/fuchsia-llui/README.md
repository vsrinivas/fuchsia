# Fuchsia Low Level UI

`fuchsia-llui` is a Rust library for doing low-level user-interface, talking directly to
the software framebuffer and the HID devices. Currently only the framebuffer functionality is
implemented.

There are two examples included in the library. `rectangle` draws some pleasing pastel
rectangles on the screen. `font` uses Raph Levien's [font-rs](https://github.com/google/font-rs)
to render characters.

### Running on hardware with supported GPUs

When Zircon detects that there is a GPU driver, it disables the software framebuffer. If you
try to run the examples on such a device you will see an error message and no graphics.

If you would like to run this software on such a device, you can disable the GPU driver with a
[kernel command line option](https://goo.gl/mLuH3F). The example below is for netbooting an
intel-based device like Acer or NUC.

        out/build-zircon/tools/bootserver \
            out/build-zircon/build-zircon-pc-x86-64/zircon.bin \
            out/release-x86-64/user.bootfs -- \
            gfxconsole.font=18x32 \
            driver.intel_gen_gpu.disable

If you have a different device, the shell command `dm dump` is useful for trying to find the
name of the GPU driver to disable it. Below is the output of `dm dump` on an Intel NUC.
Run `dm dump` on your target and try to find a similar set of drivers to the `intel_gen_display`,
`framebuffer` and `intel_gen_gpu` below.

        [root]
           <root> pid=1864
              [null] pid=1864 /boot/driver/builtin.so
              [zero] pid=1864 /boot/driver/builtin.so
           [misc]
              <misc> pid=1920
                 [console] pid=1920 /boot/driver/console.so
                 [dmctl] pid=1920 /boot/driver/dmctl.so
                 [tapctl] pid=1920 /boot/driver/ethertap.so
                 [hidctl] pid=1920 /boot/driver/hidctl.so
                 [intel-pt] pid=1920 /boot/driver/intel-pt.so
                 [rtc] pid=1920 /boot/driver/intel-rtc.so
                 [ktrace] pid=1920 /boot/driver/ktrace.so
                 [ptmx] pid=1920 /boot/driver/pty.so
                 [ramctl] pid=1920 /boot/driver/ramdisk.so
                 [sysinfo] pid=1920 /boot/driver/sysinfo.so
                 [test] pid=1920 /boot/driver/test.so
                 [usb-virtual-bus] pid=1920 /boot/driver/usb-virtual-bus.so
           [sys]
              <sys> pid=1806 /boot/driver/bus-acpi.so
                 [acpi] pid=1806 /boot/driver/bus-acpi.so
                    [acpi-battery] pid=1806 /boot/driver/bus-acpi.so
                    [acpi-battery] pid=1806 /boot/driver/bus-acpi.so
                    [acpi-battery] pid=1806 /boot/driver/bus-acpi.so
                    [acpi-battery] pid=1806 /boot/driver/bus-acpi.so
                    [acpi-battery] pid=1806 /boot/driver/bus-acpi.so
                    [acpi-battery] pid=1806 /boot/driver/bus-acpi.so
                 [pci] pid=1806 /boot/driver/bus-acpi.so
                    [pci] pid=1806 /boot/driver/bus-pci.so
                       [00:00:00] pid=1806 /boot/driver/bus-pci.so
                       [00:02:00] pid=1806 /boot/driver/bus-pci.so
                          <00:02:00> pid=3398 /boot/driver/bus-pci.so
                             [intel_gen_display] pid=3398 /boot/driver/libmsd_intel.so
                                [framebuffer] pid=3398 /boot/driver/framebuffer.so
                             [intel_gen_gpu] pid=3398 /boot/driver/libmsd_intel.so
                       [00:14:00] pid=1806 /boot/driver/bus-pci.so
                          <00:14:00> pid=3440 /boot/driver/bus-pci.so
                             [xhci] pid=3440 /boot/driver/xhci.so
                                [usb] pid=3440 /boot/driver/usb-bus.so
                                   [065] pid=3440 /boot/driver/usb-bus.so
                                      [ifc-000] pid=3440 /boot/driver/usb-bus.so
                                         [usb-hub] pid=3440 /boot/driver/usb-hub.so
                                   [066] pid=3440 /boot/driver/usb-bus.so
                                      [ifc-000] pid=3440 /boot/driver/usb-bus.so
                                         [usb-hub] pid=3440 /boot/driver/usb-hub.so
                                   [001] pid=3440 /boot/driver/usb-bus.so
                                      [ifc-000] pid=3440 /boot/driver/usb-bus.so
                                         [ums] pid=3440 /boot/driver/usb-mass-storage.so
                                            [lun-000] pid=3440 /boot/driver/usb-mass-storage.so
                                               [block] pid=3440 /boot/driver/block.so
                                                  [part-000] pid=3440 /boot/driver/mbr.so
                                                     [block] pid=3440 /boot/driver/block.so
                                   [002] pid=3440 /boot/driver/usb-bus.so
                                      [ifc-000] pid=3440 /boot/driver/usb-bus.so
                                         [usb-hub] pid=3440 /boot/driver/usb-hub.so
                                   [003] pid=3440 /boot/driver/usb-bus.so
                                      [ifc-000] pid=3440 /boot/driver/usb-bus.so
                                         [usb_bt_hci] pid=3440 /boot/driver/usb-bt-hci.so
                                      [ifc-001] pid=3440 /boot/driver/usb-bus.so
                                   [004] pid=3440 /boot/driver/usb-bus.so
                                      [ifc-000] pid=3440 /boot/driver/usb-bus.so
                                         [usb-hid] pid=3440 /boot/driver/usb-hid.so
                                            [hid-device-000] pid=3440 /boot/driver/hid.so
                                   [005] pid=3440 /boot/driver/usb-bus.so
                                      [ifc-000] pid=3440 /boot/driver/usb-bus.so
                                         [usb-hid] pid=3440 /boot/driver/usb-hid.so
                                            [hid-device-000] pid=3440 /boot/driver/hid.so
                                      [ifc-001] pid=3440 /boot/driver/usb-bus.so
                                         [usb-hid] pid=3440 /boot/driver/usb-hid.so
                                            [hid-device-001] pid=3440 /boot/driver/hid.so
                       [00:14:02] pid=1806 /boot/driver/bus-pci.so
                       [00:16:00] pid=1806 /boot/driver/bus-pci.so
                       [00:17:00] pid=1806 /boot/driver/bus-pci.so
                          <00:17:00> pid=3514 /boot/driver/bus-pci.so
                             [ahci] pid=3514 /boot/driver/ahci.so
                       [00:1c:00] pid=1806 /boot/driver/bus-pci.so
                       [01:00:00] pid=1806 /boot/driver/bus-pci.so
                       [00:1e:00] pid=1806 /boot/driver/bus-pci.so
                       [00:1e:06] pid=1806 /boot/driver/bus-pci.so
                          <00:1e:06> pid=3590 /boot/driver/bus-pci.so
                             [pci-sdhci] pid=3590 /boot/driver/pci-sdhci.so
                                [sdhci] pid=3590 /boot/driver/sdhci.so
                       [00:1f:00] pid=1806 /boot/driver/bus-pci.so
                       [00:1f:02] pid=1806 /boot/driver/bus-pci.so
                       [00:1f:03] pid=1806 /boot/driver/bus-pci.so
                          <00:1f:03> pid=3653 /boot/driver/bus-pci.so
                             [intel-hda-000] pid=3653 /system/driver/ihda-controller.so
                                [intel-hda-codec-000] pid=3653 /system/driver/ihda-controller.so
                                   [output-stream-001] pid=3653 /system/driver/realtek-ihda-codec.so
                                   [input-stream-002] pid=3653 /system/driver/realtek-ihda-codec.so
                                [intel-hda-codec-002] pid=3653 /system/driver/ihda-controller.so
                       [00:1f:04] pid=1806 /boot/driver/bus-pci.so
                       [00:1f:06] pid=1806 /boot/driver/bus-pci.so
                          <00:1f:06> pid=3757 /boot/driver/bus-pci.so
                             [intel-ethernet] pid=3757 /boot/driver/intel-ethernet.so
                                [ethernet] pid=3757 /boot/driver/ethernet.so
