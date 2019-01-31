// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

/***********************************************************************/
/* TargetCore Configuration                                            */
/***********************************************************************/
#define INC_TARGETCORE FALSE  // TRUE iff linking with TargetCore
#define NVRAM_INC FALSE       // TRUE to include NVRAM storage
#define INC_ERR_STRINGS FALSE // TRUE to include error strings
#define INC_MEASURE_MOD FALSE // TRUE to include measurement module

// Parameter errors are detected if TRUE (Checks for parameter errors,
// writing to a closed file, etc. Reduces performance as a consequence).
#define OS_PARM_CHECK FALSE // TRUE or FALSE

// Fatal errors are detected if TRUE (Checks for NULL pointers, etc.
// Reduces performance as a consequence).
#define OS_FATAL_CHECK TRUE // TRUE or FALSE

/***********************************************************************/
/* Kernel Configuration                                                */
/***********************************************************************/
#define USE_LITE_KERNEL TRUE            // tasks, FIFO semaphores, timers only
#define INC_KERNEL FALSE                // TRUE to include kernel

// Promotes all but OS_TIMED_OUT and OS_WOULD_BLOCK errors to fatal errs
#define OS_MAKE_FATAL TRUE // TRUE or FALSE

/***********************************************************************/
/* File System Configuration                                           */
/***********************************************************************/
#define FILE_AIO_INC FALSE /* task-driven asynchronous file I/O */

// Turn on UTF8 checking and UTF8 <-> UTF16 conversions for TFAT (only
// when VFAT enabled)
#define UTF_ENABLED FALSE

// Flag to allow FS encryption/decryption
#define FS_CRYPT FALSE

// File System Selection: set to TRUE to include
#define INC_XFS FALSE      // TargetXFS

// TargetFTL Selection: set to TRUE to include
#define INC_FTL_NDM_SLC TRUE  // TargetFTL-NDM on NDM SLC
#define INC_FTL_NDM_MLC FALSE // TargetFTL-NDM on NDM MLC

// Tool/Test Configuration
#define INC_RAM_DVRS FALSE // TRUE to include FS RAM drivers
#define FS_DVR_TEST FALSE  // TRUE to run FS driver test
#define INC_PAGE_FTL TRUE  // TRUE to include page-based FTL

/***********************************************************************/
/* Private Module Assert/Debug Configuration                           */
/***********************************************************************/
#define FS_ASSERT TRUE    // TRUE enables filesys PfAssert()
#define FAT_DEBUG FALSE   // TRUE for TargetFAT debug output
#define XFS_DEBUG FALSE   // TRUE for TargetXFS debug output


/***********************************************************************/
/* Symbols derived from configuration above. Do not edit.              */
/***********************************************************************/

#define INC_FS (INC_FFS || INC_FAT || INC_XFS || INC_RFS || INC_ZFS)

#define INC_FTL_NDM (INC_FTL_NDM_SLC || INC_FTL_NDM_MLC || INC_FTL_NOR_WR1)

#define INC_NDM (INC_FFS_NDM_SLC || INC_FFS_NDM_MLC || INC_FTL_NDM_SLC || INC_FTL_NDM_MLC)

#ifdef __cplusplus
}
#endif
