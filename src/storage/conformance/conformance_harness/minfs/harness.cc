// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <sys/stat.h>
#include <zircon/assert.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <block-client/cpp/fake-device.h>
#include <fbl/ref_ptr.h>

#include "fuchsia/io/cpp/fidl.h"
#include "fuchsia/io/test/cpp/fidl.h"
#include "src/lib/storage/vfs/cpp/vfs_types.h"
#include "src/storage/minfs/bcache.h"
#include "src/storage/minfs/directory.h"
#include "src/storage/minfs/format.h"
#include "src/storage/minfs/minfs.h"
#include "src/storage/minfs/minfs_private.h"
#include "src/storage/minfs/vnode.h"

namespace minfs {

constexpr uint64_t kBlockCount = 1 << 11;

class MinfsHarness : public fuchsia::io::test::Io1Harness {
 public:
  explicit MinfsHarness() : vfs_loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
    vfs_loop_.StartThread("vfs_thread");

    auto device = std::make_unique<block_client::FakeBlockDevice>(kBlockCount, kMinfsBlockSize);

    std::unique_ptr<Bcache> bcache;
    ZX_ASSERT(Bcache::Create(std::move(device), kBlockCount, &bcache) == ZX_OK);
    ZX_ASSERT(Mkfs(bcache.get()) == ZX_OK);
    ZX_ASSERT(Minfs::Create(vfs_loop_.dispatcher(), std::move(bcache), {}, &minfs_) == ZX_OK);
  }

  ~MinfsHarness() override {
    // |fs::ManagedVfs| must be shutdown first before stopping its dispatch loop.
    // Here we asynchronously post the shutdown request, then synchronously join
    // the |vfs_loop_| thread.
    minfs_->Shutdown([this](zx_status_t status) mutable {
      async::PostTask(vfs_loop_.dispatcher(), [this] {
        minfs_.reset();
        vfs_loop_.Quit();
      });
    });
    vfs_loop_.JoinThreads();
  }

  void GetConfig(GetConfigCallback callback) final {
    fuchsia::io::test::Io1Config config;
    config.set_immutable_file(false);
    config.set_immutable_dir(false);
    config.set_no_admin(false);
    config.set_no_rename(false);
    config.set_no_link(false);
    config.set_no_set_attr(false);
    config.set_no_get_token(false);

    // Minfs doesn't support executable files, VMO files, nor remote directories.
    config.set_no_execfile(true);
    config.set_no_vmofile(true);
    config.set_no_remote_dir(true);
    config.set_no_get_buffer(true);

    callback(std::move(config));
  }

  void GetDirectoryWithRemoteDirectory(
      fidl::InterfaceHandle<fuchsia::io::Directory> remote_directory, std::string dirname,
      uint32_t flags, fidl::InterfaceRequest<fuchsia::io::Directory> directory_request) final {
    ZX_PANIC("Method not supported");
  }

  void GetDirectory(fuchsia::io::test::Directory root, uint32_t flags,
                    fidl::InterfaceRequest<fuchsia::io::Directory> directory_request) final {
    // Create a unique directory within the root of minfs for each request and popuplate it with the
    // requested contents.
    auto directory = CreateUniqueDirectory();
    if (root.has_entries()) {
      PopulateDirectory(root.entries(), *directory);
    }

    auto options = directory->ValidateOptions(GetConnectionOptions(flags));
    ZX_ASSERT_MSG(options.is_ok(), "Invalid directory flags: %s",
                  zx_status_get_string(options.error()));
    // Convert the InterfaceRequest to a typed ServerEnd.
    fidl::ServerEnd<fuchsia_io::Node> server_end{directory_request.TakeChannel()};
    zx_status_t status =
        minfs_->Serve(std::move(directory), std::move(server_end), options.value());
    ZX_ASSERT_MSG(status == ZX_OK, "Failed to serve test directory: %s",
                  zx_status_get_string(status));
  }

  void PopulateDirectory(
      const std::vector<std::unique_ptr<fuchsia::io::test::DirectoryEntry>>& entries,
      Directory& dir) {
    for (const auto& entry : entries) {
      AddEntry(*entry, dir);
    }
  }

  void AddEntry(const fuchsia::io::test::DirectoryEntry& entry, Directory& parent) {
    switch (entry.Which()) {
      case fuchsia::io::test::DirectoryEntry::Tag::kDirectory: {
        fbl::RefPtr<fs::Vnode> vnode;
        // Minfs doesn't support rights flags.
        zx_status_t status = parent.Create(entry.directory().name(), S_IFDIR, &vnode);
        ZX_ASSERT_MSG(status == ZX_OK, "Failed to create a directory: %s",
                      zx_status_get_string(status));
        auto directory = fbl::RefPtr<Directory>::Downcast(std::move(vnode));
        ZX_ASSERT_MSG(directory != nullptr, "A vnode of the wrong type was created");
        if (entry.directory().has_entries()) {
          PopulateDirectory(entry.directory().entries(), *directory);
        }
        // The directory was opened when it was created.
        directory->Close();
        break;
      }
      case fuchsia::io::test::DirectoryEntry::Tag::kFile: {
        fbl::RefPtr<fs::Vnode> file;
        // Minfs doesn't support rights flags.
        zx_status_t status = parent.Create(entry.file().name(), S_IFREG, &file);
        ZX_ASSERT_MSG(status == ZX_OK, "Failed to create a file: %s", zx_status_get_string(status));

        size_t actual = 0;
        status = file->Write(entry.file().contents().data(), entry.file().contents().size(),
                             /*offset=*/0, &actual);
        ZX_ASSERT_MSG(status == ZX_OK, "Failed to write to file: %s", zx_status_get_string(status));
        // The file was opened when it was created.
        file->Close();
        break;
      }
      case fuchsia::io::test::DirectoryEntry::Tag::kVmoFile:
        ZX_PANIC("VMO files are not supported");
      case fuchsia::io::test::DirectoryEntry::Tag::Invalid:
        ZX_PANIC("Unknown DirectoryEntry type");
    }
  }

  fbl::RefPtr<Directory> GetRootNode() {
    fbl::RefPtr<VnodeMinfs> vn;
    ZX_ASSERT(minfs_->VnodeGet(&vn, kMinfsRootIno) == ZX_OK);
    auto root = fbl::RefPtr<Directory>::Downcast(std::move(vn));
    ZX_ASSERT_MSG(root != nullptr, "The root node wasn't a directory");
    return root;
  }

  fbl::RefPtr<Directory> CreateUniqueDirectory() {
    ++directory_count_;
    std::string directory_name = std::to_string(directory_count_);
    fbl::RefPtr<Directory> root = GetRootNode();
    fbl::RefPtr<fs::Vnode> vnode;
    zx_status_t status = root->Create(directory_name, S_IFDIR, &vnode);
    ZX_ASSERT_MSG(status == ZX_OK, "Failed to create a unique directory: %s",
                  zx_status_get_string(status));
    auto directory = fbl::RefPtr<Directory>::Downcast(std::move(vnode));
    ZX_ASSERT_MSG(directory != nullptr, "A vnode of the wrong type was created");
    return directory;
  }

  static fs::VnodeConnectionOptions GetConnectionOptions(uint32_t flags) {
    fs::VnodeConnectionOptions options = fs::VnodeConnectionOptions::FromIoV1Flags(flags);
    options = fs::VnodeConnectionOptions::FilterForNewConnection(options);
    return options;
  }

 private:
  async::Loop vfs_loop_;
  std::unique_ptr<Minfs> minfs_;

  // Used to create a new unique directory within minfs for every call to |GetDirectory|.
  uint32_t directory_count_ = 0;
};

}  // namespace minfs

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  syslog::SetTags({"io_conformance_harness_minfs"});

  minfs::MinfsHarness harness;
  fidl::BindingSet<fuchsia::io::test::Io1Harness> bindings;

  // Expose the Io1Harness protocol as an outgoing service.
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  context->outgoing()->AddPublicService(bindings.GetHandler(&harness));
  zx_status_t status = loop.Run();
  return status;
}
