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

#include "fuchsia/io/cpp/fidl.h"
#include "fuchsia/io/test/cpp/fidl.h"
#include "src/lib/storage/vfs/cpp/managed_vfs.h"
#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/pseudo_file.h"
#include "src/lib/storage/vfs/cpp/remote_dir.h"
#include "src/lib/storage/vfs/cpp/vfs_types.h"
#include "src/lib/storage/vfs/cpp/vmo_file.h"

zx_status_t DummyReader(fbl::String* output) { return ZX_OK; }

zx_status_t DummyWriter(std::string_view input) { return ZX_OK; }

class TestHarness : public fuchsia::io::test::Io1Harness {
 public:
  explicit TestHarness() : vfs_loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
    vfs_loop_.StartThread("vfs_thread");
    vfs_ = std::make_unique<fs::ManagedVfs>(vfs_loop_.dispatcher());
  }

  ~TestHarness() override {
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
    config.set_no_exec(false);
    config.set_no_vmofile(false);
    config.set_no_remote_dir(false);
    config.set_no_admin(false);

    // PseudoFile/PseudoDir do not support a variety of methods:
    config.set_immutable_dir(true);
    config.set_no_get_buffer(true);
    config.set_no_rename(true);
    config.set_no_link(true);
    config.set_no_set_attr(true);

    callback(std::move(config));
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

  void GetDirectory(fuchsia::io::test::Directory root, uint32_t flags,
                    fidl::InterfaceRequest<fuchsia::io::Directory> directory_request) final {
    fbl::RefPtr<fs::PseudoDir> dir{fbl::MakeRefCounted<fs::PseudoDir>()};

    if (root.has_entries()) {
      for (const auto& entry : root.entries()) {
        AddEntry(*entry, *dir);
      }
    }

    fs::VnodeConnectionOptions options = fs::VnodeConnectionOptions::FromIoV1Flags(flags);
    options = fs::VnodeConnectionOptions::FilterForNewConnection(options);
    zx_status_t status = vfs_->Serve(std::move(dir), directory_request.TakeChannel(), options);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Serving directory failed: " << zx_status_get_string(status);
      return;
    }
  }

  void AddEntry(const fuchsia::io::test::DirectoryEntry& entry, fs::PseudoDir& dest) {
    switch (entry.Which()) {
      case fuchsia::io::test::DirectoryEntry::Tag::kDirectory: {
        // TODO(fxbug.dev/33880): Set the correct flags on this directory.
        fbl::RefPtr<fs::PseudoDir> child{fbl::MakeRefCounted<fs::PseudoDir>()};
        if (entry.directory().has_entries()) {
          for (const auto& grandchild : entry.directory().entries()) {
            AddEntry(*grandchild, *child);
          }
        }
        dest.AddEntry(entry.directory().name(), child);
        break;
      }
      case fuchsia::io::test::DirectoryEntry::Tag::kFile: {
        std::vector<uint8_t> contents = entry.file().contents();
        auto reader = [contents](fbl::String* output) {
          *output = fbl::String(reinterpret_cast<const char*>(contents.data()), contents.size());
          return ZX_OK;
        };
        dest.AddEntry(entry.file().name(),
                      fbl::MakeRefCounted<fs::BufferedPseudoFile>(reader, &DummyWriter));
        break;
      }
      case fuchsia::io::test::DirectoryEntry::Tag::kVmoFile: {
        // Copy the buffer. We only have a reference, and we need to make sure it lives long enough.
        fuchsia::mem::Range buffer;
        entry.vmo_file().buffer().Clone(&buffer);

        dest.AddEntry(entry.vmo_file().name(),
                      fbl::MakeRefCounted<fs::VmoFile>(buffer.vmo, buffer.offset, buffer.size));

        // Stash the vmo here, because |fs::VmoFile| only borrows a reference to it.
        test_vmos_.emplace_back(std::move(buffer.vmo));
        break;
      }
      case fuchsia::io::test::DirectoryEntry::Tag::Invalid:
        FX_LOGS(ERROR) << "Unknown DirectoryEntry type";
        break;
    }
  }

 private:
  std::unique_ptr<fs::ManagedVfs> vfs_;
  std::vector<zx::vmo> test_vmos_;
  async::Loop vfs_loop_;
};

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  syslog::SetTags({"io_conformance_harness_cppvfs"});

  TestHarness harness;
  fidl::BindingSet<fuchsia::io::test::Io1Harness> bindings;

  // Expose the Io1Harness protocol as an outgoing service.
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  context->outgoing()->AddPublicService(bindings.GetHandler(&harness));

  return loop.Run();
}
