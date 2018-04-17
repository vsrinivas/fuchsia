#  Zircon on iMX8M EVK
Periodically check this file as the setup workflow will change/improve.


Please refer to the following documents for SoC and board related information:

i.MX 8M EVK Board Hardawre User's Guide:
https://www.nxp.com/docs/en/user-guide/IMX8MDQLQEVKHUG.pdf

iMX8M Technical Reference Manual:
https://www.nxp.com/docs/en/reference-manual/IMX8MDQLQRM.pdf

u-Boot Source:
https://source.codeaurora.org/external/imx/uboot-imx/
https://source.codeaurora.org/external/imx/uboot-imx/log/?h=imx_v2017.03_4.9.51_imx8m_ga


## Booting Zircon via USB Flash Drive:

+ Build Zircon
+ Copy build-arm64/imx8mevk-combo-bootdata.bin into a USB Flash Drive (FAT format)
+ Insert USB into the board's USB 3 HOST
+ Reboot board and press space to halt autoboot
+ From u-boot command line do the following:
    + usb start
    + icache off
    + dcache off
    + fatload usb 0 0x40480000 imx8mevk-combo-bootdata.bin
    + go 0x40480000


## Flashing Zircon on eMMC: <Internal to Google Only for now>

The board will boot out of eMMC by default. In order to boot Zircon, a custom u-boot binary
is needed. The binary can be found at: <Team Drive>/Zircon/NXP/u-boot/u-boot.imx

First step involves flashing the board with the custom u-boot binary:

# Requirements:
 + Linux Host Machine
 + Connect USB cable from the USB-C port on the board to your host
 + Create a file under /etc/udev/rules.d/70-nxp.rules with the following content:

 SUBSYSTEM=="usb", ATTR{idVendor}=="0525", MODE="0664", GROUP="plugdev", TAG+="uaccess"


# From Device (iMX8 EVK):

+ Reboot board and press space to halt autoboot
+ From u-boot command line do the following:
    + fastboot 0

# From Linux Host:
 + fastboot flash bootloader0 /PATH/TO/CUSTOM/UBOOT/u-boot.imx
 + fastboot reboot

 If successful, the new U-Boot prompt should be "zircon-u-boot=>"

Once the custom U-Boot has been flashed, perform the following:
+ Reboot board and press space to halt autoboot
+ From u-boot command line do the following:
    + fastboot 0

From the host side, go to your zircon repository and run the following commnand:
+ ./scripts/flash-nxp

If successful, the board will reboot into Zircon.
