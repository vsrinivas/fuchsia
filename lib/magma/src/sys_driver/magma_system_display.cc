// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_system_display.h"

MagmaSystemDisplay::MagmaSystemDisplay(Owner* owner)
    : MagmaSystemBufferManager(owner), owner_(owner)

{
    DASSERT(owner_);
    magic_ = kMagic;
}