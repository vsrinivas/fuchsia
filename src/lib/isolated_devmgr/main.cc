// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fdio/directory.h>
#include <lib/sys/cpp/component_context.h>
#include <zircon/device/vfs.h>

#include <iostream>

#include <src/lib/fxl/command_line.h>
#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/strings/split_string.h>
#include <src/lib/fxl/strings/string_number_conversions.h>
#include <src/lib/fxl/strings/string_view.h>

#include "isolated_devmgr.h"

#define DEVICE_IDS_VID_LOC 0
#define DEVICE_IDS_PID_LOC 1
#define DEVICE_IDS_DID_LOC 2
#define DEVICE_IDS_SIZE 3
#define ISO_DEV_MGR_RET_OK 0
#define ISO_DEV_MGR_RET_ERR 1

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
   --add_namespace=[ns]: make the namespace 'ns' from this component available to the devmgr
                         under the same path.
   --device_vid_pid_did=[dev_vid:dev_pid:dev_did]: adding a device with hex dev_vid, dev_pid
                                                   and dev_did. May be informed multiple times.
   --help: displays this help page.

Note: isolated_devmgr runs as a component, so all paths must be relative to the component's
namespace. Since the devmgr libraries and executables are currently under /boot, the components
sandbox metadata must include the "/boot/bin" and "/boot/lib". Additionally, it's common to load
drivers out of "/boot/driver" and this directory must also be specificed in the components sandbox
metadata to make these drivers available to isolated_devmgr.

Example sandbox metadata:

    "sandbox": {
        "boot": [
            "bin",
            "driver",
            "lib"
        ]
    }
)";
}

static int process_device_ids(const char* str,
                              fbl::Vector<board_test::DeviceEntry>* dev_entry_list_ptr) {
  board_test::DeviceEntry dev_entry = {};
  uint32_t key;
  fxl::StringView sv_str = fxl::StringView(str);
  auto params = fxl::SplitString(sv_str, ":", fxl::kKeepWhitespace, fxl::kSplitWantNonEmpty);
  if ((dev_entry_list_ptr == nullptr) || (params.size() < DEVICE_IDS_SIZE)) {
    return ISO_DEV_MGR_RET_ERR;
  }
  if (!fxl::StringToNumberWithError(params.at(DEVICE_IDS_VID_LOC), &key, fxl::Base::k16)) {
    return ISO_DEV_MGR_RET_ERR;
  }
  dev_entry.vid = key;
  if (!fxl::StringToNumberWithError(params.at(DEVICE_IDS_PID_LOC), &key, fxl::Base::k16)) {
    return ISO_DEV_MGR_RET_ERR;
  }
  dev_entry.pid = key;
  if (!fxl::StringToNumberWithError(params.at(DEVICE_IDS_DID_LOC), &key, fxl::Base::k16)) {
    return ISO_DEV_MGR_RET_ERR;
  }
  dev_entry.did = key;
  dev_entry_list_ptr->push_back(dev_entry);
  return ISO_DEV_MGR_RET_OK;
}

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  devmgr_launcher::Args args;
  auto device_list_unique_ptr = std::unique_ptr<fbl::Vector<board_test::DeviceEntry>>(
      new fbl::Vector<board_test::DeviceEntry>());

  // fill up defaults:
  args.sys_device_driver = "/boot/driver/test/sysdev.so";
  args.stdio = fbl::unique_fd(open("/dev/null", O_RDWR));
  args.disable_block_watcher = true;
  args.disable_netsvc = true;
  args.use_system_svchost = true;

  std::string svc_name = "fuchsia.io.Directory";
  std::vector<std::string> wait;
  std::vector<std::string> namespaces;

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
    } else if (opt.name == "add_namespace") {
      namespaces.push_back(opt.value);
    } else if (opt.name == "device_vid_pid_did") {
      int status = process_device_ids(opt.value.c_str(), device_list_unique_ptr.get());
      if (status != 0) {
        Usage();
        return status;
      }
    } else if (opt.name == "help") {
      Usage();
      return ISO_DEV_MGR_RET_OK;
    } else {
      Usage();
      return ISO_DEV_MGR_RET_ERR;
    }
  }

  // Pass-through any additional namespaces that we want to provide to the devmgr. These are
  // exposed to devmgr under the same local path. Ex: if you share '/pkg', you could provide a
  // driver as '/pkg/data/my_driver.so'.
  for (const auto& ns : namespaces) {
    zx::channel client, server;
    zx_status_t status = zx::channel::create(0, &client, &server);
    if (status != ZX_OK) {
      FXL_PLOG(ERROR, status) << "Failed to create channel";
      return ISO_DEV_MGR_RET_ERR;
    }
    status = fdio_open(ns.c_str(), ZX_FS_RIGHT_READABLE, server.release());
    if (status != ZX_OK) {
      FXL_PLOG(ERROR, status) << "Failed to open namespace " << ns;
      return ISO_DEV_MGR_RET_ERR;
    }
    args.flat_namespace.push_back({ns.c_str(), std::move(client)});
  }

  auto devmgr =
      IsolatedDevmgr::Create(std::move(args), std::move(device_list_unique_ptr), loop.dispatcher());
  if (!devmgr) {
    return ISO_DEV_MGR_RET_ERR;
  }

  devmgr->SetExceptionCallback([]() {
    FXL_LOG(ERROR) << "Isolated Devmgr crashed";
    zx_process_exit(ISO_DEV_MGR_RET_ERR);
  });

  for (const auto& path : wait) {
    if (devmgr->WaitForFile(path.c_str()) != ZX_OK) {
      FXL_LOG(ERROR) << "Isolated Devmgr failed while waiting for path " << path;
      return ISO_DEV_MGR_RET_ERR;
    }
  }

  auto context = sys::ComponentContext::Create();
  auto service =
      std::make_unique<vfs::Service>([&devmgr](zx::channel chan, async_dispatcher_t* dispatcher) {
        devmgr->Connect(std::move(chan));
      });
  context->outgoing()->AddPublicService(std::move(service), std::move(svc_name));

  loop.Run();

  return ISO_DEV_MGR_RET_OK;
}
