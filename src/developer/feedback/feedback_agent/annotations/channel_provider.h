// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be // found in the LICENSE
// file.

#ifndef SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ANNOTATIONS_CHANNEL_PROVIDER_H_
#define SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ANNOTATIONS_CHANNEL_PROVIDER_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/update/channel/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/bridge.h>
#include <lib/fit/promise.h>
#include <lib/sys/cpp/service_directory.h>

#include <set>
#include <string>
#include <vector>

#include "src/developer/feedback/feedback_agent/annotations/annotation_provider.h"
#include "src/lib/fxl/functional/cancelable_callback.h"
#include "src/lib/fxl/macros.h"

namespace feedback {

// Get the name of the current channel.
class ChannelProvider : public AnnotationProvider {
 public:
  // fuchsia.update.channel.Provider is expected to be in |services|.
  ChannelProvider(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
                  zx::duration timeout);

  static std::set<std::string> GetSupportedAnnotations();
  fit::promise<std::vector<fuchsia::feedback::Annotation>> GetAnnotations() override;

 private:
  async_dispatcher_t* dispatcher_;
  const std::shared_ptr<sys::ServiceDirectory> services_;
  const zx::duration timeout_;
};

namespace internal {

// Wraps around fuchsia::update::channel::ProviderPtr to handle establishing the connection, losing
// the connection, waiting for the callback, enforcing a timeout, etc.
//
// GetCurrent() is expected to be called only once.
class ChannelProviderPtr {
 public:
  ChannelProviderPtr(async_dispatcher_t* dispatcher,
                     std::shared_ptr<sys::ServiceDirectory> services);

  fit::promise<std::string> GetCurrent(zx::duration timeout);

 private:
  async_dispatcher_t* dispatcher_;
  const std::shared_ptr<sys::ServiceDirectory> services_;
  // Enforces the one-shot nature of GetChannel().
  bool has_called_get_current_ = false;

  fuchsia::update::channel::ProviderPtr update_info_;
  fit::bridge<std::string> done_;
  // We wrap the delayed task we post on the async loop to timeout in a CancelableClosure so we can
  // cancel it if we are done another way.
  fxl::CancelableClosure done_after_timeout_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ChannelProviderPtr);
};

}  // namespace internal

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ANNOTATIONS_CHANNEL_PROVIDER_H_
