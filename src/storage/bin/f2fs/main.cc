// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <getopt.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>

#include "src/lib/storage/block_client/cpp/remote_block_device.h"
#include "src/storage/f2fs/f2fs.h"

namespace {

const char* kComponentCommand = "component";

// Run the filesystem server on top of the block device |device|.
// This function blocks until the filesystem server is instructed to exit.
int Mount(const f2fs::MountOptions& options, std::unique_ptr<f2fs::Bcache> bc) {
  fidl::ServerEnd<fuchsia_io::Directory> root(
      zx::channel(zx_take_startup_handle(PA_DIRECTORY_REQUEST)));

  zx::result status = f2fs::Mount(options, std::move(bc), std::move(root));
  if (status.is_error()) {
    FX_LOGS(ERROR) << "failed to mount: " << status.status_string();
    return EXIT_FAILURE;
  }
  return 0;
}

int Fsck(const f2fs::MountOptions& mount_options, std::unique_ptr<f2fs::Bcache> bc) {
  uint32_t readonly;
  if (mount_options.GetValue(f2fs::kOptReadOnly, &readonly) != ZX_OK) {
    return EXIT_FAILURE;
  }
  return f2fs::Fsck(std::move(bc), f2fs::FsckOptions{.repair = !readonly});
}

int StartComponent(const f2fs::MountOptions& options, std::unique_ptr<f2fs::Bcache> bc) {
  FX_LOGS(INFO) << "start a component";

  // The arguments are either null or don't matter, we collect the real ones later on the startup
  // protocol. What does matter is the DIRECTORY_REQUEST so we can start serving that protocol.
  zx::channel outgoing_server = zx::channel(zx_take_startup_handle(PA_DIRECTORY_REQUEST));
  if (!outgoing_server.is_valid()) {
    FX_LOGS(ERROR) << "PA_DIRECTORY_REQUEST startup handle is required.";
    return EXIT_FAILURE;
  }
  fidl::ServerEnd<fuchsia_io::Directory> outgoing_dir(std::move(outgoing_server));

  zx::channel lifecycle_channel = zx::channel(zx_take_startup_handle(PA_LIFECYCLE));
  if (!lifecycle_channel.is_valid()) {
    FX_LOGS(ERROR) << "PA_LIFECYCLE startup handle is required.";
    return EXIT_FAILURE;
  }
  fidl::ServerEnd<fuchsia_process_lifecycle::Lifecycle> lifecycle_request(
      std::move(lifecycle_channel));

  zx::result status = f2fs::StartComponent(std::move(outgoing_dir), std::move(lifecycle_request));
  if (status.is_error()) {
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

struct Command {
  const char* name;
  std::function<int(const f2fs::MountOptions&, std::unique_ptr<f2fs::Bcache>)> func;
  const char* help;
};

int usage(const std::vector<Command>& commands) {
  fprintf(stderr, "usage: f2fs mkfs [ <options>*] devicepath \n");
  fprintf(stderr, "usage: f2fs fsck [ <options>*] devicepath \n");
  fprintf(stderr, "usage: f2fs mount [ <options>*] devicepath directory\n");
  return EXIT_FAILURE;
}

}  // namespace

int main(int argc, char** argv) {
  syslog::SetLogSettings({}, {"f2fs"});
  f2fs::MountOptions options;
  const std::vector<Command> commands = {
      Command{kComponentCommand, StartComponent, "start a f2fs component"},
      Command{"create",
              [argc, argv](const f2fs::MountOptions& options, std::unique_ptr<f2fs::Bcache> bc) {
                f2fs::MkfsOptions mkfs_options;
                if (f2fs::ParseOptions(argc, argv, mkfs_options) != ZX_OK) {
                  return EXIT_FAILURE;
                }
                return f2fs::Mkfs(mkfs_options, std::move(bc)).status_value();
              },
              "initialize filesystem"},
      Command{"mkfs",
              [argc, argv](const f2fs::MountOptions& options, std::unique_ptr<f2fs::Bcache> bc) {
                f2fs::MkfsOptions mkfs_options;
                if (f2fs::ParseOptions(argc, argv, mkfs_options) != ZX_OK) {
                  return EXIT_FAILURE;
                }
                return f2fs::Mkfs(mkfs_options, std::move(bc)).status_value();
              },
              "initialize filesystem"},
      Command{"check", Fsck, "check filesystem integrity"},
      Command{"fsck", Fsck, "check filesystem integrity"},
      Command{"mount",
              [](const f2fs::MountOptions& options, std::unique_ptr<f2fs::Bcache> bc) {
                return Mount(options, std::move(bc));
              },
              "mount and serve the filesystem"}};

  if (argc <= 1) {
    return usage(commands);
  }

  // TODO: Parse options.

  char* cmd = argv[1];
  for (const auto& command : commands) {
    if (strcmp(cmd, command.name) == 0) {
      std::unique_ptr<f2fs::Bcache> bc;
      bool readonly_device = false;
      if (strcmp(cmd, kComponentCommand) != 0) {
        FX_LOGS(INFO) << "start a f2fs process";
        // If we aren't being launched as a component, we are getting the block device as a startup
        // handle. Get it and create the bcache.
        zx::channel device_channel = zx::channel(zx_take_startup_handle(FS_HANDLE_BLOCK_DEVICE_ID));
        auto bc_or = f2fs::CreateBcache(std::move(device_channel), &readonly_device);
        if (bc_or.is_error()) {
          return EXIT_FAILURE;
        }
        bc = std::move(*bc_or);
      }

      if (readonly_device) {
        options.SetValue(options.GetNameView(f2fs::kOptReadOnly), 1);
      }

      return command.func(options, std::move(bc));
    }
  }
  return usage(commands);
}
