# Preparing to install Fuchsia on Pixelbook

## Putting your device into developer mode
***WARNING: This will erase any state stored locally on your Pixelbook***

1. Power off the Pixelbook
2. Go into recovery mode by holding down Refresh+Esc while powering on the device.
3. Go into developer mode by pressing Ctrl+D.
4. Wait for the device to re-configure itself, this may take a while

Note: You may need to reboot the device once again to be able to access the
command shell needed for step #2 below.

## Booting from USB

1. Boot into ChromeOS
2. At the welcome or login screen press Ctrl+Alt+Refresh/F3 to enter a command shell.
3. Enter 'chronos' as the user with a blank password
4. Enable USB booting by running `sudo crossystem dev_boot_usb=1`
5. (optional) Default to USB booting by running `sudo crossystem dev_default_boot=usb`.
6. Reboot

If you didn't make USB booting the default (step 5), you will need to press
Ctrl+U at the grey 'warning OS-not verified' screen when you power on your
device. If the device tries to boot from USB, either because that is the default
or you pressed Ctrl+U, and the device fails to boot from USB you'll hear a
fairly loud <BEEP>. Note that ChromeOS bootloader USB enumeration during has
been observed to be slow. If you're having trouble booting from USB, it may be
helpful to remove other USB devices until the device is through the bootloader
and also avoid using a USB hub.
