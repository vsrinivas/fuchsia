# Toulouse

Toulouse is a nickname for a [Jetway PC](http://www.jetwayipc.com/product/hbjc130f731-series/) that
Fuchsia developers use as a platform for writing networking software. It has multiple ethernet ports
and mini-PCIe ports for adding wireless network adapters.

## Toulouse Setup & Configuration

You will need:
- Toulouse hardware
- Power supply (included with Toulouse)
- Ethernet cable(s)
- USB stick to get started
- At least one of:
  - Serial cable (e.g., StarTech USB null modem cable)
  - HDMI + USB keyboard

Tested Wifi/Bluetooth adapters include:
* QCA6174A
* QCA9880

*** promo
Fuchsia does not have a deterministic ordering for bringing up PCI devices, so the
/dev/class/ethernet/xxx paths have no well-defined mapping to the ports on the front of the device.
In order for netbooting and loglistener to work, we need to let netsvc know which path corresponds
to the left-most ethernet port.
***

Create a file that contains the text
`kernel.serial=legacy netsvc.interface=/dev/sys/pci/00:1f.6/e1000/ethernet`.

In your `fx set` commandline, add the following arguments:

`--args='kernel_cmdline_files=["<absolute path to your file>"] zedboot_cmdline_files=["<absolute path to your file>"]'`

[optional] Add `always_zedboot=true` in --args

The last option will always boot to zedboot instead of booting off the paved image. You have to
press 'm' before the timeout if you want to boot from disk, or re-pave without
'always_zedboot=true'. One possible workaround is to use 'always_zedboot=true' when preparing the
USB stick, and leaving the USB stick in when you want to netboot. Without the USB stick it will run
off disk.

By default the device boots from the internal storage first, and you cannot set USB drives as a
generic default.

Prepare a USB drive, using `fx mkzedboot` (see the [docs](bootloader_setup.md) for details, and see
above for how to make a USB stick that can netboot).

Insert the USB drive before powering on the device. Note: if the drive isn’t recognized, try using
the other USB port. Some ports are flaky.

On boot, press Esc or Del to enter the BIOS. This works over serial as well once the serial console
is enabled (see below).

In the "Boot" section, find the entry for USB UEFI and use the '+' key to move it to the top of the
list. Press F4 to save and reset.

To use the serial port on Debian/Ubuntu Linux, you may need to remove the 'brltty' program that
wants to take over every serial port: `sudo apt-get remove brltty`. You will need to unplug/replug
your serial cable after this to get it to work.

## Serial consoles

### Enabling serial for the BIOS

In the "Advanced" section, open the "Serial Port Console Redirection" settings. Enable "Console
Redirection" and ensure the "Console Redirection Settings" look similar to the following. (You may
tune these to taste, if you know what you're doing.)
* Terminal Type: VT-UTF8
* Bits per second: 115200
* Data Bits: 8
* Parity: None
* Stop Bits: 1
* Flow Control: Off

The other settings may be left at their default values.

### Example Linux serial consoles (assumes a serial device at /dev/ttyUSB0)
* screen /dev/ttyUSB0 115200'
* picocom -b 115200 /dev/ttyUSB0
* miniterm.py /dev/ttyUSB0 115200
