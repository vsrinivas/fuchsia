// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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
#include <fs/vmo_file.h>

#include "fuchsia/io/cpp/fidl.h"
#include "fuchsia/io/test/cpp/fidl.h"
#include "src/lib/syslog/cpp/logger.h"

class TestCasesImpl : public fuchsia::io::test::TestCases {
 public:
  TestCasesImpl() : vfs_loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
    vfs_loop_.StartThread("vfs_thread");
    vfs_ = std::make_unique<fs::ManagedVfs>(vfs_loop_.dispatcher());
  }

  ~TestCasesImpl() override {
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

  void GetEmptyDirectory(zx::channel directory_request) final {
    fbl::RefPtr<fs::PseudoDir> root{fbl::MakeRefCounted<fs::PseudoDir>()};
    zx_status_t status = vfs_->ServeDirectory(std::move(root), std::move(directory_request),
                                              fs::Rights::ReadWrite());
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Serving empty directory failed: " << zx_status_get_string(status);
      return;
    }
    FX_LOGS(INFO) << "Serving empty directory";
  }

  void GetDirectoryWithVmoFile(fuchsia::mem::Range buffer, zx::channel directory_request) final {
    fbl::RefPtr<fs::PseudoDir> root{fbl::MakeRefCounted<fs::PseudoDir>()};
    root->AddEntry("vmo_file",
                   fbl::MakeRefCounted<fs::VmoFile>(buffer.vmo, buffer.offset, buffer.size));
    zx_status_t status = vfs_->ServeDirectory(std::move(root), std::move(directory_request),
                                              fs::Rights::ReadWrite());
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Serving directory with vmo file failed: " << zx_status_get_string(status);
      return;
    }
    FX_LOGS(INFO) << "Serving directory with vmo file";
    // Stash the vmo here, because |fs::VmoFile| only borrows a reference to it.
    test_vmos_.emplace_back(std::move(buffer.vmo));
  }

 private:
  std::unique_ptr<fs::ManagedVfs> vfs_;
  std::vector<zx::vmo> test_vmos_;
  async::Loop vfs_loop_;
};

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  syslog::InitLogger({"io_conformance_harness_ulibfs"});

  auto context = sys::ComponentContext::Create();
  sys::ServiceHandler handler;
  fuchsia::io::test::Harness::Handler harness(&handler);

  // Serving the fuchsia.io v1 harness for now.
  TestCasesImpl v1_impl;
  fidl::BindingSet<fuchsia::io::test::TestCases> v1_bindings;
  harness.add_v1(v1_bindings.GetHandler(&v1_impl));

  // Serve an instance of `Harness` service.
  context->outgoing()->AddService<fuchsia::io::test::Harness>(std::move(handler));

  loop.Run();
  return 0;
}
