// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

//PMIC REGISTERS

#define PMIC_ANALDO_CON1 0x0402
#define PMIC_ANALDO_CON2 0x0404
#define PMIC_ANALDO_CON4 0x0408
#define PMIC_VPROC_CON7 0x021A
#define PMIC_ANALDO_CON21 0x0420
#define PMIC_ANALDO_CON23 0x0424
#define PMIC_ANALDO_CON25 0x0428
#define PMIC_DIGLDO_CON0 0x0500
#define PMIC_DIGLDO_CON2 0x0502
#define PMIC_DIGLDO_CON3 0x0504
#define PMIC_DIGLDO_CON5 0x0506
#define PMIC_DIGLDO_CON6 0x0508
#define PMIC_DIGLDO_CON7 0x050A
#define PMIC_DIGLDO_CON8 0x050C
#define PMIC_DIGLDO_CON11 0x0512
#define PMIC_DIGLDO_CON31 0x0536
#define PMIC_DIGLDO_CON55 0x0562
#define PMIC_DIGLDO_CON30 0x0534
#define PMIC_DIGLDO_CON32 0x0538
#define PMIC_DIGLDO_CON33 0x053A
#define PMIC_DIGLDO_CON36 0x0540
#define PMIC_DIGLDO_CON41 0x0546
#define PMIC_DIGLDO_CON44 0x054C
#define PMIC_DIGLDO_CON47 0x0552
#define PMIC_DIGLDO_CON48 0x0554
#define PMIC_DIGLDO_CON49 0x0556
#define PMIC_DIGLDO_CON50 0x0558
#define PMIC_DIGLDO_CON51 0x055A
#define PMIC_DIGLDO_CON53 0x055E

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
