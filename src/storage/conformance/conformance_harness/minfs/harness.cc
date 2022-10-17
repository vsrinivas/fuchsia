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

#include <fbl/ref_ptr.h>

#include "fuchsia/io/cpp/fidl.h"
#include "fuchsia/io/test/cpp/fidl.h"
#include "src/lib/storage/block_client/cpp/fake_block_device.h"
#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/vfs_types.h"
#include "src/storage/minfs/bcache.h"
#include "src/storage/minfs/directory.h"
#include "src/storage/minfs/format.h"
#include "src/storage/minfs/minfs.h"
#include "src/storage/minfs/minfs_private.h"
#include "src/storage/minfs/runner.h"
#include "src/storage/minfs/vnode.h"

namespace minfs {

constexpr uint64_t kBlockCount = 1 << 11;

class MinfsHarness : public fuchsia::io::test::Io1Harness {
 public:
  explicit MinfsHarness() : vfs_loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
    vfs_loop_.StartThread("vfs_thread");

    auto device = std::make_unique<block_client::FakeBlockDevice>(kBlockCount, kMinfsBlockSize);

    auto bcache = Bcache::Create(std::move(device), kBlockCount);
    ZX_ASSERT(bcache.is_ok());
    ZX_ASSERT(Mkfs(bcache.value().get()).is_ok());

    auto runner = Runner::Create(vfs_loop_.dispatcher(), *std::move(bcache), {});
    ZX_ASSERT(runner.is_ok());
    runner_ = *std::move(runner);

    // One connection must be maintained to avoid filesystem termination.
    auto root_server = root_client_.NewRequest();
    zx::result status =
        runner_->ServeRoot(fidl::ServerEnd<fuchsia_io::Directory>(root_server.TakeChannel()));
    ZX_ASSERT(status.is_ok());
  }

  ~MinfsHarness() override {
    // The runner shutdown takes care of shutting everything down in the right order, including the
    // async loop.
    runner_->Shutdown([](zx_status_t status) { ZX_ASSERT(status == ZX_OK); });
    vfs_loop_.JoinThreads();
  }

  void GetConfig(GetConfigCallback callback) final {
    fuchsia::io::test::Io1Config config;

    // Supported options
    config.set_mutable_file(true);
    config.set_supports_create(true);
    config.set_supports_rename(true);
    config.set_supports_link(true);
    config.set_supports_set_attr(true);
    config.set_supports_get_token(true);
    config.set_conformant_path_handling(true);
    config.set_supports_unlink(true);

    // Unsupported options
    config.set_supports_executable_file(false);
    config.set_supports_vmo_file(false);
    config.set_supports_remote_dir(false);
    config.set_supports_get_backing_memory(false);

    callback(std::move(config));
  }

  void GetDirectory(fuchsia::io::test::Directory root, fuchsia::io::OpenFlags flags,
                    fidl::InterfaceRequest<fuchsia::io::Directory> directory_request) final {
    // Create a unique directory within the root of minfs for each request and popuplate it with the
    // requested contents.
    auto directory = CreateUniqueDirectory();
    if (root.has_entries()) {
      PopulateDirectory(root.entries(), *directory);
    }

    auto options = directory->ValidateOptions(GetConnectionOptions(flags));
    ZX_ASSERT_MSG(options.is_ok(), "Invalid directory flags: %s", options.status_string());
    zx_status_t status =
        runner_->Serve(std::move(directory), directory_request.TakeChannel(), options.value());
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
      case fuchsia::io::test::DirectoryEntry::Tag::kRemoteDirectory:
        ZX_PANIC("Remote directories are not supported");
      case fuchsia::io::test::DirectoryEntry::Tag::kVmoFile:
        ZX_PANIC("VMO files are not supported");
      case fuchsia::io::test::DirectoryEntry::Tag::kExecutableFile:
        ZX_PANIC("Executable files are not supported");
      case fuchsia::io::test::DirectoryEntry::Tag::Invalid:
        ZX_PANIC("Unknown/Invalid DirectoryEntry type!");
    }
  }

  fbl::RefPtr<Directory> GetRootNode() {
    auto vn_or = runner_->minfs().VnodeGet(kMinfsRootIno);
    ZX_ASSERT(vn_or.is_ok());
    auto root = fbl::RefPtr<Directory>::Downcast(std::move(vn_or.value()));
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

  static fs::VnodeConnectionOptions GetConnectionOptions(fuchsia::io::OpenFlags flags) {
    fs::VnodeConnectionOptions options = fs::VnodeConnectionOptions::FromIoV1Flags(
        static_cast<fuchsia_io::wire::OpenFlags>(static_cast<uint32_t>(flags)));
    options = fs::VnodeConnectionOptions::FilterForNewConnection(options);
    return options;
  }

 private:
  async::Loop vfs_loop_;
  std::unique_ptr<Runner> runner_;

  // Used to create a new unique directory within minfs for every call to |GetDirectory|.
  uint32_t directory_count_ = 0;
  fuchsia::io::DirectoryPtr root_client_;
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
