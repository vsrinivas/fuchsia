// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_EXCEPTION_BROKER_LIMBO_CLIENT_LIMBO_CLIENT_H_
#define SRC_DEVELOPER_EXCEPTION_BROKER_LIMBO_CLIENT_LIMBO_CLIENT_H_

#include <lib/sys/cpp/component_context.h>

namespace fuchsia {
namespace exception {

class LimboClient {
 public:
  LimboClient(std::shared_ptr<sys::ServiceDirectory> services);

  zx_status_t Init();

  bool active() const { return active_; }

 private:
  std::shared_ptr<sys::ServiceDirectory> services_;

  bool active_ = false;
};

}  // namespace exception
}  // namespace fuchsia

#endif  // SRC_DEVELOPER_EXCEPTION_BROKER_LIMBO_CLIENT_LIMBO_CLIENT_H_
