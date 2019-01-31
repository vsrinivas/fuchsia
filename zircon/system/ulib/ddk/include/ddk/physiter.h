// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// Redirect ddk/physiter.h to ddk/phys-iter.h.
// We need to do this because banjo can't handle dashes in file names.
#include <ddk/phys-iter.h>
