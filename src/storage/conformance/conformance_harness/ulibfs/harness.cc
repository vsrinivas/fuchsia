// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <zircon/status.h>

#include <memory>
#include <vector>

#include <fs/managed_vfs.h>
#include <fs/pseudo_dir.h>
#include <fs/remote_dir.h>
#include <fs/vmo_file.h>

#include "fuchsia/io/cpp/fidl.h"
#include "fuchsia/io/test/cpp/fidl.h"
#include "src/lib/syslog/cpp/logger.h"

class UlibfsHarness : public fuchsia::io::test::Io1TestHarness {
 public:
  explicit UlibfsHarness() : vfs_loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
    vfs_loop_.StartThread("vfs_thread");
    vfs_ = std::make_unique<fs::ManagedVfs>(vfs_loop_.dispatcher());
  }

  ~UlibfsHarness() override {
    // |fs::ManagedVfs| must be shutdown first before stopping its dispatch loop.
    // Here we asynchronously post the shutdown request, then synchronously join
    // the |vfs_loop_| thread.
    vfs_->Shutdown([this](zx_status_t status) mutable {
      async::PostTask(vfs_loop_.dispatcher(), [this] {
        vfs_.reset();
        vfs_loop_.Quit();
      });
    });
    vfs_loop_.JoinThreads();
  }

  void GetEmptyDirectory(fidl::InterfaceRequest<fuchsia::io::Directory> directory_request) final {
    fbl::RefPtr<fs::PseudoDir> root{fbl::MakeRefCounted<fs::PseudoDir>()};
    zx_status_t status = vfs_->ServeDirectory(std::move(root), directory_request.TakeChannel(),
                                              fs::Rights::ReadWrite());
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Serving empty directory failed: " << zx_status_get_string(status);
      return;
    }
  }

  void GetDirectoryWithVmoFile(
      fuchsia::mem::Range buffer,
      fidl::InterfaceRequest<fuchsia::io::Directory> directory_request) final {
    fbl::RefPtr<fs::PseudoDir> root{fbl::MakeRefCounted<fs::PseudoDir>()};
    root->AddEntry("vmo_file",
                   fbl::MakeRefCounted<fs::VmoFile>(buffer.vmo, buffer.offset, buffer.size));
    zx_status_t status = vfs_->ServeDirectory(std::move(root), directory_request.TakeChannel(),
                                              fs::Rights::ReadWrite());
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Serving directory with vmo file failed: " << zx_status_get_string(status);
      return;
    }
    FX_LOGS(INFO) << "Serving directory with vmo file";
    // Stash the vmo here, because |fs::VmoFile| only borrows a reference to it.
    test_vmos_.emplace_back(std::move(buffer.vmo));
  }

  void GetDirectoryWithRemoteDirectory(
      std::string path, fidl::InterfaceHandle<fuchsia::io::Directory> remote_directory,
      fidl::InterfaceRequest<fuchsia::io::Directory> directory_request) final {
    fbl::RefPtr<fs::PseudoDir> root{fbl::MakeRefCounted<fs::PseudoDir>()};
    root->AddEntry(path, fbl::MakeRefCounted<fs::RemoteDir>(remote_directory.TakeChannel()));
    zx_status_t status = vfs_->ServeDirectory(std::move(root), directory_request.TakeChannel(),
                                              fs::Rights::ReadWrite());
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Serving directory with remote failed: " << zx_status_get_string(status);
      return;
    }
  }

 private:
  std::unique_ptr<fs::ManagedVfs> vfs_;
  std::vector<zx::vmo> test_vmos_;
  async::Loop vfs_loop_;
};

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  syslog::InitLogger({"io_conformance_harness_ulibfs"});

  UlibfsHarness harness;
  fidl::BindingSet<fuchsia::io::test::Io1TestHarness> bindings;
  fuchsia::io::test::Io1TestHarnessPtr connection;
  bindings.AddBinding(&harness, connection.NewRequest());

  // Sends a connection of `Io1TestHarness` protocol to the test through a HarnessReceiver.
  auto context = sys::ComponentContext::Create();
  fuchsia::io::test::HarnessReceiverPtr receiver =
      context->svc()->Connect<fuchsia::io::test::HarnessReceiver>();
  receiver->SendIo1Harness(connection.Unbind());

  return loop.Run();
}
