// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ANNOTATIONS_BOARD_INFO_PROVIDER_H_
#define SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ANNOTATIONS_BOARD_INFO_PROVIDER_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/hwinfo/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/bridge.h>
#include <lib/fit/promise.h>
#include <lib/sys/cpp/service_directory.h>
#include <zircon/time.h>

#include <map>
#include <set>
#include <string>

#include "src/developer/feedback/feedback_agent/annotations/annotation_provider.h"
#include "src/lib/fxl/functional/cancelable_callback.h"
#include "src/lib/fxl/macros.h"

namespace feedback {

// Get the requested parts of fuchsia.hwinfo.BoardInfo as annotations.
class BoardInfoProvider : public AnnotationProvider {
 public:
  // fuchsia.hwinfo.Board is expected to be in |services|.
  BoardInfoProvider(const std::set<std::string>& annotations_to_get, async_dispatcher_t* dispatcher,
                    std::shared_ptr<sys::ServiceDirectory> services, zx::duration timeout);

  static std::set<std::string> GetSupportedAnnotations();
  fit::promise<std::vector<fuchsia::feedback::Annotation>> GetAnnotations() override;

 private:
  std::set<std::string> annotations_to_get_;
  async_dispatcher_t* dispatcher_;
  const std::shared_ptr<sys::ServiceDirectory> services_;
  const zx::duration timeout_;
};

namespace internal {

// Wraps around fuchsia::hwinfo::BoardPtr to handle establishing the connection, losing
// the connection, waiting for the callback, enforcing a timeout, etc.
//
// Will ever only make one call to fuchsia::hwinfo::Board::GetInfo.
class BoardInfoPtr {
 public:
  BoardInfoPtr(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services);

  fit::promise<std::map<std::string, std::string>> GetBoardInfo(zx::duration timeout);

 private:
  async_dispatcher_t* dispatcher_;
  const std::shared_ptr<sys::ServiceDirectory> services_;
  // Enforces the one-shot nature of GetBoardInfo().
  bool has_called_get_board_info_ = false;

  fuchsia::hwinfo::BoardPtr board_ptr_;
  fit::bridge<std::map<std::string, std::string>> done_;

  // We wrap the delayed task we post on the async loop to timeout in a CancelableClosure so we can
  // cancel it if we are done another way.
  fxl::CancelableClosure done_after_timeout_;

  FXL_DISALLOW_COPY_AND_ASSIGN(BoardInfoPtr);
};

}  // namespace internal
}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ANNOTATIONS_BOARD_INFO_PROVIDER_H_
