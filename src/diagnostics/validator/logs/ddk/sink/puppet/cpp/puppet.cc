// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/validate/logs/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/driver-integration-test/fixture.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>

#include <filesystem>

#include <ddk/platform-defs.h>
#include <fbl/ref_ptr.h>
#include <fs/service.h>

#include "fuchsia/logger/cpp/fidl.h"
#include "lib/fdio/namespace.h"
#include "lib/zx/channel.h"

class Puppet : public fuchsia::validate::logs::LogSinkPuppet {
 public:
  explicit Puppet(std::unique_ptr<sys::ComponentContext> context) : context_(std::move(context)) {
    context_->outgoing()->AddPublicService(puppet_bindings_.GetHandler(this));
    setenv("devmgr.log-to-debuglog", "false", 1);
    fidl::InterfacePtr<fuchsia::sys::Launcher> launcher;
    context_->svc()->Connect(launcher.NewRequest());
    zx::channel req;
    auto services = sys::ServiceDirectory::CreateWithRequest(&req);

    fuchsia::sys::LaunchInfo info;
    info.directory_request = std::move(req);
    info.url =
        "fuchsia-pkg://fuchsia.com/accessor-validator-ddk#meta/"
        "log-test-devmgr.cmx";

    launcher->CreateComponent(std::move(info), ctlr_.NewRequest());
    zx::channel devfs_req, devfs;
    zx::channel::create(0, &devfs_req, &devfs);
    services->Connect("fuchsia.validate.logs.IsolatedDevmgr", std::move(devfs_req));
    ForwardPuppet(std::move(devfs));
  }

  // Starts forwarding the Puppet service from devfs
  void ForwardPuppet(zx::channel devfs) {
    // Acquire the fdio_ns_t so we can bind /dev to our local namespace
    fdio_ns_t* ns;
    fdio_ns_get_installed(&ns);

    // Bind /dev at /remote-dev in our local namespace.
    fdio_ns_bind(ns, "/remote-dev", devfs.release());
    int fd = 0;

    // Wait for DDK puppet to activate
    while (fd < 1) {
      fd = open("/remote-dev/test/virtual-logsink", O_RDONLY);
      usleep(1);
    }

    // Connect to the virtual-logsink service exported from the isolated devmgr.
    zx::channel channels[2];
    zx::channel::create(0, channels, channels + 1);
    fdio_service_connect("/remote-dev/test/virtual-logsink", channels[0].release());
    puppet_.Bind(std::move(channels[1]));
  }

  void GetInfo(GetInfoCallback callback) override { puppet_->GetInfo(std::move(callback)); }

  void EmitLog(fuchsia::validate::logs::RecordSpec spec, EmitLogCallback callback) override {
    puppet_->EmitLog(std::move(spec), std::move(callback));
  }

 private:
  fuchsia::validate::logs::LogSinkPuppetPtr puppet_;
  driver_integration_test::IsolatedDevmgr devmgr_;
  std::unique_ptr<sys::ComponentContext> context_;
  fidl::BindingSet<fuchsia::validate::logs::LogSinkPuppet> puppet_bindings_;
  fidl::InterfacePtr<fuchsia::sys::ComponentController> ctlr_;
};

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  Puppet puppet(sys::ComponentContext::CreateAndServeOutgoingDirectory());
  loop.Run();
}
