// Copyright 2022 The Fuchsia Authors.All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_PROVIDER_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_PROVIDER_H_

#include <lib/fit/function.h>
#include <lib/fpromise/promise.h>

#include <set>

#include "src/developer/forensics/feedback/annotations/types.h"

namespace forensics::feedback {

// Defines an interface for functionality all annotation providers must implement.
class AnnotationProvider {
 public:
  // Returns the annotation keys a provider will collect.
  virtual std::set<std::string> GetKeys() const = 0;
};

// Collects safe-to-cache annotations asynchronously.
class StaticAsyncAnnotationProvider : public virtual AnnotationProvider {
 public:
  // Returns the annotations this provider collects via |callback|.
  //
  // Note: this method will be called once.
  virtual void GetOnce(::fit::callback<void(Annotations)> callback) = 0;
};

// Collects unsafe-to-cache annotations synchronously.
//
// Note: synchronous calls must be low-cost and return quickly, e.g. not IPC.
class DynamicSyncAnnotationProvider : public virtual AnnotationProvider {
 public:
  // Returns the Annotations from this provider.
  virtual Annotations Get() = 0;
};

// Collects annotations not set by the platform.
class NonPlatformAnnotationProvider : public DynamicSyncAnnotationProvider {
 public:
  std::set<std::string> GetKeys() const override {
    FX_LOGS(FATAL) << "Not implemented";
    return {};
  }

  // Returns true if non-platform annotations are missing.
  virtual bool IsMissingAnnotations() const = 0;
};

// Collects unsafe-to-cache annotations asynchronously.
class DynamicAsyncAnnotationProvider : public virtual AnnotationProvider {
 public:
  // Returns the annotations this provider collects via |callback|.
  virtual void Get(::fit::callback<void(Annotations)> callback) = 0;
};

// Collects safe-to-cache but dynamic annotations asynchronously.
class CachedAsyncAnnotationProvider : public virtual AnnotationProvider {
 public:
  virtual ~CachedAsyncAnnotationProvider() = default;

  // Returns the annotations this provider collects via |callback| when they change.
  //
  // Note: this method will be called once and |callback| invoked each time the annotations change.
  // Additionally, |callback| is expected to return all its annotations, regardless of whether their
  // values changed.
  virtual void GetOnUpdate(::fit::function<void(Annotations)> callback) = 0;
};

}  // namespace forensics::feedback

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_PROVIDER_H_
