// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_MT8167_INCLUDE_SOC_MT8167_MT8167_POWER_H_
#define SRC_DEVICES_LIB_MT8167_INCLUDE_SOC_MT8167_MT8167_POWER_H_

#include <vector>

// PMIC REGISTERS

constexpr uint32_t kPmicVprocCon5 = 0x0216;
constexpr uint32_t kPmicVprocCon7 = 0x021A;
constexpr uint32_t kPmicVprocCon8 = 0x021C;
constexpr uint32_t kPmicVprocCon9 = 0x021E;
constexpr uint32_t kPmicVprocCon10 = 0x0220;

constexpr uint32_t kPmicVsysCon5 = 0x023C;
constexpr uint32_t kPmicVsysCon7 = 0x0240;
constexpr uint32_t kPmicVsysCon8 = 0x0242;
constexpr uint32_t kPmicVsysCon9 = 0x0244;
constexpr uint32_t kPmicVsysCon10 = 0x0246;

constexpr uint32_t kPmicVcoreCon5 = 0x030A;
constexpr uint32_t kPmicVcoreCon7 = 0x030E;
constexpr uint32_t kPmicVcoreCon8 = 0x0310;
constexpr uint32_t kPmicVcoreCon9 = 0x0312;
constexpr uint32_t kPmicVcoreCon10 = 0x0314;

constexpr uint32_t kPmicAnaLdoCon1 = 0x0402;
constexpr uint32_t kPmicAnaLdoCon2 = 0x0404;
constexpr uint32_t kPmicAnaLdoCon4 = 0x0408;
constexpr uint32_t kPmicAnaLdoCon8 = 0x0410;
constexpr uint32_t kPmicAnaLdoCon16 = 0x0416;
constexpr uint32_t kPmicAnaLdoCon21 = 0x0420;
constexpr uint32_t kPmicAnaLdoCon23 = 0x0424;
constexpr uint32_t kPmicAnaLdoCon25 = 0x0428;
constexpr uint32_t kPmicAnaLdoCon26 = 0x042A;

constexpr uint32_t kPmicDigLdoCon0 = 0x0500;
constexpr uint32_t kPmicDigLdoCon2 = 0x0502;
constexpr uint32_t kPmicDigLdoCon3 = 0x0504;
constexpr uint32_t kPmicDigLdoCon5 = 0x0506;
constexpr uint32_t kPmicDigLdoCon6 = 0x0508;
constexpr uint32_t kPmicDigLdoCon7 = 0x050A;
constexpr uint32_t kPmicDigLdoCon8 = 0x050C;
constexpr uint32_t kPmicDigLdoCon11 = 0x0512;
constexpr uint32_t kPmicDigLdoCon24 = 0x052A;
constexpr uint32_t kPmicDigLdoCon26 = 0x052C;
constexpr uint32_t kPmicDigLdoCon27 = 0x052E;
constexpr uint32_t kPmicDigLdoCon28 = 0x0530;
constexpr uint32_t kPmicDigLdoCon29 = 0x0532;
constexpr uint32_t kPmicDigLdoCon31 = 0x0536;
constexpr uint32_t kPmicDigLdoCon55 = 0x0562;
constexpr uint32_t kPmicDigLdoCon30 = 0x0534;
constexpr uint32_t kPmicDigLdoCon32 = 0x0538;
constexpr uint32_t kPmicDigLdoCon33 = 0x053A;
constexpr uint32_t kPmicDigLdoCon36 = 0x0540;
constexpr uint32_t kPmicDigLdoCon41 = 0x0546;
constexpr uint32_t kPmicDigLdoCon44 = 0x054C;
constexpr uint32_t kPmicDigLdoCon47 = 0x0552;
constexpr uint32_t kPmicDigLdoCon48 = 0x0554;
constexpr uint32_t kPmicDigLdoCon49 = 0x0556;
constexpr uint32_t kPmicDigLdoCon50 = 0x0558;
constexpr uint32_t kPmicDigLdoCon51 = 0x055A;
constexpr uint32_t kPmicDigLdoCon52 = 0x055C;
constexpr uint32_t kPmicDigLdoCon53 = 0x055E;

constexpr uint32_t kMt8167NumPowerDomains = 23;

enum Mt8167PowerDomains {
  kBuckVProc,
  kBuckVCore,
  kBuckVSys,
  kALdoVAud28,
  kALdoVAud22,
  kALdoVAdc18,
  kALdoVXo22,
  kALdoVCamA,
  kVSysLdoVm,
  kVSysLdoVcn18,
  kVSysLdoVio18,
  kVSysLdoVCamIo,
  kVSysLdoVCamD,
  kVDLdoVcn35,
  kVDLdoVio28,
  kVDLdoVemc33,
  kVDLdoVmc,
  kVDLdoVmch,
  kVDLdoVUsb33,
  kVDLdoVGp1,
  kVDLdoVM25,
  kVDLdoVGp2,
  kVDLdoVCamAf,
};

#endif  // SRC_DEVICES_LIB_MT8167_INCLUDE_SOC_MT8167_MT8167_POWER_H_
