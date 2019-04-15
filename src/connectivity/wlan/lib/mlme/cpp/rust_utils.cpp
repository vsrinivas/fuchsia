// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/rust_utils.h>

namespace wlan {

SequenceManager NewSequenceManager() {
  return SequenceManager(mlme_sequence_manager_new(),
                         mlme_sequence_manager_delete);
}

}  // namespace wlan
