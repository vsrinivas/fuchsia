// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_EXTRACT_RESOURCE_ON_DESTRUCTION_H_
#define LIB_FIDL_LLCPP_EXTRACT_RESOURCE_ON_DESTRUCTION_H_

#include <lib/fit/optional.h>
#include <lib/sync/completion.h>
#include <lib/zx/channel.h>
#include <zircon/assert.h>

#include <memory>

namespace fidl {
namespace internal {

// Wraps some value that can be optionally moved out of the containing object
// during destruction. See |DestroyAndExtract| for more rationale.
// |Resource| is the type of the value, which is generally some resource
// (e.g. channel).
template <typename Resource>
class ExtractedOnDestruction {
 public:
  explicit ExtractedOnDestruction(Resource resource) : resource_(std::move(resource)) {}

  ~ExtractedOnDestruction() {
    if (receiver_on_destruction_) {
      *receiver_on_destruction_ = fit::optional(std::move(resource_));
    }
    if (on_destruction_completion_) {
      sync_completion_signal(on_destruction_completion_);
    }
  }

  Resource& get() { return resource_; }
  const Resource& get() const { return resource_; }

 private:
  template <typename Container, typename ResourceType, typename Callback>
  friend void DestroyAndExtract(std::shared_ptr<Container>&& object,
                                ExtractedOnDestruction<ResourceType> Container::*member_path,
                                Callback callback);

  Resource resource_;
  fit::optional<Resource>* receiver_on_destruction_ = nullptr;
  sync_completion_t* on_destruction_completion_ = nullptr;
};

// Blocks until there are no other live references to the pointee of |object|,
// then extracts the field within it indexed by |member_path| during
// destruction, and finally returns it by passing it to |callback|.
//
// In a multi-threaded system, teardown can be safely arranged through the use
// of |std::shared_ptr<T>|: the last strong reference owner is responsible
// for destroying the object, regardless of which thread. However, we would
// often like to observe the destruction of this object, and extract important
// resource within it, on some specific thread. For example, a server binding
// object may be destroyed on any thread, but the "on-unbound" handler should
// always run from the dispatcher thread, and need to extract the channel within
// the server binding as it is being destructed.
//
// That extraction can be safely implemented by declaring a local resource
// variable in the observing thread, and storing its address inside the object
// to be destructed, such that the destructor has the opportunity to move out
// that resource into the local variable on the observing thread.
//
// This function implements this general behavior of the observing thread.
// |Container| is the type of the object that will be destructed, while
// |member_path| is a pointer-to-member from a |Container| type to a
// |ExtractedOnDestruction<Resource>| field. The caller should ensure that there
// are no other long-living strong references to |object|, then move its own
// strong reference into this function, which will trigger the destruction.
template <typename Container, typename Resource, typename Callback>
void DestroyAndExtract(std::shared_ptr<Container>&& object,
                       ExtractedOnDestruction<Resource> Container::*member_path,
                       Callback callback) {
  ExtractedOnDestruction<Resource>* member = &(object.get()->*member_path);
  sync_completion_t on_destruction;
  member->on_destruction_completion_ = &on_destruction;

  // Using an optional accommodates |Resource| types which do not
  // have a default constructor.
  fit::optional<Resource> result;
  member->receiver_on_destruction_ = &result;

  // Trigger the destruction of |object|.
  object.reset();

  zx_status_t status = sync_completion_wait(&on_destruction, ZX_TIME_INFINITE);
  ZX_ASSERT_MSG(status == ZX_OK, "Error waiting for object destruction.\n");

  callback(std::move(result.value()));
}

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_EXTRACT_RESOURCE_ON_DESTRUCTION_H_
