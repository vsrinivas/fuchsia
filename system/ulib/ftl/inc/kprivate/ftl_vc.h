// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fsprivate.h>
#include <fscache.h>

/***********************************************************************/
/* Symbol Definitions                                                  */
/***********************************************************************/
#define FTLVC_WRITE 1
#define FTLVC_READ 2

/***********************************************************************/
/* Function Prototypes                                                 */
/***********************************************************************/
void* ftlvcNew(void* ftl, ui32 num_cached_pages, MedWFunc wr_page, MedRFunc rd_page, ui32 page_sz);
void ftlvcDelete(void* ftlvc);
FcEntry* ftlvcGetRdPage(void* ftlvc, ui32 vpn);
int ftlvcFlushPage(void* ftlvc, ui32 vpn);
void ftlvcSetPageDirty(void* ftlvc, FcEntry* ftlvc_ent);
void ftlvcUpdate(void* ftlvc, ui32 start_vpn, ui32 n, const ui8* data, ui32 page_sz);
