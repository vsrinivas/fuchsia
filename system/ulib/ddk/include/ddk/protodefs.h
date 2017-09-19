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
DDK_PROTOCOL_DEF(BLOCK_CORE,     'pBKC', "block-core", 0)
DDK_PROTOCOL_DEF(CONSOLE,        'pCON', "console", 0)
DDK_PROTOCOL_DEF(DEVICE,         'pDEV', "device", 0)
DDK_PROTOCOL_DEF(DISPLAY,        'pDIS', "display", 0)
DDK_PROTOCOL_DEF(ETHERNET,       'pETH', "ethernet", 0)
DDK_PROTOCOL_DEF(ETHERMAC,       'pEMA', "ethermac", 0)
DDK_PROTOCOL_DEF(FRAMEBUFFER,    'pFRB', "framebuffer", 0)
DDK_PROTOCOL_DEF(GPIO,           'pGPI', "gpio", 0)
DDK_PROTOCOL_DEF(HIDBUS,         'pHID', "hidbus", 0)
DDK_PROTOCOL_DEF(INPUT,          'pINP', "input", 0)
DDK_PROTOCOL_DEF(ROOT,           'pAAA', "root", PF_NOPUB)
DDK_PROTOCOL_DEF(MISC,           'pMSC', "misc", PF_NOPUB)
DDK_PROTOCOL_DEF(MISC_PARENT,    'pMSP', "misc-parent", PF_NOPUB)
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
DDK_PROTOCOL_DEF(BLUETOOTH_HCI,  'pBHC', "bt-hci", 0)
DDK_PROTOCOL_DEF(AUDIO,          'pAUD', "audio", 0)
DDK_PROTOCOL_DEF(MIDI,           'pMID', "midi", 0)
DDK_PROTOCOL_DEF(ACPI_BUS,       'pABS', "acpi-bus", 0)
DDK_PROTOCOL_DEF(ACPI,           'pACP', "acpi", 0)
DDK_PROTOCOL_DEF(SDHCI,          'pSDH', "sdhci", 0)
DDK_PROTOCOL_DEF(SDMMC,          'pSDM', "sdmmc", 0)
DDK_PROTOCOL_DEF(WLANMAC,        'pWMA', "wlanmac", 0)
DDK_PROTOCOL_DEF(AUDIO_INPUT,    'pAUI', "audio-input", 0)
DDK_PROTOCOL_DEF(AUDIO_OUTPUT,   'pAUO', "audio-output", 0)
DDK_PROTOCOL_DEF(BATTERY,        'pBAT', "battery", 0)
DDK_PROTOCOL_DEF(POWER,          'pPWR', "power", 0)
DDK_PROTOCOL_DEF(PTY,            'pPTY', "pty", 0)
DDK_PROTOCOL_DEF(IHDA,           'pHDA', "intel-hda", 0)
DDK_PROTOCOL_DEF(IHDA_CODEC,     'pIHC', "intel-hda-codec", 0)
DDK_PROTOCOL_DEF(TEST,           'pTST', "test", 0)
DDK_PROTOCOL_DEF(BCM_BUS,        'pBCB', "bcm-bus", 0)
DDK_PROTOCOL_DEF(PLATFORM_DEV,   'pPDV', "platform-dev", 0)
#undef DDK_PROTOCOL_DEF
#endif
