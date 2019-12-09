// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_EXCEPTION_BROKER_LIMBO_CLIENT_LIMBO_CLIENT_H_
#define SRC_DEVELOPER_EXCEPTION_BROKER_LIMBO_CLIENT_LIMBO_CLIENT_H_

#include <fuchsia/exception/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>
#include <zircon/syscalls/exception.h>

namespace fuchsia {
namespace exception {

class LimboClient {
 public:
  LimboClient(std::shared_ptr<sys::ServiceDirectory> services);

  zx_status_t Init();

  bool active() const { return active_; }

  zx_status_t SetActive(bool active);

  struct ProcessDescription {
    zx_koid_t process_koid;
    zx_koid_t thread_koid;

    std::string process_name;
    std::string thread_name;

    zx_excp_type_t exception;
  };
  zx_status_t ListProcesses(std::vector<ProcessDescription>* processes);

  zx_status_t GetFilters(std::vector<std::string>* filters);
  zx_status_t AppendFilters(const std::vector<std::string>& filters);

 private:
  std::shared_ptr<sys::ServiceDirectory> services_;

  ProcessLimboSyncPtr connection_;

  bool active_ = false;
};

}  // namespace exception
}  // namespace fuchsia

#endif  // SRC_DEVELOPER_EXCEPTION_BROKER_LIMBO_CLIENT_LIMBO_CLIENT_H_
