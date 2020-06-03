// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/test/capturer_shim.h"

namespace media::audio::test {

namespace internal {
size_t capturer_shim_next_inspect_id = 1;  // ids start at 1
}  // namespace internal

}  // namespace media::audio::test
