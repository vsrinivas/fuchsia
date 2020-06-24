// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/data_register.h"

#include <lib/syslog/cpp/macros.h>

#include "src/developer/forensics/feedback_data/annotations/types.h"
#include "src/developer/forensics/feedback_data/constants.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace forensics {
namespace feedback_data {
namespace {

const char kDefaultNamespace[] = "misc";

const char kNamespaceSeparator[] = ".";

Annotations Flatten(const std::map<std::string, Annotations>& namespaced_annotations) {
  Annotations flat_annotations;
  for (const auto& [namespace_, annotations] : namespaced_annotations) {
    for (const auto& [k, v] : annotations) {
      flat_annotations.insert({namespace_ + kNamespaceSeparator + k, v});
    }
  }
  return flat_annotations;
}

}  // namespace

DataRegister::DataRegister(Datastore* datastore) : datastore_(datastore) { FX_CHECK(datastore_); }

void DataRegister::Upsert(fuchsia::feedback::ComponentData data, UpsertCallback callback) {
  if (!data.has_annotations()) {
    FX_LOGS(WARNING) << "No non-platform annotations to upsert";
    callback();
    return;
  }

  std::string namespace_;
  if (!data.has_namespace()) {
    FX_LOGS(WARNING) << "No namespace specified, defaulting to " << kDefaultNamespace;
    namespace_ = kDefaultNamespace;
  } else if (kReservedAnnotationNamespaces.find(data.namespace_()) !=
             kReservedAnnotationNamespaces.end()) {
    FX_LOGS(WARNING) << fxl::StringPrintf(
        "Ignoring non-platform annotations, %s is a reserved namespace", data.namespace_().c_str());
    // TODO(fxb/48664): close connection with ZX_ERR_INVALID_ARGS instead.
    callback();
    return;
  } else {
    namespace_ = data.namespace_();
  }

  for (const auto& annotation : data.annotations()) {
    namespaced_annotations_[namespace_].insert_or_assign(annotation.key,
                                                         AnnotationOr(annotation.value));
  }
  // TODO(fxb/48666): close all connections if false.
  datastore_->TrySetNonPlatformAnnotations(Flatten(namespaced_annotations_));

  callback();
}

}  // namespace feedback_data
}  // namespace forensics
