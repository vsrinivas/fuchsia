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

namespace fio_test = fuchsia::io::test;

zx_status_t DummyWriter(std::string_view input) { return ZX_OK; }

class TestHarness : public fio_test::Io1Harness {
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
    fio_test::Io1Config config;

    // Supported options
    config.set_mutable_file(true);
    config.set_supports_vmo_file(true);
    config.set_supports_remote_dir(true);
    config.set_supports_get_token(true);
    config.set_conformant_path_handling(true);

    // Unsupported options
    config.set_supports_create(false);
    config.set_supports_executable_file(false);
    config.set_supports_get_backing_memory(false);
    config.set_supports_rename(false);
    config.set_supports_link(false);
    config.set_supports_set_attr(false);
    config.set_supports_unlink(false);

    callback(std::move(config));
  }

  void GetDirectory(fio_test::Directory root, fuchsia::io::OpenFlags flags,
                    fidl::InterfaceRequest<fuchsia::io::Directory> directory_request) final {
    fbl::RefPtr<fs::PseudoDir> dir{fbl::MakeRefCounted<fs::PseudoDir>()};

    if (root.has_entries()) {
      for (auto& entry : *root.mutable_entries()) {
        AddEntry(std::move(*entry), *dir);
      }
    }

    fs::VnodeConnectionOptions options = fs::VnodeConnectionOptions::FromIoV1Flags(
        static_cast<fuchsia_io::wire::OpenFlags>(static_cast<uint32_t>(flags)));
    options = fs::VnodeConnectionOptions::FilterForNewConnection(options);
    zx_status_t status = vfs_->Serve(std::move(dir), directory_request.TakeChannel(), options);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Serving directory failed: " << zx_status_get_string(status);
      return;
    }
  }

  void AddEntry(fio_test::DirectoryEntry entry, fs::PseudoDir& dest) {
    switch (entry.Which()) {
      case fio_test::DirectoryEntry::Tag::kDirectory: {
        fio_test::Directory directory = std::move(entry.directory());
        // TODO(fxbug.dev/33880): Set the correct flags on this directory.
        auto dir_entry = fbl::MakeRefCounted<fs::PseudoDir>();
        if (directory.has_entries()) {
          for (auto& child : *directory.mutable_entries()) {
            AddEntry(std::move(*child), *dir_entry);
          }
        }
        dest.AddEntry(directory.name(), dir_entry);
        break;
      }
      case fio_test::DirectoryEntry::Tag::kRemoteDirectory: {
        fio_test::RemoteDirectory remote_dir = std::move(entry.remote_directory());
        // Convert the HLCPP InterfaceHandle to a LLCPP ClientEnd.
        // We use a temporary `interface_handle` here to show that the conversion is safe.
        fidl::InterfaceHandle<fuchsia::io::Directory> interface_handle =
            std::move(*remote_dir.mutable_remote_client());
        fidl::ClientEnd<fuchsia_io::Directory> remote_client(interface_handle.TakeChannel());
        auto remote_entry = fbl::MakeRefCounted<fs::RemoteDir>(std::move(remote_client));
        dest.AddEntry(remote_dir.name(), std::move(remote_entry));
        break;
      }
      case fio_test::DirectoryEntry::Tag::kFile: {
        fio_test::File file = std::move(entry.file());
        std::vector<uint8_t> contents = std::move(*file.mutable_contents());
        auto reader = [contents = std::move(contents)](fbl::String* output) {
          *output = fbl::String(reinterpret_cast<const char*>(contents.data()), contents.size());
          return ZX_OK;
        };
        dest.AddEntry(file.name(),
                      fbl::MakeRefCounted<fs::BufferedPseudoFile>(reader, &DummyWriter));
        break;
      }
      case fio_test::DirectoryEntry::Tag::kVmoFile: {
        fio_test::VmoFile vmo_file = std::move(entry.vmo_file());
        fuchsia::mem::Range buffer = std::move(*vmo_file.mutable_buffer());
        dest.AddEntry(vmo_file.name(), fbl::MakeRefCounted<fs::VmoFile>(std::move(buffer.vmo),
                                                                        buffer.offset, buffer.size,
                                                                        /*writable=*/true));
        break;
      }
      case fio_test::DirectoryEntry::Tag::kExecutableFile:
        ZX_PANIC("Executable files are not supported");
        break;
      case fio_test::DirectoryEntry::Tag::Invalid:
        ZX_PANIC("Unknown/Invalid DirectoryEntry type");
        break;
    }
  }

 private:
  std::unique_ptr<fs::ManagedVfs> vfs_;
  async::Loop vfs_loop_;
};

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  syslog::SetTags({"io_conformance_harness_cppvfs"});

  TestHarness harness;
  fidl::BindingSet<fio_test::Io1Harness> bindings;

  // Expose the Io1Harness protocol as an outgoing service.
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  context->outgoing()->AddPublicService(bindings.GetHandler(&harness));

  return loop.Run();
}
