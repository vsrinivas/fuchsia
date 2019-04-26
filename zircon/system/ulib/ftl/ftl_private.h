// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ftl.h>   // For API definition.

#ifdef __cplusplus
extern "C" {
#endif

//
// Symbol Definitions.
//
#define ui8 uint8_t

//
// Function Prototypes.
//

// TargetNDM interface to TargetFTL
int ndmEraseBlock(uint32_t pn, NDM ndm);
int ndmReadPages(uint32_t pn0, uint32_t count, ui8* buf, ui8* spare, NDM ndm);
int ndmReadSpare(uint32_t vpn, ui8* spare, NDM ndm);
int ndmWritePages(uint32_t pn0, uint32_t cnt, const ui8* buf, ui8* spare, NDM ndm);
int ndmWritePage(uint32_t vpn, const ui8* buf, ui8* spare, NDM ndm);
int ndmTransferPage(uint32_t old_vpn, uint32_t new_vpn, ui8* buf, ui8* spare, NDM ndm);
int ndmCheckPage(uint32_t pn, ui8* data, ui8* spare, NDM ndm);
uint32_t ndmPairOffset(uint32_t page_offset, CNDM ndm);

#ifdef __cplusplus
}
#endif

