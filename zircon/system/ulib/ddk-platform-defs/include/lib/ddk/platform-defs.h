// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_DDK_PLATFORM_DEFS_H_
#define LIB_DDK_PLATFORM_DEFS_H_

#include <zircon/compiler.h>

__BEGIN_CDECLS

// clang-format off
// Vendor, Product and Device IDs for generic platform drivers
#define PDEV_VID_GENERIC            0x00
#define PDEV_PID_GENERIC            0x00
#define PDEV_DID_USB_DWC3           0x01   // DWC3 USB Controller
#define PDEV_DID_USB_XHCI           0x02   // XHCI USB Controller
#define PDEV_DID_KPCI               0x03   // Syscall based PCI driver
// 0x04 unused
#define PDEV_DID_USB_DWC2           0x05   // DWC2 USB Controller
#define PDEV_DID_RTC_PL031          0x06   // ARM Primecell PL031 RTC
#define PDEV_DID_DSI                0x07   // DSI
#define PDEV_DID_GPIO_TEST          0x08   // Simple GPIO test driver
#define PDEV_DID_DW_I2C             0x09   // Designware I2C
#define PDEV_DID_DW_PCIE            0x0A  // Designware PCIe
#define PDEV_DID_LED2472G           0x0B  // RPi Sense Hat LED2472G
#define PDEV_DID_VSI_VIP            0x0C  // Verisilicon VIP
// 0x0D unused
#define PDEV_DID_OPTEE                  0x0E  // OP-TEE OS Driver
// 0x10 unused
// 0x11 unused
#define PDEV_DID_CAMERA_SENSOR          0x12  // Camera Sensor
#define PDEV_DID_HID_BUTTONS            0x13  // HID Buttons
#define PDEV_DID_MUSB_PERIPHERAL        0x14  // MUSB in peripheral role
#define PDEV_DID_MUSB_HOST              0x15  // MUSB in host role
#define PDEV_DID_FAKE_DISPLAY           0x16  // Dummy display
#define PDEV_DID_FOCALTOUCH             0x17  // FocalTech touch device
#define PDEV_DID_LITE_ON_ALS            0x18  // Lite-On ambient light sensor
#define PDEV_DID_BOSCH_BMA253           0x19  // Bosch BMA253 acceleration sensor
#define PDEV_DID_SG_MICRO_SGM37603A     0x1A  // SG Micro SGM37603A backlight driver
#define PDEV_DID_SYSMEM                 0x1B  // Sysmem driver
#define PDEV_DID_GPIO_LIGHT             0x1C  // Driver for controlling lights via GPIOs
#define PDEV_DID_CPU_TRACE              0x1D  // CPU tracing driver
#define PDEV_DID_DW_DSI                 0x1E  // Designware DSI
#define PDEV_DID_USB_XHCI_COMPOSITE     0x1F  // XHCI USB Controller, as a composite device
#define PDEV_DID_SSD1306                0x20  // Oled Display
#define PDEV_DID_CAMERA_CONTROLLER      0x21  // Camera Controller
#define PDEV_DID_CADENCE_HPNFC          0x22  // Cadence NAND Flash controller
#define PDEV_DID_OT_RADIO               0x23  // OpenThread radio
#define PDEV_DID_GOODIX_GTX8X           0x25  // Goodix GTx8X touch controllers
#define PDEV_DID_RADAR_SENSOR           0x26  // Radar Sensor
#define PDEV_DID_POWER_DOMAIN_COMPOSITE 0x27  // Power domain, as a composite device
#define PDEV_DID_DW_SPI                 0x28  // Designware SPI
#define PDEV_DID_REGISTERS              0x29  // Registers device
#define PDEV_DID_DAI_TEST               0x2A  // DAI testing codec
#define PDEV_DID_PWM_VREG               0x2B  // PWM Voltage Regulator
#define PDEV_DID_FUSB302                0x2C  // FUSB203, USB power delivery
#define PDEV_DID_RAM_DISK               0x2D  // RAM disk device
#define PDEV_DID_RAM_NAND               0x2E  // RAM disk for nand devices
#define PDEV_DID_VIRTUAL_AUDIO          0x2F  // Virtual audio for test
#define PDEV_DID_BT_HCI_EMULATOR        0x30  // Bluetooth HCI emulator for test

// QEMU emulator
#define PDEV_VID_QEMU               0x01
#define PDEV_PID_QEMU               0x01

// 96Boards (unused)
#define PDEV_VID_96BOARDS           0x02

// Google
#define PDEV_VID_GOOGLE             0x03
#define PDEV_PID_GAUSS              0x01
#define PDEV_PID_MACHINA            0x02
#define PDEV_PID_ASTRO              0x03
// 0x04 unused
#define PDEV_PID_SHERLOCK           0x05
#define PDEV_PID_CLEO               0x06
#define PDEV_PID_EAGLE              0x07
#define PDEV_PID_VISALIA            0x08
#define PDEV_PID_C18                0x09
#define PDEV_PID_NELSON             0x0A
#define PDEV_PID_VS680_EVK          0x0B
#define PDEV_PID_LUIS               0x0C
#define PDEV_PID_GOLDFISH           0x0D
#define PDEV_PID_MOTMOT             0x0E
#define PDEV_PID_AV400              0x0F

#define PDEV_DID_GAUSS_AUDIO_IN     0x01
#define PDEV_DID_GAUSS_AUDIO_OUT    0x02
#define PDEV_DID_GAUSS_I2C_TEST     0x03
#define PDEV_DID_GAUSS_LED          0x04
#define PDEV_DID_ASTRO_GOODIXTOUCH  0x05
#define PDEV_DID_GOOGLE_AMLOGIC_CPU 0x06
#define PDEV_DID_GOLDFISH_CONTROL   0x07
#define PDEV_DID_GOOGLE_BROWNOUT    0x08  // Product-specific brownout protection device
#define PDEV_DID_GOLDFISH_PIPE_CONTROL 0x09
#define PDEV_DID_GOLDFISH_PIPE_SENSOR  0x0A

// Khadas
#define PDEV_VID_KHADAS             0x04
#define PDEV_PID_VIM2               0x02
#define PDEV_PID_VIM2_MACHINA       0X3EA
#define PDEV_PID_VIM3               0x03
#define PDEV_DID_VIM3_MCU           0x05
#define PDEV_DID_VIM2_THERMAL       0x06

#define PDEV_DID_VIM_DISPLAY        0x01

// Amlogic
#define PDEV_VID_AMLOGIC            0x05
#define PDEV_PID_AMLOGIC_A113       0x01
#define PDEV_PID_AMLOGIC_S912       0x02
#define PDEV_PID_AMLOGIC_S905D2     0x03
#define PDEV_PID_AMLOGIC_T931       0x04
#define PDEV_PID_AMLOGIC_S905D3     0x05
#define PDEV_PID_AMLOGIC_A311D      0x06
#define PDEV_PID_AMLOGIC_A5         0x07

#define PDEV_DID_AMLOGIC_GPIO        0x01
#define PDEV_DID_AMLOGIC_I2C         0x02
#define PDEV_DID_AMLOGIC_UART        0x03
#define PDEV_DID_AMLOGIC_AXG_CLK     0x04
#define PDEV_DID_AMLOGIC_GXL_CLK     0x05
#define PDEV_DID_AMLOGIC_SDMMC_A   0x06
#define PDEV_DID_AMLOGIC_SDMMC_B   0x07
#define PDEV_DID_AMLOGIC_SDMMC_C   0x08
#define PDEV_DID_AMLOGIC_ETH         0x09
#define PDEV_DID_AMLOGIC_THERMAL_PLL 0x0A
#define PDEV_DID_AMLOGIC_MAILBOX     0x0B
#define PDEV_DID_AMLOGIC_SCPI        0x0C
#define PDEV_DID_AMLOGIC_DISPLAY     0x0D
#define PDEV_DID_AMLOGIC_VIDEO       0x0E
#define PDEV_DID_AMLOGIC_RAW_NAND    0x0F
#define PDEV_DID_AMLOGIC_CANVAS      0x10
#define PDEV_DID_AMLOGIC_G12A_CLK    0x11
#define PDEV_DID_AMLOGIC_TDM         0x12
#define PDEV_DID_AMLOGIC_PDM         0x13
#define PDEV_DID_AMLOGIC_G12B_CLK    0x14
#define PDEV_DID_AMLOGIC_MIPI_CSI    0x15
#define PDEV_DID_SHERLOCK_PDM        0x16
#define PDEV_DID_AMLOGIC_MALI_INIT   0x17
#define PDEV_DID_AML_USB_PHY_V2      0x18
#define PDEV_DID_AMLOGIC_SPI         0x19
#define PDEV_DID_AMLOGIC_SECURE_MEM  0x1A
#define PDEV_DID_AMLOGIC_GE2D        0x1B
#define PDEV_DID_AMLOGIC_NNA         0x1C
#define PDEV_DID_AMLOGIC_PWM         0x1D
#define PDEV_DID_AMLOGIC_CPU         0x1E
#define PDEV_DID_AMLOGIC_PWM_INIT    0x1F
#define PDEV_DID_NELSON_PDM          0x20
#define PDEV_DID_NELSON_USB_PHY      0x21
#define PDEV_DID_AMLOGIC_SM1_CLK     0x22
#define PDEV_DID_AMLOGIC_VIDEO_ENC   0x23
#define PDEV_DID_AMLOGIC_RAM_CTL     0x24
#define PDEV_DID_AMLOGIC_HEVC_ENC    0x25
#define PDEV_DID_AMLOGIC_POWER       0x26
#define PDEV_DID_AMLOGIC_THERMISTOR  0x27
#define PDEV_DID_AMLOGIC_THERMAL_DDR 0x28
#define PDEV_DID_AMLOGIC_DAI_OUT     0x29
#define PDEV_DID_AMLOGIC_DAI_IN      0x2A
#define PDEV_DID_AMLOGIC_HDMI        0x2B
#define PDEV_DID_AMLOGIC_A5_CLK      0x2C
#define PDEV_DID_VIM3_USB_PHY        0x2D
#define PDEV_DID_AMLOGIC_RTC         0x2E
#define PDEV_DID_AML_USB_CRG_PHY_V2  0x2F
#define PDEV_DID_USB_CRG_UDC         0x30  // Corigine USB device controller
#define PDEV_DID_AMLOGIC_DSP         0x31

// Broadcom
#define PDEV_VID_BROADCOM           0x06
#define PDEV_PID_BCM4356            0x01
#define PDEV_PID_BCM43458           0x02

#define PDEV_DID_BCM_WIFI           0x01

//Hardkernel
#define PDEV_VID_HARDKERNEL         0x07
#define PDEV_PID_ODROID_C2          0x01

// Intel
#define PDEV_VID_INTEL              0x08
#define PDEV_PID_X86                0x01

// NXP
#define PDEV_VID_NXP                0x09
#define PDEV_PID_IMX8MMEVK          0x01

#define PDEV_DID_IMX_GPIO           0x01
#define PDEV_DID_IMX_DISPLAY        0x02
#define PDEV_DID_IMX_SDHCI          0x03
#define PDEV_DID_IMX_I2C            0x04
#define PDEV_DID_PCF8563_RTC        0x05

// REALTEK
#define PDEV_VID_REALTEK            0x0B
#define PDEV_PID_RTL8211F           0x01
#define PDEV_DID_ALC5663            0x01
#define PDEV_DID_ALC5514            0x02
#define PDEV_DID_REALTEK_ETH_PHY    0x03

// Designware
#define PDEV_VID_DESIGNWARE         0x0C
#define PDEV_DID_DESIGNWARE_ETH_MAC 0x10

// Mediatek
#define PDEV_VID_MEDIATEK           0x0D
#define PDEV_PID_MEDIATEK_8167S_REF 0x01
#define PDEV_DID_MEDIATEK_GPIO      0x01
#define PDEV_DID_MEDIATEK_MSDC0     0x02
#define PDEV_DID_MEDIATEK_MSDC1     0x03
#define PDEV_DID_MEDIATEK_MSDC2     0x04
#define PDEV_DID_MEDIATEK_DISPLAY   0x05
#define PDEV_DID_MEDIATEK_I2C       0x06
#define PDEV_DID_MEDIATEK_GPU       0x07
#define PDEV_DID_MEDIATEK_CLK       0x08
#define PDEV_DID_MEDIATEK_THERMAL   0x09
#define PDEV_DID_MEDIATEK_AUDIO_OUT 0x0A
#define PDEV_DID_MEDIATEK_AUDIO_IN  0x0B
#define PDEV_DID_MEDIATEK_DSI       0x0C
#define PDEV_DID_MEDIATEK_POWER     0x0D
#define PDEV_DID_MEDIATEK_SPI       0x0E

// Sony
#define PDEV_VID_SONY               0x0E
#define PDEV_PID_SONY_IMX227        0x01
#define PDEV_PID_SONY_IMX355        0x02

// Hisilicon
#define PDEV_VID_HISILICON          0x0F
#define PDEV_PID_CORNEL             0x01

// Texas Instruments
#define PDEV_VID_TI                 0x10
#define PDEV_PID_TI_LP8556          0x01
#define PDEV_PID_TI_LP5018          0x02
#define PDEV_PID_TI_LP5024          0x03
#define PDEV_PID_TI_LP5030          0x04
#define PDEV_PID_TI_LP5036          0x05
#define PDEV_PID_TI_TMP112          0x06
#define PDEV_DID_TI_BACKLIGHT       0x01
#define PDEV_DID_TI_TAS58xx         0x02
#define PDEV_DID_TI_TAS5782         0x03
#define PDEV_DID_TI_TAS2770         0x04
#define PDEV_DID_TI_LED             0x05
#define PDEV_DID_TI_TEMPERATURE     0x06
#define PDEV_DID_TI_TAS5720         0x07
#define PDEV_DID_TI_INA231          0x08
#define PDEV_DID_TI_TCA6408A        0x09
#define PDEV_DID_TI_TAS5707         0x0A

// Test
#define PDEV_VID_TEST               0x11
#define PDEV_PID_PBUS_TEST          0x01
#define PDEV_PID_USB_VBUS_TEST      0x03
#define PDEV_PID_HIDCTL_TEST        0x04
#define PDEV_PID_VCAMERA_TEST       0x05
#define PDEV_PID_LIBDRIVER_TEST     0x06
#define PDEV_PID_METADATA_TEST      0x07
#define PDEV_PID_PCI_TEST           0x08
#define PDEV_PID_DDKFIDL_TEST       0x09
#define PDEV_PID_COMPATIBILITY_TEST 0x0A
#define PDEV_PID_POWER_TEST         0x0B
#define PDEV_PID_SCHEDULE_WORK_TEST 0x0D
#define PDEV_PID_DEVHOST_TEST       0x0E
#define PDEV_PID_TEL_TEST           0x0F
#define PDEV_PID_LIFECYCLE_TEST     0x10
#define PDEV_PID_OT_TEST            0x11
#define PDEV_PID_INSTANCE_LIFECYCLE_TEST     0x12
#define PDEV_PID_INSPECT_TEST       0x13
#define PDEV_PID_ENVIRONMENT_TEST   0x14
#define PDEV_PID_FIRMWARE_TEST      0x15
#define PDEV_PID_FALLBACK_TEST      0x16
#define PDEV_PID_RESTART_TEST       0x17
#define PDEV_PID_TEST               0x18

#define PDEV_DID_TEST_PARENT        0x01
#define PDEV_DID_TEST_CHILD_1       0x02
#define PDEV_DID_TEST_CHILD_2       0x03
#define PDEV_DID_TEST_CHILD_3       0x04
#define PDEV_DID_TEST_GPIO          0x05
#define PDEV_DID_TEST_COMPOSITE     0x06
#define PDEV_DID_TEST_CLOCK         0x07
#define PDEV_DID_TEST_I2C           0x08
#define PDEV_DID_TEST_POWER         0x09
#define PDEV_DID_TEST_CHILD_4       0x0A
#define PDEV_DID_TEST_VCAMERA       0x0B
#define PDEV_DID_TEST_AUDIO_CODEC   0x0C
#define PDEV_DID_TEST_DDKFIDL       0x0D
#define PDEV_DID_TEST_VCAM_FACTORY  0x0E
#define PDEV_DID_TEST_COMPOSITE_1   0x0F
#define PDEV_DID_TEST_COMPOSITE_2   0x10
#define PDEV_DID_TEST_SPI           0x12
#define PDEV_DID_TEST_CAMERA_SENSOR 0x13
#define PDEV_DID_TEST_QMI_MODEM     0x14
#define PDEV_DID_TEST_DDKASYNCFIDL  0x15
#define PDEV_DID_TEST_AT_MODEM      0x16
#define PDEV_DID_TEST_PWM           0x17
#define PDEV_DID_TEST_OT_RADIO      0x18
#define PDEV_DID_TEST_RPMB          0x19
#define PDEV_DID_TEST_BTI           0x1A
#define PDEV_DID_TEST_VREG          0x1B
#define PDEV_DID_TEST_GOLDFISH_PIPE 0x1C
#define PDEV_DID_TEST_GOLDFISH_ADDRESS_SPACE     0x1D
#define PDEV_DID_TEST_GOLDFISH_CONTROL_COMPOSITE 0x1E
#define PDEV_DID_TEST_CRASH         0x1F
#define PDEV_DID_TEST_GOLDFISH_SYNC 0x20
#define PDEV_DID_TEST_PCI           0x21
#define PDEV_DID_TEST_POWER_SENSOR  0x22

// ARM
#define PDEV_VID_ARM                0x12
#define PDEV_PID_ARM_ISP            0x01
#define PDEV_PID_GDC                0x02
#define PDEV_PID_GE2D               0x03
#define PDEV_PID_ACPI_BOARD         0x04
#define PDEV_DID_ARM_MALI_IV009     0x01
#define PDEV_DID_ARM_MALI_IV010     0x02
#define PDEV_DID_ARM_MAGMA_MALI     0x03

// Qualcomm
#define PDEV_VID_QUALCOMM           0x13
#define PDEV_PID_QUALCOMM_MSM8X53   0x01
#define PDEV_PID_QUALCOMM_MSM8998   0x02
#define PDEV_PID_QUALCOMM_MSM8150   0x03

#define PDEV_DID_QUALCOMM_GPIO          0x01
#define PDEV_DID_QUALCOMM_PIL           0x02
#define PDEV_DID_QUALCOMM_SDC1          0x03
#define PDEV_DID_QUALCOMM_CLOCK         0x04
#define PDEV_DID_QUALCOMM_POWER         0x05
#define PDEV_DID_QUALCOMM_MAGMA_ADRENO  0x06

// Synaptics
#define PDEV_VID_SYNAPTICS          0x14
#define PDEV_PID_SYNAPTICS_AS370    0x01
#define PDEV_PID_SYNAPTICS_VS680    0x02
#define PDEV_DID_SYNAPTICS_GPIO     0x01
#define PDEV_DID_AS370_USB_PHY      0x02
#define PDEV_DID_AS370_AUDIO_OUT    0x03
#define PDEV_DID_AS370_AUDIO_IN     0x04
#define PDEV_DID_AS370_CLOCK        0x05
#define PDEV_DID_AS370_DHUB         0x06
#define PDEV_DID_AS370_POWER        0x07
#define PDEV_DID_AS370_THERMAL      0x08
#define PDEV_DID_AS370_TOUCH        0x09
#define PDEV_DID_AS370_SDHCI0       0x0A
#define PDEV_DID_VS680_SDHCI0       0x0B
#define PDEV_DID_VS680_SDHCI1       0x0C
#define PDEV_DID_VS680_USB_PHY      0x0D
#define PDEV_DID_VS680_CLOCK        0x0E
#define PDEV_DID_VS680_THERMAL      0x0F
#define PDEV_DID_VS680_POWER        0x10
#define PDEV_DID_AS370_SDHCI1       0x11

// Maxim
#define PDEV_VID_MAXIM              0x15
#define PDEV_DID_MAXIM_MAX98373     0x01
#define PDEV_DID_MAXIM_MAX98927     0x02

// Nordic
#define PDEV_VID_NORDIC             0x16
#define PDEV_PID_NORDIC_NRF52840    0x01
#define PDEV_PID_NORDIC_NRF52811    0x02
#define PDEV_DID_NORDIC_THREAD      0x01

// Marvell
#define PDEV_VID_MARVELL            0x17
#define PDEV_PID_MARVELL_88W8987    0x01
#define PDEV_DID_MARVELL_WIFI       0x01

// Infineon
#define PDEV_VID_INFINEON             0x18
#define PDEV_PID_INFINEON_BGT60TR13C  0x01

// Silergy
#define PDEV_VID_SILERGY            0x19
#define PDEV_PID_SILERGY_SYBUCK     0X01

// FocalTech
#define PDEV_VID_FOCALTECH           0x1A
#define PDEV_DID_FOCALTECH_FT8201    0x01

// Sensirion
#define PDEV_VID_SENSIRION           0x1B
#define PDEV_DID_SENSIRION_SHTV3     0x01

// Goodix
#define PDEV_VID_GOODIX              0x1C
#define PDEV_DID_GOODIX_GT6853       0x01

// Verisilicon
#define PDEV_VID_VERISILICON            0x1D
#define PDEV_DID_VERISILICON_MAGMA_VIP  0x01

// Dialog
#define PDEV_VID_DIALOG                 0x1E
#define PDEV_DID_DIALOG_DA7219          0x01

// clang-format on

__END_CDECLS

#endif  // LIB_DDK_PLATFORM_DEFS_H_
