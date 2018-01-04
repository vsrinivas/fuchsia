# Preparing to install Fuchsia on Pixelbook

## Update ChromeOS

If your Pixelbook has never been booted, it is best to boot it normally to check
for any critical updates, as follows:

1. Boot the Pixelbook normally. The power button is on the left side of the
device, near the front of the wrist rest.
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
minute or two. The process is complete when you hear two loud &lt;BEEP&gt;s.
6. The device should reboot itself when the Developer Mode transition is
complete. You can now jump to Step #2 in the "Boot from USB" section.

## Boot from USB

1. Boot into ChromeOS. You should see a screen that says "OS verification is
OFF" and approximately 30 seconds later the boot will continue.
2. Wait for the Welcome or Login screen to load. **Ignore** any link for "Enable
debugging features". Instead press Ctrl+Alt+Refresh/F3 to enter a
command shell. If pressing this key combination has no effect, try rebooting the
Pixelbook once more.
3. Enter 'chronos' as the user with a blank password
4. Enable USB booting by running `sudo crossystem dev_boot_usb=1`
5. (optional) Default to USB booting by running `sudo crossystem dev_default_boot=usb`.
6. Reboot by typing `sudo reboot`

If you didn't make USB booting the default (step 5), you will need to press
Ctrl+U at the grey 'warning OS-not verified' screen when you power on your
device. If the device tries to boot from USB, either because that is the default
or you pressed Ctrl+U, and the device fails to boot from USB you'll hear a
fairly loud <BEEP>. Note that ChromeOS bootloader USB enumeration during boot
has been observed to be slow. If you're having trouble booting from USB, it may
be helpful to remove other USB devices until the device is through the bootloader
and also avoid using a USB hub.
