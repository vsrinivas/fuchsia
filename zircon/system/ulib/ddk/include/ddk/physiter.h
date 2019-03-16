// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DDK_PHYSITER_H_
#define DDK_PHYSITER_H_

// Redirect ddk/physiter.h to ddk/phys-iter.h.
// We need to do this because banjo can't handle dashes in file names.
#include <ddk/phys-iter.h>

#endif  // DDK_PHYSITER_H_
