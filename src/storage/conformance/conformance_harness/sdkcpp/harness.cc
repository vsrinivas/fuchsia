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

namespace fio_test = fuchsia::io::test;

zx_status_t DummyWriter(const std::vector<uint8_t>&) { return ZX_OK; }
class SdkCppHarness : public fio_test::Io1Harness {
 public:
  explicit SdkCppHarness() = default;

  ~SdkCppHarness() override = default;

  void GetConfig(GetConfigCallback callback) final {
    fio_test::Io1Config config;

    // Supported configuration options:
    config.set_mutable_file(true);         // Files are mutable.
    config.set_supports_remote_dir(true);  // vfs::RemoteDir
    config.set_supports_vmo_file(true);    // vfs::VmoFile

    // Unsupported configuration options:
    config.set_supports_create(false);           // OPEN_FLAG_CREATE is not supported.
    config.set_supports_rename(false);           // vfs::PseudoDir does not support Rename.
    config.set_supports_link(false);             // Link is not supported.
    config.set_supports_set_attr(false);         // SetAttr is not supported.
    config.set_supports_get_token(false);        // GetToken is unsupported.
    config.set_conformant_path_handling(false);  // Path handling is currently inconsistent.
    config.set_supports_unlink(false);           // Unlink is not supported.

    // TODO(fxbug.dev/45287): Support ExecutableFile, and GetBuffer.
    config.set_supports_executable_file(false);
    config.set_supports_get_buffer(false);

    callback(std::move(config));
  }

  void GetDirectory(fio_test::Directory root, fuchsia::io::OpenFlags flags,
                    fidl::InterfaceRequest<fuchsia::io::Directory> directory_request) final {
    auto dir = std::make_unique<vfs::PseudoDir>();

    if (root.has_entries()) {
      for (auto& entry : *root.mutable_entries()) {
        AddEntry(std::move(*entry), *dir);
      }
    }

    ZX_ASSERT_MSG(dir->Serve(flags, directory_request.TakeChannel()) == ZX_OK,
                  "Failed to serve directory!");
    directories_.push_back(std::move(dir));
  }

 private:
  void AddEntry(fio_test::DirectoryEntry entry, vfs::PseudoDir& dest) {
    switch (entry.Which()) {
      case fio_test::DirectoryEntry::Tag::kDirectory: {
        fio_test::Directory directory = std::move(entry.directory());
        auto dir_entry = std::make_unique<vfs::PseudoDir>();
        if (directory.has_entries()) {
          for (auto& child_entry : *directory.mutable_entries()) {
            AddEntry(std::move(*child_entry), *dir_entry);
          }
        }
        ZX_ASSERT_MSG(dest.AddEntry(directory.name(), std::move(dir_entry)) == ZX_OK,
                      "Failed to add Directory entry!");
        break;
      }
      case fio_test::DirectoryEntry::Tag::kRemoteDirectory: {
        fio_test::RemoteDirectory remote_directory = std::move(entry.remote_directory());
        auto remote_dir_entry =
            std::make_unique<vfs::RemoteDir>(std::move(*remote_directory.mutable_remote_client()));
        dest.AddEntry(remote_directory.name(), std::move(remote_dir_entry));
        break;
      }
      case fio_test::DirectoryEntry::Tag::kFile: {
        fio_test::File file = std::move(entry.file());
        std::vector<uint8_t> contents = std::move(*file.mutable_contents());
        auto read_handler = [contents = std::move(contents)](std::vector<uint8_t>* output,
                                                             size_t max_bytes) -> zx_status_t {
          ZX_ASSERT(contents.size() <= max_bytes);
          *output = std::vector<uint8_t>(contents);
          return ZX_OK;
        };
        auto file_entry = std::make_unique<vfs::PseudoFile>(std::numeric_limits<size_t>::max(),
                                                            read_handler, DummyWriter);
        ZX_ASSERT_MSG(dest.AddEntry(file.name(), std::move(file_entry)) == ZX_OK,
                      "Failed to add File entry!");
        break;
      }
      case fio_test::DirectoryEntry::Tag::kVmoFile: {
        fio_test::VmoFile vmo_file = std::move(entry.vmo_file());
        fuchsia::mem::Range buffer = std::move(*vmo_file.mutable_buffer());
        auto vmo_file_entry = std::make_unique<vfs::VmoFile>(
            std::move(buffer.vmo), buffer.offset, buffer.size, vfs::VmoFile::WriteOption::WRITABLE);
        ZX_ASSERT_MSG(dest.AddEntry(vmo_file.name(), std::move(vmo_file_entry)) == ZX_OK,
                      "Failed to add VmoFile entry!");
        break;
      }
      case fio_test::DirectoryEntry::Tag::kExecutableFile:
        ZX_PANIC("Executable files are not supported!");
        break;
      case fio_test::DirectoryEntry::Tag::Invalid:
        ZX_PANIC("Unknown/Invalid DirectoryEntry type!");
        break;
    }
  }

  std::vector<std::unique_ptr<vfs::PseudoDir>> directories_;
};

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  syslog::SetTags({"io_conformance_harness_sdkcpp"});

  SdkCppHarness harness;
  fidl::BindingSet<fio_test::Io1Harness> bindings;

  // Expose the Io1Harness protocol as an outgoing service.
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  context->outgoing()->AddPublicService(bindings.GetHandler(&harness));

  return loop.Run();
}
