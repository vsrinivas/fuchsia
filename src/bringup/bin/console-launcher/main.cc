// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.boot/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/namespace.h>
#include <lib/fdio/spawn.h>
#include <lib/fit/defer.h>
#include <lib/service/llcpp/service.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/debuglog.h>
#include <lib/zx/process.h>
#include <lib/zx/time.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <algorithm>
#include <future>
#include <utility>

#include <fbl/ref_ptr.h>

#include "src/bringup/bin/console-launcher/console_launcher.h"
#include "src/bringup/bin/console-launcher/virtcon-setup.h"
#include "src/lib/fxl/strings/split_string.h"
#include "src/lib/storage/vfs/cpp/managed_vfs.h"
#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/remote_dir.h"
#include "src/lib/storage/vfs/cpp/vfs_types.h"
#include "src/sys/lib/stdout-to-debuglog/cpp/stdout-to-debuglog.h"

namespace {

template <typename F>
class EventHandler : public fidl::WireSyncEventHandler<fuchsia_io::Directory> {
 public:
  explicit EventHandler(F fn) : fn_(std::move(fn)) {}

  void OnOpen(fidl::WireEvent<fuchsia_io::Directory::OnOpen>* event) override { fn_(event); }

  zx_status_t Unknown() override { return ZX_OK; }

 private:
  F fn_;
};

std::ostream& operator<<(std::ostream& os, const std::vector<std::string>& args) {
  for (size_t i = 0; i < args.size(); ++i) {
    if (i != 0) {
      os << ' ';
    }
    os << args[i];
  }
  return os;
}

}  // namespace

int main(int argv, char** argc) {
  syslog::SetTags({"console-launcher"});

  if (zx_status_t status = StdoutToDebuglog::Init(); status != ZX_OK) {
    FX_PLOGS(ERROR, status)
        << "failed to redirect stdout to debuglog, assuming test environment and continuing";
  }

  FX_LOGS(INFO) << "running";

  zx::status boot_args = service::Connect<fuchsia_boot::Arguments>();
  if (boot_args.is_error()) {
    FX_PLOGS(FATAL, boot_args.status_value())
        << "failed to connect to " << fidl::DiscoverableProtocolName<fuchsia_boot::Arguments>;
  }

  zx::status args = console_launcher::GetArguments(boot_args.value());
  if (args.is_error()) {
    FX_PLOGS(FATAL, args.status_value()) << "failed to get arguments";
  }

  if (zx_status_t status = console_launcher::SetupVirtcon(boot_args.value()); status != ZX_OK) {
    // If launching virtcon fails, we still should continue so that the autorun programs
    // and serial console are launched.
    FX_PLOGS(ERROR, status) << "failed to set up virtcon";
  }

  if (!args->run_shell) {
    if (!args->autorun_boot.empty()) {
      FX_LOGS(ERROR) << "cannot launch autorun command '" << args->autorun_boot << "'";
    }
    FX_LOGS(INFO) << "console.shell: disabled";
    // TODO(https://fxbug.dev/97657): Hang around. If we exit before archivist has started, our logs
    // will be lost, and this log is load bearing in shell_disabled_test.
    std::promise<void>().get_future().wait();
    return 0;
  }
  FX_LOGS(INFO) << "console.shell: enabled";

  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  async_dispatcher_t* dispatcher = loop.dispatcher();
  fbl::RefPtr root = fbl::MakeRefCounted<fs::PseudoDir>();

  std::unordered_map<std::string_view, std::thread> threads;
  fdio_flat_namespace_t* flat;
  if (zx_status_t status = fdio_ns_export_root(&flat); status != ZX_OK) {
    FX_PLOGS(FATAL, status) << "failed to get namespace root";
  }
  auto free_flat = fit::defer([&flat]() { fdio_ns_free_flat_ns(flat); });

  // Our incoming namespace contains directories provided by fshost that may not
  // yet be responding to requests. This is ordinarily fine, but can cause
  // indefinite hangs in an interactive shell when storage devices fail to
  // start.
  //
  // Rather than expose these directly to the shell, indirect through a local
  // VFS to which entries are added only once they are seen to be servicing
  // requests. This causes the shell to initially observe an empty root
  // directory to which entries are added once they are ready for blocking
  // operations.
  for (size_t i = 0; i < flat->count; ++i) {
    zx::status endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
    if (endpoints.is_error()) {
      FX_PLOGS(FATAL, endpoints.status_value()) << "failed to create endpoints";
    }

    std::string_view path = flat->path[i];

    const fidl::WireResult result =
        fidl::WireCall(fidl::UnownedClientEnd<fuchsia_io::Directory>(flat->handle[i]))
            ->Clone(fuchsia_io::wire::OpenFlags::kDescribe |
                        fuchsia_io::wire::OpenFlags::kCloneSameRights,
                    fidl::ServerEnd<fuchsia_io::Node>(endpoints->server.TakeChannel()));
    if (!result.ok()) {
      FX_PLOGS(FATAL, result.status()) << "failed to clone '" << path << "'";
    }

    // TODO(https://fxbug.dev/68742): Replace the use of threads with async clients when it is
    // possible to extract the channel from the client.
    auto [thread,
          inserted] = threads.emplace(path, [&root, client_end = std::move(endpoints->client),
                                             dispatcher, path]() mutable {
      EventHandler handler([&](fidl::WireEvent<fuchsia_io::Directory::OnOpen>* event) {
        if (event->s != ZX_OK) {
          FX_PLOGS(ERROR, event->s) << "failed to open '" << path << "'";
        }
        // Must run on the dispatcher thread to avoid racing with VFS dispatch.
        async::PostTask(dispatcher, [&root, path, client_end = std::move(client_end)]() mutable {
          // Drop the leading slash.
          std::string_view relative_path = path;
          if (relative_path.front() == '/') {
            relative_path = relative_path.substr(1);
          }
          if (zx_status_t status = root->AddEntry(
                  relative_path, fbl::MakeRefCounted<fs::RemoteDir>(std::move(client_end)));
              status != ZX_OK) {
            FX_PLOGS(ERROR, status) << "failed to add entry for '" << path << "'";
          } else {
            FX_LOGS(INFO) << "mounted '" << path << "'";
          }
        });
      });
      if (fidl::Status status = handler.HandleOneEvent(client_end); !status.ok()) {
        FX_PLOGS(ERROR, status.status()) << "failed to receive OnOpen event for '" << path << "'";
      }
    });
    if (!inserted) {
      FX_LOGS(FATAL) << "duplicate namespace entry: " << path;
    }
  }

  std::thread thread([&loop]() {
    if (zx_status_t status = loop.Run(); status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "VFS loop exited";
    }
  });

  fs::ManagedVfs vfs(dispatcher);

  zx::status result = console_launcher::ConsoleLauncher::Create();
  if (result.is_error()) {
    FX_PLOGS(FATAL, result.status_value()) << "failed to create console launcher";
  }
  auto& launcher = result.value();

  std::tuple<const char*, std::string&, std::vector<std::string_view>> map[] = {
      {"autorun:boot", args->autorun_boot, {}},
      {"autorun:system", args->autorun_system, {"/system"}},
  };

  std::vector<std::thread> autorun;
  for (const auto& [name, args, paths] : map) {
    if (args.empty()) {
      continue;
    }
    zx::status endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
    if (endpoints.is_error()) {
      FX_PLOGS(FATAL, endpoints.status_value()) << "failed to create endpoints";
    }

    if (zx_status_t status =
            vfs.ServeDirectory(root, std::move(endpoints->server), fs::Rights::All());
        status != ZX_OK) {
      FX_PLOGS(FATAL, status) << "failed to serve root directory";
    }

    // Get the full commandline by splitting on '+'.
    std::vector argv = fxl::SplitStringCopy(args, "+", fxl::WhiteSpaceHandling::kTrimWhitespace,
                                            fxl::SplitResult::kSplitWantNonEmpty);
    autorun.emplace_back([paths = paths, &threads, args = std::move(argv), name = name,
                          client_end = std::move(endpoints->client),
                          &job = launcher.shell_job()]() {
      for (std::string_view path : paths) {
        if (auto it = threads.find(path); it != threads.end()) {
          it->second.join();
        }
      }

      const char* argv[args.size() + 1];
      argv[args.size()] = nullptr;
      for (size_t i = 0; i < args.size(); ++i) {
        argv[i] = args[i].c_str();
      }

      fdio_spawn_action_t actions[] = {
          {
              .action = FDIO_SPAWN_ACTION_SET_NAME,
              .name =
                  {
                      .data = name,
                  },
          },
          {
              .action = FDIO_SPAWN_ACTION_ADD_NS_ENTRY,
              .ns =
                  {
                      .prefix = "/",
                      .handle = client_end.channel().get(),
                  },
          },
      };

      zx::process process;
      char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
      constexpr uint32_t flags = FDIO_SPAWN_CLONE_ALL & ~FDIO_SPAWN_CLONE_NAMESPACE;
      FX_LOGS(INFO) << "starting '" << name << "': " << args;
      zx_status_t status =
          fdio_spawn_etc(job.get(), flags, argv[0], argv, nullptr, std::size(actions), actions,
                         process.reset_and_get_address(), err_msg);
      if (status != ZX_OK) {
        FX_PLOGS(ERROR, status) << "failed to start '" << name << "': " << err_msg;
        return;
      }
      if (zx_status_t status =
              process.wait_one(ZX_PROCESS_TERMINATED, zx::time::infinite(), nullptr);
          status != ZX_OK) {
        FX_PLOGS(ERROR, status) << "failed to wait for '" << name << "' termination";
      }
      FX_LOGS(INFO) << "completed '" << name << "': " << args;
    });
  }

  while (true) {
    zx::status endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
    if (endpoints.is_error()) {
      FX_PLOGS(FATAL, endpoints.status_value()) << "failed to create endpoints";
    }
    if (zx_status_t status =
            vfs.ServeDirectory(root, std::move(endpoints->server), fs::Rights::All());
        status != ZX_OK) {
      FX_PLOGS(FATAL, status) << "failed to serve root directory";
    }

    if (zx_status_t status = launcher.LaunchShell(args.value(), std::move(endpoints->client));
        status != ZX_OK) {
      FX_PLOGS(FATAL, status) << "failed to launch shell";
    }

    if (zx_status_t status = launcher.WaitForShellExit(); status != ZX_OK) {
      FX_PLOGS(FATAL, status) << "failed to wait for shell exit";
    }
  }
}
