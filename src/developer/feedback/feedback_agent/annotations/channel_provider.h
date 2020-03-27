// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ANNOTATIONS_CHANNEL_PROVIDER_H_
#define SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ANNOTATIONS_CHANNEL_PROVIDER_H_

#include <fuchsia/update/channel/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/promise.h>
#include <lib/sys/cpp/service_directory.h>

#include <memory>

#include "src/developer/feedback/feedback_agent/annotations/aliases.h"
#include "src/developer/feedback/feedback_agent/annotations/annotation_provider.h"
#include "src/developer/feedback/utils/bridge.h"
#include "src/developer/feedback/utils/cobalt.h"
#include "src/lib/fxl/macros.h"

namespace feedback {

// Get the name of the current channel.
class ChannelProvider : public AnnotationProvider {
 public:
  // fuchsia.update.channel.Provider is expected to be in |services|.
  ChannelProvider(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
                  zx::duration timeout, Cobalt* cobalt);

  static AnnotationKeys GetSupportedAnnotations();

  fit::promise<Annotations> GetAnnotations() override;

 private:
  async_dispatcher_t* dispatcher_;
  const std::shared_ptr<sys::ServiceDirectory> services_;
  const zx::duration timeout_;
  Cobalt* cobalt_;
};

namespace internal {

// Wraps around fuchsia::update::channel::ProviderPtr to handle establishing the connection, losing
// the connection, waiting for the callback, enforcing a timeout, etc.
//
// GetCurrent() is expected to be called only once.
class ChannelProviderPtr {
 public:
  ChannelProviderPtr(async_dispatcher_t* dispatcher,
                     std::shared_ptr<sys::ServiceDirectory> services, Cobalt* cobalt);

  fit::promise<AnnotationValue> GetCurrent(zx::duration timeout);

 private:
  const std::shared_ptr<sys::ServiceDirectory> services_;
  Cobalt* cobalt_;

  // Enforces the one-shot nature of GetChannel().
  bool has_called_get_current_ = false;

  fuchsia::update::channel::ProviderPtr update_info_;
  Bridge<AnnotationValue> bridge_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ChannelProviderPtr);
};

}  // namespace internal

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ANNOTATIONS_CHANNEL_PROVIDER_H_
