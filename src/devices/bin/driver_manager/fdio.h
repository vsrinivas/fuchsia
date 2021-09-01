// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_FDIO_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_FDIO_H_

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/service/llcpp/service.h>
#include <lib/zx/channel.h>
#include <lib/zx/job.h>

#include <memory>

// clang-format off

// Flags for specifying what should be in a new process's namespace
#define FS_SVC      0x0001
#define FS_DEV      0x0002
#define FS_BOOT     0x0004
#define FS_DATA     0x0010
#define FS_SYSTEM   0x0020
#define FS_BLOB     0x0040
#define FS_VOLUME   0x0080
#define FS_PKGFS    0x0100
#define FS_INSTALL  0x0200
#define FS_TMP      0x0400
#define FS_HUB      0x0800
#define FS_BIN      0x1000
#define FS_DURABLE  0x2000
#define FS_FACTORY  0x4000
#define FS_ALL      0xFFFF

// clang-format on

#define FS_FOR_FSPROC (FS_SVC)

// TODO: we might be able to remove NOREMOTE
#define FS_READONLY_DIR_FLAGS (ZX_FS_RIGHT_READABLE | ZX_FS_FLAG_DIRECTORY)

#define FS_READ_EXEC_DIR_FLAGS (FS_READONLY_DIR_FLAGS | ZX_FS_RIGHT_EXECUTABLE)
#define FS_READ_WRITE_DIR_FLAGS (FS_READONLY_DIR_FLAGS | ZX_FS_RIGHT_WRITABLE)
#define FS_READ_WRITE_EXEC_DIR_FLAGS \
  (FS_READONLY_DIR_FLAGS | ZX_FS_RIGHT_WRITABLE | ZX_FS_RIGHT_EXECUTABLE)

class FsProvider {
  // Pure abstract interface describing how to get a clone of a channel to an fs handle.
 public:
  virtual ~FsProvider();

  // Opens a path relative to locally-specified roots.
  //
  // This acts similar to 'open', but avoids utilizing the local process' namespace.
  // Instead, it manually translates hardcoded paths, such as "svc", "dev", etc into
  // their corresponding root connection, where the request is forwarded.
  //
  // This function is implemented by both devmgr and fshost.
  virtual fidl::ClientEnd<fuchsia_io::Directory> CloneFs(const char* path) = 0;
};

class DevmgrLauncher {
 public:
  explicit DevmgrLauncher(FsProvider* fs_provider);
  // If |executable| is invalid, then argv[0] is used as the path to the binary
  // If |loader| is invalid, the default loader service is used.
  zx_status_t LaunchWithLoader(const zx::job& job, const char* name, zx::vmo executable,
                               zx::channel loader, const char* const* argv,
                               const char** initial_envp, int stdiofd,
                               const zx::resource& root_resource, const zx_handle_t* handles,
                               const uint32_t* types, size_t hcount, zx::process* out_proc,
                               uint32_t flags);
  zx_status_t Launch(const zx::job& job, const char* name, const char* const* argv,
                     const char** envp, int stdiofd, const zx::resource& root_resource,
                     const zx_handle_t* handles, const uint32_t* types, size_t hcount,
                     zx::process* proc_out, uint32_t flags);

 private:
  FsProvider* fs_provider_;
};

// The variable to set on the kernel command line to enable ld.so tracing
// of the processes we launch.
#define LDSO_TRACE_CMDLINE "ldso.trace"
// The env var to set to enable ld.so tracing.
#define LDSO_TRACE_ENV "LD_TRACE=1"

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_FDIO_H_
