// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/crash_reports/product.h"

namespace feedback {

bool operator==(const Product& a, const Product& b) {
  return a.name == b.name && a.version == b.version && a.channel == b.channel;
}

}  // namespace feedback
