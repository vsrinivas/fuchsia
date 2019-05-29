// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>
#include <string>

#include <lib/zx/job.h>
#include <lib/zx/process.h>

#include "lib/fsl/tasks/fd_waiter.h"

namespace sshd_host {

constexpr zx_rights_t kChildJobRights =
    ZX_RIGHTS_BASIC | ZX_RIGHTS_IO | ZX_RIGHT_DESTROY | ZX_RIGHT_MANAGE_JOB;

zx_status_t make_child_job(const zx::job& parent, std::string name,
                           zx::job* job);

// Service relies on the default async dispatcher and is not thread safe.
class Service {
 public:
  explicit Service(int port);
  ~Service();

 private:
  void Wait();
  void Launch(int conn, const std::string& peer_name);
  void ProcessTerminated(zx::process process, zx::job job);

  int port_;
  int sock_;
  fsl::FDWaiter waiter_;
  zx::job job_;

  std::vector<std::unique_ptr<async::Wait>> process_waiters_;
};

}  // namespace sshd_host
