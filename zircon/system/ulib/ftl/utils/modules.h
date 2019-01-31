// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define MENU_WAIT 3 // number of seconds

typedef enum {
    kInitMod,
    kAppName,
    kStart,
    kReadParms,
    kEditParms,
    kShowParms,
    kErrorLookup,
    kInitNi,
    kWaitNi,
    kNiAddr,
    kNetUp,
    kNetDown,
    kParseMonCmd,
    kModCmdList,
    kModName,
    kFormat,
    kFormatResetWc,
    kUnformat,
    kMount,
    kSync,
    kVolName,
    kDevInserted,
    kDevRemoved,
    kCfUartDriver,
    kDev,
    kDisplayStats,
    kResetStats,
    kDnsSdAdded,
    kDnsSdRemoved,
    kmDnsResolved,
    kmDnsQueryResolved,
    kScrInit,
    kInfo,
} SysModCmds;

typedef void* (*Module)(int code, ...);

extern Module ModuleList[];

// Module List API

void modInit(void);
void modAdd(Module module);
void modReadParms(void);
void modMenu(int menu_wait);
void modLoop(int req);
Module modFirst(void);
Module modNext(Module mod);

// File Systems and Driver Modules

void* FsModule(int req, ...);
void* NdmModule(int req, ...);

#ifdef __cplusplus
}
#endif
