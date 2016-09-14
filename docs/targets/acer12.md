# Acer Switch Alpha 12

WARNING:  These are directions to configure the machine and boot an experimental, in-development OS on it.

## Powering the Machine On
To power on you must hold the power button (lefthand side, above the Volume rocker) for several seconds, then let go.  You can safely let go when the tiny blue light on the power button itself turns on (yes, this is really hard to see when you’re holding the power button), or when the display backlight turns on.  If you hold too long it may end up power right back off again.

## Powering the Machine Off
If you boot into Windows 10 or something hangs or crashes and you need to power things off, Press and Hold the power button until the display shuts off.  To be sure, hold for about 10 seconds total.

## Entering the BIOS
With the machine off, Press and hold Volume Up, then continue to hold while pressing and holding the Power button.  Let go of the Power button when the display backlight turns on.  Alternatively, hold F2 on an attached keyboard while powering the machine on.

## Enabling Magenta Boot
1. Boot the machine and enter the BIOS
2. Select “Security” from the tabs at the left
3. Tap the “[clean]” gray bar under “Supervisor Password Is”
4. Enter a supervisor password, enter it again, press OK
5. Select “Boot” from the tabs at the left
6. Tap the “[Enabled]” gray bar under “Secure Boot”
    (if there’s no gray bar, you have not set a supervisor password, go back and do that now)
7. Select “Disabled” from the menu
8. The “Boot priority order” list may be adjusted using the up/down arrows to the right of each item
9. Order the list like so:
- USB HDD
- USB FDD
- USB CDROM
- Network Boot-IPV4
- Network Boot-IPV6
- HDD: <MFG> <SERIALNO>
- Windows Boot Manager
10. (Optional)  Go back to the “Security” tab and set the supervisor password back to nothing.
Otherwise you’ll need to enter the password every time you use the BIOS.
A password is required to modify the secure boot setting, but “disabled” will persist without one.
11. Select “Exit” from the tabs at the left
12. Select “Exit Saving Changes”

## What if you end up in the Windows 10 Setup?
If you don’t enter the BIOS and haven’t installed another OS, You’ll end up on a blue background “Hi there” screen asking you to select country, language, etc.  

1. Press Power and Hold it for about 10 seconds (the screen will turn off after 2-3 seconds).
2. Boot into the BIOS as described above.

## What if you get stuck in Windows 10 Recovery?
It’s possible to end up in a situation where the machine *really* wants to help you recover your failed boots into Windows 10 and dumps you into a recovery screen -- blue background, “Recovery” in the upper left, and some text saying “It looks like Windows didn’t load correctly”.

1. Select “See advanced repair options”
2. Select “Troubleshoot” (screwdriver and wrench icon)
3. Select “Advanced options” (checkmarks and lines icon)
4. Select “UEFI Firmware Settings” (integrated circuit and gear icon)
5. When prompted “Restart to change UEFI firmware settings”, select “Restart”
6. The machine should now reboot into the BIOS
7. Check that “Windows Boot Manager” didn’t get moved to the top of the boot order, fix it if it did

## How to Create a Bootable USB Flash Drive
1. Check out and build GigaBoot20x6: https://fuchsia.googlesource.com/gigaboot20x6
  * Note: build without parallelism (no -j flag to make)
2. Format your USB Flash Drive with a FAT32 partition as the first partition
3. Copy gigaboot20x6/out/osboot.efi to EFI/BOOT/BOOTX64.EFI on the USB Flash Drive
4. Copy build-magenta-pc-x86-64/magenta.bin to the root of the USB Flash Drive
5. Optionally copy an additional bootfs image to ramdisk.bin on the root of the USB Flash Drive

If you need to boot magenta over the network, skip step 4 and/or delete
magenta.bin from the root of the USB Flash Drive.
