// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <pthread.h>
#include <zircon/compiler.h>

// Make certain that PTHREAD_MUTEX_INITIALIZER does not use the GNU empty
// initializer extension when compiling for C.
//
// See fxb/64794
//
__UNUSED static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
