// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <semaphore.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <zircon/assert.h>

#include "kernel.h"

// Gets a semaphore token.
//
// Inputs: sem = pointer to semaphore control block.
//         wait_opt = 0 (no wait), -1 (wait forever), or positive timeout count.
//
// Returns: 0 if successful, else -1 with errno set to error code.
int semPend(SEM sem, int wait_opt) {
  ZX_DEBUG_ASSERT(wait_opt == WAIT_FOREVER);
  return sem_wait(reinterpret_cast<sem_t*>(sem));
}

// Returns a semaphore token, ensures not already released.
void semPostBin(SEM sem) { sem_post(reinterpret_cast<sem_t*>(sem)); }

// Creates and initialize semaphore.
//
// Inputs: name = ASCII string of semaphore name.
//         count = initial semaphore count.
//         mode = task queuing mode: OS_FIFO (unused).
//
// Returns: ID of new semaphore, or NULL if error
SEM semCreate(const char name[8], int init_count, int mode) {
  sem_t* semp = new sem_t;
  if (sem_init(semp, 0, init_count) != 0) {
    delete semp;
    return NULL;
  }
  return reinterpret_cast<SEM>(semp);
}

// Deletes specified semaphore, freeing its control block and any pending tasks.
void semDelete(SEM* semp) {
  sem_t* sem = reinterpret_cast<sem_t*>(*semp);
  sem_destroy(sem);
  delete sem;
  *semp = nullptr;
}
