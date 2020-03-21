// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/testing/stubs/data_provider.h"

#include <lib/fit/result.h>

namespace feedback {
namespace stubs {

void DataProvider::GetData(GetDataCallback callback) {
  callback(fit::ok(
      std::move(fuchsia::feedback::Data().set_attachment_bundle(std::move(attachment_bundle_)))));
}

}  // namespace stubs
}  // namespace feedback
