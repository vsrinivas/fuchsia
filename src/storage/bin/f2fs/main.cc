// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/limits.h>
#include <lib/fdio/vfs.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/inspect/cpp/component.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/channel.h>

#include <fstream>
#include <iostream>
#include <string>

#include <block-client/cpp/block-device.h>
#include <block-client/cpp/remote-block-device.h>
#include <fbl/unique_fd.h>

#include "src/storage/f2fs/f2fs.h"

int main(int argc, char** argv) {
#ifdef F2FS_BU_DEBUG
  std::cout << "f2fs arg= ";

  for (int i = 0; i < argc; i++) {
    std::cout << argv[i] << " ";
  }

  std::cout << std::endl;
#endif

  if (argc <= 1) {
    fprintf(stderr, "usage: f2fs mkfs [ <options>*] devicepath f2fs\n");
    fprintf(stderr, "usage: f2fs fsck [ <options>*] devicepath f2fs\n");
    fprintf(stderr, "usage: f2fs mount [ <options>*] devicepath f2fs\n");
    fprintf(stderr, "usage: f2fs fsync filename\n");
    return 0;
  }

  // TODO: remove it after unittest impl.
  if (!strcmp(argv[1], "fsync")) {
    if (argc != 3) {
      fprintf(stderr, "usage: fsync filename\n");
      return EXIT_FAILURE;
    }
    fbl::unique_fd fd(open(argv[2], O_RDONLY));
    if (!fd.get()) {
      std::cout << "f2fs: cannot open " << argv[2] << std::endl;
      return EXIT_FAILURE;
    }
    fsync(fd.get());
    close(fd.release());
    return 0;
  }

  // Block device passed by handle
  zx::channel device_channel = zx::channel(zx_take_startup_handle(FS_HANDLE_BLOCK_DEVICE_ID));

  std::unique_ptr<block_client::RemoteBlockDevice> device;
  zx_status_t status = block_client::RemoteBlockDevice::Create(std::move(device_channel), &device);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Could not access block device";
    return EXIT_FAILURE;
  }

  std::unique_ptr<f2fs::Bcache> bc;
  bool readonly_device = false;

  if (f2fs::CreateBcache(std::move(device), &readonly_device, &bc) != ZX_OK) {
    FX_LOGS(ERROR) << "f2fs: error: cannot create block cache";
    return EXIT_FAILURE;
  }

  if (!strcmp(argv[1], "mkfs")) {
    f2fs::MkfsOptions mkfs_options;
    if (f2fs::ParseOptions(argc, argv, mkfs_options) != ZX_OK) {
      return EXIT_FAILURE;
    }
    if (f2fs::Mkfs(mkfs_options, std::move(bc)).is_error())
      return EXIT_FAILURE;
  } else if (!strcmp(argv[1], "fsck")) {
    f2fs::Fsck(bc.get());
  } else if (!strcmp(argv[1], "mount")) {
    f2fs::MountOptions mount_options;
    f2fs::Mount(mount_options, std::move(bc));
  } else {
    FX_LOGS(ERROR) << "unknown operation:" << argv[1];
  }

  return EXIT_SUCCESS;
}
