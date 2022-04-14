// Copyright 2022 The Fuchsia Authors.All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_ANNOTATION_MANAGER_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_ANNOTATION_MANAGER_H_

#include <lib/async/dispatcher.h>
#include <lib/fpromise/promise.h>
#include <lib/zx/time.h>

#include <vector>

#include "src/developer/forensics/feedback/annotations/provider.h"
#include "src/developer/forensics/feedback/annotations/types.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace forensics::feedback {

// Responsible for the storage and collection of annotations.
//
// Note: inserted annotations must be unique and a check-fail will occur if keys intersect.
class AnnotationManager {
 public:
  // Allowlist dictates which messages can be extracted from the manager's interface.
  //
  // Annotations not in the allowlist or not explicitly exempted won't be returned.
  AnnotationManager(async_dispatcher_t* dispatcher, std::set<std::string> allowlist,
                    Annotations static_annotations = {},
                    NonPlatformAnnotationProvider* non_platform_provider = nullptr,
                    std::vector<DynamicSyncAnnotationProvider*> dynamic_sync_providers = {},
                    std::vector<StaticAsyncAnnotationProvider*> static_async_providers = {});

  // Returns all annotations collected by the manager in a promise that is guaranteed to complete
  // before |timeout| expires.
  //
  // Note: currently only immediately available annotations are returned.
  ::fpromise::promise<Annotations> GetAll(zx::duration timeout);

  // Returns the annotations that are immediately available regardless of whether they're static or
  // dynamic.
  //
  // This is useful when annotations can't be waited, e.g. component startup /
  // shutdown, and the annotation aggregate data that are ready to be used because it never changes,
  // is available in a short amount of time, or is cached.
  Annotations ImmediatelyAvailable() const;

  // Returns true if the non-platform annotation provider is missing annotations.
  bool IsMissingNonPlatformAnnotations() const;

  // Inserts static, synchronous annotations.
  void InsertStatic(const Annotations& annotations);

 private:
  async_dispatcher_t* dispatcher_;

  std::set<std::string> allowlist_;
  Annotations static_annotations_;
  NonPlatformAnnotationProvider* non_platform_provider_;
  std::vector<DynamicSyncAnnotationProvider*> dynamic_sync_providers_;
  std::vector<StaticAsyncAnnotationProvider*> static_async_providers_;

  // Calls to GetAll that have not yet completed.
  std::vector<std::function<void()>> waiting_for_static_;

  fxl::WeakPtrFactory<AnnotationManager> ptr_factory_{this};
};

}  // namespace forensics::feedback

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_ANNOTATION_MANAGER_H_
