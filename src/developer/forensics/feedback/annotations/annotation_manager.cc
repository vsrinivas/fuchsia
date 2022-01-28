// Copyright 2022 The Fuchsia Authors.All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/annotations/annotation_manager.h"

#include <lib/syslog/cpp/macros.h>

namespace forensics::feedback {

AnnotationManager::AnnotationManager(Annotations static_annotations)
    : static_annotations_(std::move(static_annotations)) {}

void AnnotationManager::InsertStatic(const Annotations& annotations) {
  for (const auto& [key, value] : annotations) {
    FX_CHECK(static_annotations_.count(key) == 0) << "Attempting to re-insert " << key;
    static_annotations_.insert({key, value});
  }
}

const Annotations& AnnotationManager::ImmediatelyAvailable() const { return static_annotations_; }

}  // namespace forensics::feedback
