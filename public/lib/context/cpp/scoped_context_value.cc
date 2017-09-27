// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/context/cpp/scoped_context_value.h"
#include "garnet/public/lib/fxl/functional/make_copyable.h"

namespace maxwell {

ScopedContextValue::ScopedContextValue(ContextWriter* writer)
    : writer_(writer) {
  FXL_CHECK(writer_ != nullptr);
}

ScopedContextValue::ScopedContextValue(ContextWriter* writer,
                                       const fidl::String& parent_id)
    : writer_(writer), parent_id_(parent_id) {
  FXL_CHECK(writer_ != nullptr);
}

ScopedContextValue::ScopedContextValue(ScopedContextValue&& o)
    : writer_(o.writer_),
      parent_id_(o.parent_id_),
      value_(std::move(o.value_)),
      value_id_(std::move(o.value_id_)) {
  FXL_CHECK(writer_ != nullptr);
  // This signals to the destructor of |o| not to try to delete the value, since
  // we have taken ownership of that responsibility.
  o.writer_ = nullptr;
}

ScopedContextValue::~ScopedContextValue() {
  if (!writer_) {
    // See the move constructor above.
    return;
  }

  if (!value_) {
    // Nobody set a value yet, so there's nothing to delete.
  }

  if (!value_id_) {
    // TODO(thatguy): Only solution I can think of to avoid this race condition
    // is to change the ContextWriter FIDL interface to hand back a service per
    // value that looks like this class (provides some set-state operations),
    // and itself removes the value when the channel closes.
    //
    // For now, this is very unlikely to happen.
    FXL_LOG(WARNING) << "ScopedContextValue going out of scope after a value "
                        "was set but before getting the value_id back. Cannot "
                        "safely issue a delete request, so the value is "
                        "orphaned.";
    return;
  }

  // Depsite the fact that we're giving |value_id_| a closure, we know it will
  // queue it on the run loop immediately since above we checked if |value_id_|
  // was valid.
  value_id_.OnValue([writer = writer_](const fidl::String& value_id) {
    writer->Remove(value_id);
  });
}

void ScopedContextValue::Set(ContextValuePtr new_value) {
  // The value cannot be null. We condition on a null value to mean that we
  // haven't sent a request to writer_ to add the value yet.
  FXL_CHECK(new_value);

  if (!value_) {
    if (parent_id_) {
      writer_->AddChildValue(
          parent_id_, new_value.Clone(),
          [this](const fidl::String& value_id) { value_id_ = value_id; });
    } else {
      writer_->AddValue(
          new_value.Clone(),
          [this](const fidl::String& value_id) { value_id_ = value_id; });
    }
  } else {
    value_id_.OnValue(fxl::MakeCopyable([
      v = new_value.Clone(), writer = writer_
    ](const fidl::String& id) mutable { writer->Update(id, std::move(v)); }));
  }
  value_ = std::move(new_value);
}

void ScopedContextValue::UpdateMetadata(ContextMetadataPtr new_metadata) {
  FXL_CHECK(value_) << "Cannot call UpateMetadata() until Set() has been "
                       "called at least once.";
  auto new_value = value_.Clone();
  new_value->meta = std::move(new_metadata);
  Set(std::move(new_value));
}

void ScopedContextValue::OnId(FutureValue<fidl::String>::UseValueFunction fn) {
  value_id_.OnValue(std::move(fn));
}

}  // namespace maxwell
