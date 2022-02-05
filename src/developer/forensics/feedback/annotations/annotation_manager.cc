// Copyright 2022 The Fuchsia Authors.All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/annotations/annotation_manager.h"

#include <lib/syslog/cpp/macros.h>

#include <map>

namespace forensics::feedback {
namespace {

void InsertUnique(const Annotations& annotations, const std::set<std::string>& allowlist,
                  Annotations* out) {
  for (const auto& [k, v] : annotations) {
    if (allowlist.count(k) != 0) {
      FX_CHECK(out->count(k) == 0) << "Attempting to re-insert " << k;
      out->insert({k, v});
    }
  }
}

void InsertUnique(const Annotations& annotations, Annotations* out) {
  for (const auto& [k, v] : annotations) {
    FX_CHECK(out->count(k) == 0) << "Attempting to re-insert " << k;
    out->insert({k, v});
  }
}

}  // namespace

AnnotationManager::AnnotationManager(
    std::set<std::string> allowlist, const Annotations static_annotations,
    NonPlatformAnnotationProvider* non_platform_provider,
    std::vector<DynamicSyncAnnotationProvider*> dynamic_sync_providers)
    : allowlist_(std::move(allowlist)),
      static_annotations_(),
      non_platform_provider_(non_platform_provider),
      dynamic_sync_providers_(std::move(dynamic_sync_providers)) {
  InsertUnique(static_annotations, allowlist_, &static_annotations_);
}

void AnnotationManager::InsertStatic(const Annotations& annotations) {
  InsertUnique(annotations, allowlist_, &static_annotations_);
}

Annotations AnnotationManager::ImmediatelyAvailable() const {
  Annotations annotations(static_annotations_);
  for (auto* provider : dynamic_sync_providers_) {
    InsertUnique(provider->Get(), allowlist_, &annotations);
  }

  if (non_platform_provider_ != nullptr) {
    InsertUnique(non_platform_provider_->Get(), &annotations);
  }

  return annotations;
}

bool AnnotationManager::IsMissingNonPlatformAnnotations() const {
  return (non_platform_provider_ == nullptr) ? false
                                             : non_platform_provider_->IsMissingAnnotations();
}

}  // namespace forensics::feedback
