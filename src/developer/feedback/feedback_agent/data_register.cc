// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/data_register.h"

namespace feedback {

void DataRegister::Upsert(fuchsia::feedback::ComponentData data, UpsertCallback callback) {
  // TODO(fxb/47368): actually do something with |data|.
  callback();
}

}  // namespace feedback
