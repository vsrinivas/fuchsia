// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include <region-alloc/region-alloc.h>

RegionAllocator* Get32BitMmioAllocator();
RegionAllocator* Get64BitMmioAllocator();
RegionAllocator* GetIoAllocator();
