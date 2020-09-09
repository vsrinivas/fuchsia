// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include <memory>
#include <vector>

#include <fs/managed_vfs.h>
#include <fs/pseudo_dir.h>
#include <fs/pseudo_file.h>
#include <fs/remote_dir.h>
#include <fs/vfs_types.h>
#include <fs/vmo_file.h>

#include "fuchsia/io/cpp/fidl.h"
#include "fuchsia/io/test/cpp/fidl.h"

zx_status_t DummyReader(fbl::String* output) { return ZX_OK; }

zx_status_t DummyWriter(fbl::StringPiece input) { return ZX_OK; }

class UlibfsHarness : public fuchsia::io::test::Io1Harness {
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

  void GetConfig(GetConfigCallback callback) final {
    fuchsia::io::test::Io1Config config;
    config.set_immutable_file(false);
    config.set_immutable_dir(false);
    config.set_no_exec(false);
    config.set_no_vmofile(false);
    config.set_no_remote_dir(false);
    callback(std::move(config));
  }

  void GetEmptyDirectory(uint32_t flags,
                         fidl::InterfaceRequest<fuchsia::io::Directory> directory_request) final {
    fbl::RefPtr<fs::PseudoDir> root{fbl::MakeRefCounted<fs::PseudoDir>()};

    fs::VnodeConnectionOptions options = fs::VnodeConnectionOptions::FromIoV1Flags(flags);
    options = fs::VnodeConnectionOptions::FilterForNewConnection(options);
    zx_status_t status = vfs_->Serve(std::move(root), directory_request.TakeChannel(), options);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Serving empty directory failed: " << zx_status_get_string(status);
      return;
    }
  }

  void GetDirectoryWithEmptyFile(
      std::string filename, uint32_t flags,
      fidl::InterfaceRequest<fuchsia::io::Directory> directory_request) final {
    fbl::RefPtr<fs::PseudoDir> root{fbl::MakeRefCounted<fs::PseudoDir>()};
    root->AddEntry(filename,
                   fbl::MakeRefCounted<fs::BufferedPseudoFile>(&DummyReader, &DummyWriter));
    fs::VnodeConnectionOptions options = fs::VnodeConnectionOptions::FromIoV1Flags(flags);
    options = fs::VnodeConnectionOptions::FilterForNewConnection(options);
    zx_status_t status = vfs_->Serve(std::move(root), directory_request.TakeChannel(), options);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Serving directory with empty file failed: "
                     << zx_status_get_string(status);
      return;
    }
  }

  void GetDirectoryWithVmoFile(
      fuchsia::mem::Range file, std::string filename, uint32_t flags,
      fidl::InterfaceRequest<fuchsia::io::Directory> directory_request) final {
    fbl::RefPtr<fs::PseudoDir> root{fbl::MakeRefCounted<fs::PseudoDir>()};
    root->AddEntry(filename, fbl::MakeRefCounted<fs::VmoFile>(file.vmo, file.offset, file.size));
    fs::VnodeConnectionOptions options = fs::VnodeConnectionOptions::FromIoV1Flags(flags);
    options = fs::VnodeConnectionOptions::FilterForNewConnection(options);
    zx_status_t status = vfs_->Serve(std::move(root), directory_request.TakeChannel(), options);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Serving directory with vmo file failed: " << zx_status_get_string(status);
      return;
    }
    FX_LOGS(INFO) << "Serving directory with vmo file";
    // Stash the vmo here, because |fs::VmoFile| only borrows a reference to it.
    test_vmos_.emplace_back(std::move(file.vmo));
  }

  void GetDirectoryWithDirectory(
      std::string dirname, uint32_t flags,
      ::fidl::InterfaceRequest<fuchsia::io::Directory> directory_request) final {
    fbl::RefPtr<fs::PseudoDir> root{fbl::MakeRefCounted<fs::PseudoDir>()};
    root->AddEntry(dirname, fbl::MakeRefCounted<fs::PseudoDir>());
    fs::VnodeConnectionOptions options = fs::VnodeConnectionOptions::FromIoV1Flags(flags);
    options = fs::VnodeConnectionOptions::FilterForNewConnection(options);
    zx_status_t status = vfs_->Serve(std::move(root), directory_request.TakeChannel(), options);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Serving directory with child directory failed: "
                     << zx_status_get_string(status);
      return;
    }
  }

  void GetDirectoryWithNestedDirectory(
      std::string dirname, std::string nested_dirname, uint32_t flags,
      fidl::InterfaceRequest<fuchsia::io::Directory> directory_request) final {
    fbl::RefPtr<fs::PseudoDir> root{fbl::MakeRefCounted<fs::PseudoDir>()};
    fbl::RefPtr<fs::PseudoDir> child{fbl::MakeRefCounted<fs::PseudoDir>()};
    child->AddEntry(nested_dirname, fbl::MakeRefCounted<fs::PseudoDir>());
    root->AddEntry(dirname, child);
    fs::VnodeConnectionOptions options = fs::VnodeConnectionOptions::FromIoV1Flags(flags);
    options = fs::VnodeConnectionOptions::FilterForNewConnection(options);
    zx_status_t status = vfs_->Serve(std::move(root), directory_request.TakeChannel(), options);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Serving directory with nested directory failed: "
                     << zx_status_get_string(status);
      return;
    }
  }

  void GetDirectoryWithRemoteDirectory(
      fidl::InterfaceHandle<fuchsia::io::Directory> remote_directory, std::string dirname,
      uint32_t flags, fidl::InterfaceRequest<fuchsia::io::Directory> directory_request) final {
    fbl::RefPtr<fs::PseudoDir> root{fbl::MakeRefCounted<fs::PseudoDir>()};
    root->AddEntry(dirname, fbl::MakeRefCounted<fs::RemoteDir>(remote_directory.TakeChannel()));
    fs::VnodeConnectionOptions options = fs::VnodeConnectionOptions::FromIoV1Flags(flags);
    fs::VnodeConnectionOptions filtered_options =
        fs::VnodeConnectionOptions::FilterForNewConnection(options);
    zx_status_t status =
        vfs_->Serve(std::move(root), directory_request.TakeChannel(), filtered_options);
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
  syslog::SetTags({"io_conformance_harness_ulibfs"});

  UlibfsHarness harness;
  fidl::BindingSet<fuchsia::io::test::Io1Harness> bindings;

  // Expose the Io1Harness protocol as an outgoing service.
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  context->outgoing()->AddPublicService(bindings.GetHandler(&harness));

  return loop.Run();
}
