// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/sanitizer.h>

#include "dynlink.h"

__EXPORT
void __sanitizer_log_write(const char *buffer, size_t len) {
  _dl_log_unlogged();
  _dl_log_write(buffer, len);
}
