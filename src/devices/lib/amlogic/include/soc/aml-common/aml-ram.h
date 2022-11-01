// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_RAM_H_
#define SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_RAM_H_

#include <stdint.h>

namespace aml_ram {

// NOTE:
// Port "DEVICE" is total name for small bandwidth device.
// There are many small bandwidth devices such as nand/arb/parser
// connected to dmc under port "device", for better configure of
// these devices, re-number them with start ID of 32
//
// EXAMPLE:
//
//            A5 DMC CONTROLLER
//                   |
//   -------------------------------
//   |    |    |    |        |     |
//  arm  nnq  dev1 test     dev0   dsp
//  (0)  (3)  (6)   (5)     (7)    (4)
//             |             |
//       -------------    -----------------
//       |     |     |    | ...  |    |    |
//     spicc1 eth spicc0 sdio  emmc  usb  audio
//      (32)  (33)  (34) (40)  (42)  (43) (44)
//
// When port < 32 :
//  set "dmc_offsets_.ctrl1_offset" to 'old_val | (1 << port)'
//  set "dmc_offsets_.ctrl2_offset" to '0xffff'.
//
// When port >= 32: (The driver has not implemented this function)
//  set "dmc_offsets_.ctrl1_offset" to '1 << 6' or '1 << 7'. (select the device port)
//  set "dmc_offsets_.ctrl2_offset" to 'old_val | (1 << y)'.
//
//   if (port - 32) >= 8;
//       y = port - 32 - 8
//   else
//       y = port - 32
//
//  e.g. set port -> sdio
//    port = 40;
//    y = 40 - 32 - 8 = 0;
//
//  ctrl1_offset = 1 << 7; (device0)
//  ctrl2_offset |= 1 << 0;
//
//
//            A1 DMC CONTROLLER
//                   |
//   --------------------------------------
//   |    |    |    |        |      |     |
//  arm dspa dspb  dev0   usb-ahb  i2c usb-host
//  (0)  (1)  (2)  (4)      (5)    (6)   (7)
//                  |
//       -----------------------
//       |      |        |     |
//     spicc sd_emmc_a audio  dma
//      (33)   (35)     (37)  (38)

// Astro and Sherlock ports.
constexpr uint64_t kPortIdArmAe = 0x01u << 0;
constexpr uint64_t kPortIdMali = 0x01u << 1;
constexpr uint64_t kPortIdPcie = 0x01u << 2;
constexpr uint64_t kPortIdHdcp = 0x01u << 3;
constexpr uint64_t kPortIdHevcFront = 0x01u << 4;
constexpr uint64_t kPortIdTest = 0x01u << 5;
constexpr uint64_t kPortIdUsb30 = 0x01u << 6;
constexpr uint64_t kPortIdHevcBack = 0x01u << 8;
constexpr uint64_t kPortIdH265Enc = 0x01u << 9;
constexpr uint64_t kPortIdVpuR1 = 0x01u << 16;
constexpr uint64_t kPortIdVpuR2 = 0x01u << 17;
constexpr uint64_t kPortIdVpuR3 = 0x01u << 18;
constexpr uint64_t kPortIdVpuW1 = 0x01u << 19;
constexpr uint64_t kPortIdVpuW2 = 0x01u << 20;
constexpr uint64_t kPortIdVDec = 0x01u << 21;
constexpr uint64_t kPortIdHCodec = 0x01u << 22;
constexpr uint64_t kPortIdGe2D = 0x01u << 23;
// Sherlock-only ports.
constexpr uint64_t kPortIdNNA = 0x01u << 10;
constexpr uint64_t kPortIdGDC = 0x01u << 11;
constexpr uint64_t kPortIdMipiIsp = 0x01u << 12;
constexpr uint64_t kPortIdArmAf = 0x01u << 13;
// A5-only ports
constexpr uint64_t kA5PortIdNNA = 0x01u << 3;
constexpr uint64_t kA5PortIdDsp = 0x01u << 4;
constexpr uint64_t kA5PortIdTest = 0x01u << 5;
constexpr uint64_t kA5PortIdDev1 = 0x01u << 6;
constexpr uint64_t kA5PortIdDev0 = 0x01u << 7;
constexpr uint64_t kA5Sub0PortIdSpicc1 = 0x01ul << 32;
constexpr uint64_t kA5Sub0PortIdEth = 0x01ul << 33;
constexpr uint64_t kA5Sub0PortIdSpicc0 = 0x01ul << 34;
constexpr uint64_t kA5Sub1PortIdSdio = 0x01ul << 41;
constexpr uint64_t kA5Sub1PortIdEmmc = 0x01ul << 42;
constexpr uint64_t kA5Sub1PortIdUsb = 0x01ul << 43;
constexpr uint64_t kA5Sub1PortIdAudio = 0x01ul << 44;
// A1-only ports
constexpr uint64_t kA1PortIdDspa = 0x01u << 1;
constexpr uint64_t kA1PortIdDspb = 0x01u << 2;
constexpr uint64_t kA1PortIdDev0 = 0x01u << 4;
constexpr uint64_t kA1PortIdUsbAhb = 0x01u << 5;
constexpr uint64_t kA1PortIdI2c = 0x01u << 6;
constexpr uint64_t kA1PortIdUsbHost = 0x01u << 7;
constexpr uint64_t kA1Sub0PortIdSpicc = 0x01ul << 33;
constexpr uint64_t kA1Sub0PortIdEmmc = 0x01ul << 35;
constexpr uint64_t kA1Sub0PortIdAudio = 0x01ul << 37;
constexpr uint64_t kA1Sub0PortIdDma = 0x01ul << 38;

constexpr uint64_t kDefaultChannelCpu = kPortIdArmAe;
constexpr uint64_t kDefaultChannelGpu = kPortIdMali;
constexpr uint64_t kDefaultChannelVDec =
    kPortIdHevcFront | kPortIdHevcBack | kPortIdVDec | kPortIdHCodec;
constexpr uint64_t kDefaultChannelVpu =
    kPortIdVpuR1 | kPortIdVpuR2 | kPortIdVpuR3 | kPortIdVpuW1 | kPortIdVpuW2;

}  // namespace aml_ram

#endif  // SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_RAM_H_
