// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FSHOST_FDIO_H_
#define SRC_STORAGE_FSHOST_FDIO_H_

#include <lib/zx/channel.h>
#include <lib/zx/job.h>

#include <memory>

namespace fshost {

// Launch a binary using fdio_spawn_etc, populating the process args with the list of handles
// provided, as well as some other environmental setup, like providing /svc.
zx_status_t Launch(const zx::job& job, const char* name, const char* const* argv, const char** envp,
                   int stdiofd, const zx::resource& root_resource, const zx_handle_t* handles,
                   const uint32_t* types, size_t hcount, zx::process* out_proc);

// Returns the result of splitting |args| into an argument vector.
class ArgumentVector {
 public:
  static ArgumentVector FromCmdline(const char* cmdline);

  // Returns a nullptr-terminated list of arguments.  Only valid for the
  // lifetime of |this|.
  const char* const* argv() const { return argv_; }

 private:
  ArgumentVector() = default;

  static constexpr size_t kMaxArgs = 8;
  const char* argv_[kMaxArgs + 1];
  std::unique_ptr<char[]> raw_bytes_;
};

std::ostream& operator<<(std::ostream& stream, const ArgumentVector& arguments);

// The variable to set on the kernel command line to enable ld.so tracing
// of the processes we launch.
#define LDSO_TRACE_CMDLINE "ldso.trace"
// The env var to set to enable ld.so tracing.
#define LDSO_TRACE_ENV "LD_TRACE=1"

}  // namespace fshost

#endif  // SRC_STORAGE_FSHOST_FDIO_H_
