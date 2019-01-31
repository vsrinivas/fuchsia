// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "codec-cmd-job.h"
#include "intel-hda-codec.h"

// Instantiate storage for the static allocator.
DECLARE_STATIC_SLAB_ALLOCATOR_STORAGE(::audio::intel_hda::CodecCmdJobAllocTraits, 0x100, true);
