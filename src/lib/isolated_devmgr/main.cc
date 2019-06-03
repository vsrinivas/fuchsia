// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/sys/cpp/component_context.h>
#include <src/lib/fxl/command_line.h>
#include <src/lib/fxl/logging.h>

#include <iostream>

#include "isolated_devmgr.h"

using namespace isolated_devmgr;

void Usage() {
  std::cerr <<
      R"(
Usage:
   isolated_devmgr [options]

Options:
   --svc_name=[svc_name]: service name to expose, defaults to fuchsia.io.Directory
   --load_driver=[driver_path]: loads a driver into isolated manager. May be informed multiple
                                times.
   --search_driver=[search_path]: loads all drivers in provided search path. May be informed
                                  multiple times.
   --sys_device=[sys_device_driver]: path to sys device driver, defaults to
                                     /boot/driver/test/sysdev.so
   --wait_for=[device]: wait for isolated manager to have |device| exposed before serving any
                        requests. May be informed multiple times.
   --help: displays this help page.

Note: isolated_devmgr runs as a component, so all paths must be relative to the component's
namespace. When using any paths that point to /boot/*, features "shell" must be part of the
manifest.
)";
}

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  devmgr_launcher::Args args;
  // fill up defaults:
  args.sys_device_driver = "/boot/driver/test/sysdev.so";
  args.stdio = fbl::unique_fd(open("/dev/null", O_RDWR));
  args.disable_block_watcher = true;
  args.disable_netsvc = true;
  args.use_system_svchost = true;

  std::string svc_name = "fuchsia.io.Directory";
  std::vector<std::string> wait;

  // load options from command line
  auto cl = fxl::CommandLineFromArgcArgv(argc, argv);
  for (const auto& opt : cl.options()) {
    if (opt.name == "svc_name") {
      svc_name = opt.value;
    } else if (opt.name == "load_driver") {
      args.load_drivers.push_back(opt.value.c_str());
    } else if (opt.name == "search_driver") {
      args.driver_search_paths.push_back(opt.value.c_str());
    } else if (opt.name == "sys_device") {
      args.sys_device_driver = opt.value.c_str();
    } else if (opt.name == "wait_for") {
      wait.push_back(opt.value);
    } else if (opt.name == "help") {
      Usage();
      return 0;
    } else {
      Usage();
      return 1;
    }
  }

  auto devmgr = IsolatedDevmgr::Create(std::move(args), loop.dispatcher());
  if (!devmgr) {
    return 1;
  }
  devmgr->SetExceptionCallback([]() {
    FXL_LOG(ERROR) << "Isolated Devmgr crashed";
    zx_process_exit(1);
  });

  for (const auto& path : wait) {
    if (devmgr->WaitForFile(path.c_str()) != ZX_OK) {
      FXL_LOG(ERROR) << "Isolated Devmgr failed while waiting for path "
                     << path;
      return 1;
    }
  }

  auto context = sys::ComponentContext::Create();
  auto service = std::make_unique<vfs::Service>(
      [&devmgr](zx::channel chan, async_dispatcher_t* dispatcher) {
        devmgr->Connect(std::move(chan));
      });
  context->outgoing()->AddPublicService(std::move(service),
                                        std::move(svc_name));

  loop.Run();

  return 0;
}
