// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off

#ifndef DDK_PROTOCOL_DEF
#error Internal use only. Do not include.
#else
#ifndef PF_NOPUB
// Do not publish aliases in /dev/class/...
#define PF_NOPUB 1
#endif
DDK_PROTOCOL_DEF(BLOCK,          'pBLK', "block", 0)
DDK_PROTOCOL_DEF(BLOCK_IMPL,     'pBKC', "block-impl", 0)
DDK_PROTOCOL_DEF(CONSOLE,        'pCON', "console", 0)
DDK_PROTOCOL_DEF(DEVICE,         'pDEV', "device", 0)
DDK_PROTOCOL_DEF(DISPLAY,        'pDIS', "display", 0)
DDK_PROTOCOL_DEF(DISPLAY_CONTROLLER, 'pDSC', "display-controller", 0)
DDK_PROTOCOL_DEF(DISPLAY_CONTROLLER_IMPL, 'pDCI', "display-controller-impl", PF_NOPUB)
DDK_PROTOCOL_DEF(ETHERNET,       'pETH', "ethernet", 0)
DDK_PROTOCOL_DEF(ETHERNET_IMPL,  'pEMA', "ethernet-impl", 0)
DDK_PROTOCOL_DEF(FRAMEBUFFER,    'pFRB', "framebuffer", 0)
DDK_PROTOCOL_DEF(GPIO,           'pGPI', "gpio", PF_NOPUB)
DDK_PROTOCOL_DEF(HIDBUS,         'pHID', "hidbus", 0)
DDK_PROTOCOL_DEF(I2C,            'pI2C', "i2c", 0)
DDK_PROTOCOL_DEF(I2C_IMPL ,      'pI2I', "i2c-impl", 0)
DDK_PROTOCOL_DEF(INPUT,          'pINP', "input", 0)
DDK_PROTOCOL_DEF(ROOT,           'pAAA', "root", PF_NOPUB)
DDK_PROTOCOL_DEF(MISC,           'pMSC', "misc", PF_NOPUB)
DDK_PROTOCOL_DEF(MISC_PARENT,    'pMSP', "misc-parent", PF_NOPUB)
DDK_PROTOCOL_DEF(ACPI,           'pACP', "acpi", 0)
DDK_PROTOCOL_DEF(PCI,            'pPCI', "pci", 0)
DDK_PROTOCOL_DEF(PCIROOT,        'pPRT', "pci-root", PF_NOPUB)
DDK_PROTOCOL_DEF(TPM,            'pTPM', "tpm", 0)
DDK_PROTOCOL_DEF(USB,            'pUSB', "usb", 0)
DDK_PROTOCOL_DEF(USB_BUS,        'pUBS', "usb-bus", 0)
DDK_PROTOCOL_DEF(USB_DCI,        'pUDC', "usb-dci", 0)  // Device Controller Interface
DDK_PROTOCOL_DEF(USB_DEVICE,     'pUSD', "usb-device", 0)
DDK_PROTOCOL_DEF(USB_FUNCTION,   'pUSF', "usb-function", 0)
DDK_PROTOCOL_DEF(USB_HCI,        'pUHI', "usb-hci", 0)  // Host Controller Interface
DDK_PROTOCOL_DEF(USB_MODE_SWITCH,'pUMS', "usb-mode-switch", 0)
DDK_PROTOCOL_DEF(USB_DBC,        'pUDB', "usb-dbc", 0) // Debug Capability
DDK_PROTOCOL_DEF(USB_TEST_FWLOADER, 'pUTF', "usb-test-fwloader", 0)
DDK_PROTOCOL_DEF(BT_HCI,         'pBHC', "bt-hci", 0)
DDK_PROTOCOL_DEF(BT_TRANSPORT,   'pBTR', "bt-transport", 0)
DDK_PROTOCOL_DEF(BT_HOST,        'pBTH', "bt-host", 0)
DDK_PROTOCOL_DEF(BT_GATT_SVC,    'pBGS', "bt-gatt-svc", 0)
DDK_PROTOCOL_DEF(AUDIO,          'pAUD', "audio", 0)
DDK_PROTOCOL_DEF(MIDI,           'pMID', "midi", 0)
DDK_PROTOCOL_DEF(SDHCI,          'pSDH', "sdhci", 0)
DDK_PROTOCOL_DEF(SDMMC,          'pSDM', "sdmmc", 0)
DDK_PROTOCOL_DEF(SDIO,           'pSDI', "sdio", 0)
DDK_PROTOCOL_DEF(WLANPHY,        'pWLP', "wlanphy", 0)
DDK_PROTOCOL_DEF(WLANPHY_IMPL,   'pWPI', "wlanphy-impl", 0)
DDK_PROTOCOL_DEF(WLANIF,         'pWLI', "wlanif", 0)
DDK_PROTOCOL_DEF(WLANIF_IMPL,    'pWII', "wlanif-impl", 0)
DDK_PROTOCOL_DEF(WLANMAC,        'pWLM', "wlanmac", 0)
DDK_PROTOCOL_DEF(AUDIO_INPUT,    'pAUI', "audio-input", 0)
DDK_PROTOCOL_DEF(AUDIO_OUTPUT,   'pAUO', "audio-output", 0)
DDK_PROTOCOL_DEF(CAMERA,         'pCAM', "camera", 0)
DDK_PROTOCOL_DEF(MEDIA_CODEC,    'pMCF', "media-codec", 0)
DDK_PROTOCOL_DEF(BATTERY,        'pBAT', "battery", 0)
DDK_PROTOCOL_DEF(POWER,          'pPWR', "power", 0)
DDK_PROTOCOL_DEF(THERMAL,        'pTHM', "thermal", 0)
DDK_PROTOCOL_DEF(GPU_THERMAL,    'pGPT', "gpu-thermal", 0)
DDK_PROTOCOL_DEF(PTY,            'pPTY', "pty", 0)
DDK_PROTOCOL_DEF(IHDA,           'pHDA', "intel-hda", 0)
DDK_PROTOCOL_DEF(IHDA_CODEC,     'pIHC', "intel-hda-codec", 0)
DDK_PROTOCOL_DEF(IHDA_DSP,       'piHD', "intel-hda-dsp", 0)
DDK_PROTOCOL_DEF(AUDIO_CODEC,    'pAUC', "audio-codec", 0)
DDK_PROTOCOL_DEF(TEST,           'pTST', "test", 0)
DDK_PROTOCOL_DEF(TEST_PARENT,    'pTSP', "test-parent", PF_NOPUB)
DDK_PROTOCOL_DEF(PLATFORM_BUS,   'pPBU', "platform-bus", 0)
DDK_PROTOCOL_DEF(PLATFORM_DEV,   'pPDV', "platform-dev", 0)
DDK_PROTOCOL_DEF(I2C_HID,        'pIHD', "i2c-hid", 0)
DDK_PROTOCOL_DEF(SERIAL,         'pSer', "serial", 0)
DDK_PROTOCOL_DEF(SERIAL_IMPL,    'pSri', "serial-impl", 0)
DDK_PROTOCOL_DEF(CLK,            'pCLK', "clock", 0)
DDK_PROTOCOL_DEF(INTEL_GPU_CORE, 'pIGC', "intel-gpu-core", 0)
DDK_PROTOCOL_DEF(IOMMU,          'pIOM', "iommu", 0)
DDK_PROTOCOL_DEF(NAND,           'pNND', "nand", 0)
DDK_PROTOCOL_DEF(RAW_NAND,       'pRND', "rawnand", 0)
DDK_PROTOCOL_DEF(BAD_BLOCK,      'pBBL', "bad-block", PF_NOPUB)
DDK_PROTOCOL_DEF(MAILBOX,        'pMHU', "mailbox", PF_NOPUB)
DDK_PROTOCOL_DEF(SCPI,           'pSCP', "scpi", PF_NOPUB)
DDK_PROTOCOL_DEF(BACKLIGHT,      'pBKL', "backlight", 0)
DDK_PROTOCOL_DEF(CANVAS,         'pCAN', "canvas", PF_NOPUB)
DDK_PROTOCOL_DEF(SKIP_BLOCK,     'pSKB', "skip-block", 0)
// Protocol definition at garnet/magma/src/magma_util/platform/zircon/zircon_platform_ioctl.h
DDK_PROTOCOL_DEF(GPU,            'pGPU', "gpu", 0)
DDK_PROTOCOL_DEF(RTC,            'pRTC', "rtc", 0)
DDK_PROTOCOL_DEF(TEE,            'pTEE', "tee", 0)
#undef DDK_PROTOCOL_DEF
#endif
