// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>
#include <stddef.h>

#include <stdio.h>

/***********************************************************************/
/* Configuration                                                       */
/***********************************************************************/
#define L_tmpnam_TFS 14
#define TMP_MAX_TFS 10000
#define BUFSIZ_TFS 256

/***********************************************************************/
/* Symbol Definitions                                                  */
/***********************************************************************/
#ifndef NULL
#define NULL 0
#endif
#define _IOFBF_TFS 0
#define _IOLBF_TFS 1
#define _IONBF_TFS 2
#define EOF_TFS (-1)
#define SEEK_CUR_TFS 1
#define SEEK_END_TFS 2
#define SEEK_SET_TFS 0
#define STDIN_OFF 24
#define STDOUT_OFF 28
#define STDERR_OFF 32
#define stdin_TFS (*(FILE_TFS**)((char*)RunningTask + STDIN_OFF))
#define stdout_TFS (*(FILE_TFS**)((char*)RunningTask + STDOUT_OFF))
#define stderr_TFS (*(FILE_TFS**)((char*)RunningTask + STDERR_OFF))

/***********************************************************************/
/* Type Definitions                                                    */
/***********************************************************************/
typedef struct file FILE_TFS;
struct file;

typedef struct {
    unsigned int sect_off; // number of file sectors past first sector
    unsigned int sector;   // absolute sector number
    unsigned int offset;   // byte offset into absolute sector number
} fpos_t_TFS;

/***********************************************************************/
/* Data Declarations                                                   */
/***********************************************************************/
extern struct tcb* RunningTask;

#ifdef __cplusplus
}
#endif
