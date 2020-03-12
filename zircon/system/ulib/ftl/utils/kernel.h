// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>

// Flags for Semaphore/Queue Mode for Task Queueing
#define OS_FIFO 0

// Flags for Wait-Option
#define WAIT_FOREVER -1

typedef struct scb* SEM; /* Semaphore Control Block */

__BEGIN_CDECLS

// Semaphore Related Routines
SEM semCreate(const char name[8], int init_count, int mode);
void semDelete(SEM* semp);
void semPostBin(SEM sem);
int semPend(SEM sem, int wait_opt);

__END_CDECLS
