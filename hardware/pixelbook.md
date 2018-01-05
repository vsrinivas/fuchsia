# Preparing to install Fuchsia on Pixelbook

## Update ChromeOS

If your Pixelbook has never been booted, it is best to boot it normally to check
for any critical updates, as follows:

1. Boot the Pixelbook normally. Opening the lid usually powers on the device.
If this doesn't work, the power button is on the left side of the device, near
the front of the wrist rest.
2. Tap the "Let's go" button.
3. Connect to a wired or wireless network.
4. Accept the terms to proceed to the update check step.
5. The device should check for updates, install any found.
6. After rebooting from any updates, tap 'Browse as Guest' in the lower left
corner.
7. From the browser UI, go into Settings->About Chrome OS and confirm the version
is &gt;=62.

## Put your device into developer mode
***WARNING: This will erase any state stored locally on your Pixelbook***

1. Power off the Pixelbook.
2. Go into Recovery Mode by holding down Refresh+Esc while powering on the
device. (The Refresh key is the third from the left on the top row of the
keyboard.)
3. Go into Developer Mode by pressing Ctrl+D.
4. Press Enter to confirm your choice to enter Developer Mode.
5. Wait for the device to re-configure itself, which will take several minutes.
Initially it may not appear to be doing anything. Let the device sit for a
minute or two. You will heard two loud &lt;BEEP&gt;s early in the process. The
process is complete when you hear two more loud &lt;BEEP&gt;s.
6. The device should reboot itself when the Developer Mode transition is
complete. You can now jump to Step #2 in the "Boot from USB" section.

## Boot from USB

1. Boot into ChromeOS.
2. You should see a screen that says "OS verification is OFF" and approximately
30 seconds later the boot will continue. Wait for the Welcome or Login screen
to load. **Ignore** any link for "Enable debugging features".
3. Press Ctrl+Alt+Refresh/F3 to enter a command shell. If pressing this key
combination has no effect, try rebooting the Pixelbook once more.
4. Enter 'chronos' as the user with a blank password
5. Enable USB booting by running `sudo crossystem dev_boot_usb=1`
6. (optional) Default to USB booting by running `sudo crossystem dev_default_boot=usb`.
7. Reboot by typing `sudo reboot`
8. On the "OS verification is OFF" screen press Ctrl+U to bypass the timeout and
boot from USB immediately. (See [Tips and Tricks](#tips-and-tricks) for other
short circuit options)

The USB drive is only needed for booting when you want to re-pave or otherwise
netboot the device. If you didn't make USB booting the default (Step #6), you
will need to press Ctrl+U at the grey 'warning OS-not verified' screen to boot
from USB when you power on your device. If the device tries to boot from USB,
either because that is the default or you pressed Ctrl+U, and the device fails
to boot from USB you'll hear a fairly loud &lt;BEEP&gt;. Note that ChromeOS
bootloader USB enumeration during boot has been observed to be slow. If you're
having trouble booting from USB, it may be helpful to remove other USB devices
until the device is through the bootloader and also avoid using a USB hub.

## Tips and Tricks

By default the ChromeOS bootloader has a long timeout to allow you to press
buttons. To shortcut this you can press Ctrl+D or Ctrl+U when on the grey screen
that warns that the OS will not be verified. Ctrl+D will cause the device to
skip the timeout and boot from its default source. Ctrl+U will skip the timeout
and boot the device from USB.
