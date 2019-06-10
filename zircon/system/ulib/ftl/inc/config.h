// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

/***********************************************************************/
/* File System Configuration                                           */
/***********************************************************************/
#define INC_FTL_NDM_MLC FALSE // TargetFTL-NDM on NDM MLC
#define INC_FTL_NDM_SLC TRUE  // TargetFTL-NDM on NDM SLC

#if INC_FTL_NDM_MLC && INC_FTL_NDM_SLC
#error Set INC_FTL_NDM_MLC or INC_FTL_NDM_SLC to TRUE, not both
#elif !INC_FTL_NDM_MLC && !INC_FTL_NDM_SLC
#error Need INC_FTL_NDM_MLC or INC_FTL_NDM_SLC set to TRUE
#endif

// Tool/Test Configuration
#define FS_DVR_TEST FALSE  // TRUE to run FS driver test

/***********************************************************************/
/* Private Module Assert/Debug Configuration                           */
/***********************************************************************/
#define FS_ASSERT TRUE    // TRUE enables filesys PfAssert()
