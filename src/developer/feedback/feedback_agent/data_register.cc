// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/data_register.h"

#include "src/lib/syslog/cpp/logger.h"

namespace feedback {
namespace {

const char kDefaultNamespace[] = "misc";

}  // namespace

void DataRegister::Upsert(fuchsia::feedback::ComponentData data, UpsertCallback callback) {
  if (!data.has_annotations()) {
    FX_LOGS(WARNING) << "No extra annotations to upsert";
    callback();
    return;
  }

  std::string namespace_;
  if (!data.has_namespace()) {
    FX_LOGS(WARNING) << "No namespace specified, defaulting to " << kDefaultNamespace;
    namespace_ = kDefaultNamespace;
  } else {
    namespace_ = data.namespace_();
  }

  for (const auto& annotation : data.annotations()) {
    namespaced_annotations_[namespace_][annotation.key] = annotation.value;
  }

  callback();
}

}  // namespace feedback
