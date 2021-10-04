// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/vfs/cpp/pseudo_dir.h>
#include <lib/vfs/cpp/pseudo_file.h>
#include <lib/vfs/cpp/remote_dir.h>
#include <lib/vfs/cpp/vmo_file.h>
#include <zircon/status.h>

#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

#include "fuchsia/io/cpp/fidl.h"
#include "fuchsia/io/test/cpp/fidl.h"

zx_status_t DummyWriter(std::vector<uint8_t>) { return ZX_OK; }

class SdkCppHarness : public fuchsia::io::test::Io1Harness {
 public:
  explicit SdkCppHarness() = default;

  ~SdkCppHarness() override = default;

  void GetConfig(GetConfigCallback callback) final {
    fuchsia::io::test::Io1Config config;

    // Supported configuration options:
    config.set_immutable_file(false);  // Files are mutable.
    config.set_no_remote_dir(false);   // vfs::RemoteDir
    config.set_no_vmofile(false);      // vfs::VmoFile

    // Unsupported configuration options:
    config.set_immutable_dir(true);                 // OPEN_FLAG_CREATE is not supported.
    config.set_no_admin(true);                      // Admin flag is not supported.
    config.set_no_rename(true);                     // vfs::PseudoDir does not support Rename.
    config.set_no_link(true);                       // Link/Unlink is not supported.
    config.set_no_set_attr(true);                   // SetAttr is not supported.
    config.set_no_get_token(true);                  // GetToken is unsupported.
    config.set_non_conformant_path_handling(true);  // Path handling is currently inconsistent.

    // TODO(fxbug.dev/45287): Support ExecFile, and GetBuffer.
    config.set_no_execfile(true);
    config.set_no_get_buffer(true);

    callback(std::move(config));
  }

  void GetDirectoryWithRemoteDirectory(
      fidl::InterfaceHandle<fuchsia::io::Directory> remote_directory, std::string dirname,
      uint32_t flags, fidl::InterfaceRequest<fuchsia::io::Directory> directory_request) final {
    auto root = std::make_unique<vfs::PseudoDir>();
    auto remote_dir_entry = std::make_unique<vfs::RemoteDir>(std::move(remote_directory));

    zx_status_t status = root->AddEntry(dirname, std::move(remote_dir_entry));
    ZX_ASSERT_MSG(status == ZX_OK, "Failed to add Remote directory entry!");
    status = root->Serve(flags, directory_request.TakeChannel());
    ZX_ASSERT_MSG(status == ZX_OK, "Failed to serve remote directory!");

    directories_.push_back(std::move(root));
  }

  void GetDirectory(fuchsia::io::test::Directory root, uint32_t flags,
                    fidl::InterfaceRequest<fuchsia::io::Directory> directory_request) final {
    auto dir = std::make_unique<vfs::PseudoDir>();

    if (root.has_entries()) {
      for (const auto& entry : root.entries()) {
        AddEntry(*entry, *dir);
      }
    }

    ZX_ASSERT_MSG(dir->Serve(flags, directory_request.TakeChannel()) == ZX_OK,
                  "Failed to serve directory!");
    directories_.push_back(std::move(dir));
  }

 private:
  void AddEntry(const fuchsia::io::test::DirectoryEntry& entry, vfs::PseudoDir& dest) {
    switch (entry.Which()) {
      case fuchsia::io::test::DirectoryEntry::Tag::kDirectory: {
        auto dir_entry = std::make_unique<vfs::PseudoDir>();
        if (entry.directory().has_entries()) {
          for (const auto& child_entry : entry.directory().entries()) {
            AddEntry(*child_entry, *dir_entry);
          }
        }
        ZX_ASSERT_MSG(dest.AddEntry(entry.directory().name(), std::move(dir_entry)) == ZX_OK,
                      "Failed to add Directory entry!");
        break;
      }
      case fuchsia::io::test::DirectoryEntry::Tag::kFile: {
        std::vector<uint8_t> contents = entry.file().contents();
        auto read_handler = [contents](std::vector<uint8_t>* output,
                                       size_t max_bytes) -> zx_status_t {
          ZX_ASSERT(contents.size() <= max_bytes);
          *output = std::vector<uint8_t>(contents);
          return ZX_OK;
        };
        auto file_entry = std::make_unique<vfs::PseudoFile>(std::numeric_limits<size_t>::max(),
                                                            read_handler, DummyWriter);
        ZX_ASSERT_MSG(dest.AddEntry(entry.file().name(), std::move(file_entry)) == ZX_OK,
                      "Failed to add File entry!");
        break;
      }
      case fuchsia::io::test::DirectoryEntry::Tag::kVmoFile: {
        const auto& buffer = entry.vmo_file().buffer();
        // The VMO backing the buffer is duplicated so that tests using VMO_FLAG_EXACT can ensure
        // the same VMO is returned by subsequent GetBuffer calls.
        zx::vmo vmo_clone;
        ZX_ASSERT_MSG(buffer.vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_clone) == ZX_OK,
                      "Failed to duplicate VMO!");

        auto vmo_file_entry = std::make_unique<vfs::VmoFile>(
            std::move(vmo_clone), buffer.offset, buffer.size, vfs::VmoFile::WriteOption::WRITABLE);

        ZX_ASSERT_MSG(dest.AddEntry(entry.vmo_file().name(), std::move(vmo_file_entry)) == ZX_OK,
                      "Failed to add VmoFile entry!");
        break;
      }
      case fuchsia::io::test::DirectoryEntry::Tag::Invalid:
      default:
        ZX_ASSERT_MSG(false, "Unknown/Invalid DirectoryEntry type!");
        break;
    }
  }

  std::vector<std::unique_ptr<vfs::PseudoDir>> directories_;
};

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  syslog::SetTags({"io_conformance_harness_sdkcpp"});

  SdkCppHarness harness;
  fidl::BindingSet<fuchsia::io::test::Io1Harness> bindings;

  // Expose the Io1Harness protocol as an outgoing service.
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  context->outgoing()->AddPublicService(bindings.GetHandler(&harness));

  return loop.Run();
}
