// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/maxwell/lib/async/future_value.h"
#include "apps/maxwell/services/context/context_writer.fidl.h"
#include "apps/maxwell/services/context/value.fidl.h"

namespace maxwell {

// Encapsulates one value in the Context Engine. Exposes to its client a simple
// "set state" interface. When it is destroyed, also deletes the corresponding
// value in Context.
//
// Usage:
//
// ScopedContextValue value(writer_ptr.get());
//
// ContextValuePtr value = ...;  // Make a value.
// value.Set(std::move(value));
//
// ... any time later ...
// value.Set(std::move(new_updated_value));
class ScopedContextValue {
 public:
  explicit ScopedContextValue(ContextWriter* writer);
  ScopedContextValue(ContextWriter* writer, const fidl::String& parent_id);
  ScopedContextValue(ScopedContextValue&& o);
  ~ScopedContextValue();

  // Creates a new, or updates the existing, value in the Context Engine. The
  // value will not exist at all and cannot be referenced until this is called
  // at least once.
  void Set(ContextValuePtr new_value);

  // Updates only the metadata part of the ContextValue. Cannot be called
  // unless Set() has been called at least once.
  void UpdateMetadata(ContextMetadataPtr new_metadata);

  // Enqueues |fn| on the runloop when |value_id_| is available.
  void OnId(FutureValue<fidl::String>::UseValueFunction fn);

 private:
  ContextWriter* writer_;
  const fidl::String parent_id_;
  ContextValuePtr value_;
  FutureValue<fidl::String> value_id_;
};

}  // namespace maxwell