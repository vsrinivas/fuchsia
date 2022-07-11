// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WEAVE_ADAPTATION_TESTS_FAKE_DIRECTORY_H_
#define SRC_CONNECTIVITY_WEAVE_ADAPTATION_TESTS_FAKE_DIRECTORY_H_

#include <lib/vfs/cpp/pseudo_dir.h>
#include <lib/vfs/cpp/vmo_file.h>

#include <utility>

#include <gtest/gtest.h>
#include <src/lib/fsl/vmo/sized_vmo.h>
#include <src/lib/fsl/vmo/strings.h>

namespace weave::adaptation::testing {

// Implementation of a fake, pseudo-directory that can be made to hold files for
// tests that serve directories.
class FakeDirectory final {
 public:
  FakeDirectory() : root_(std::make_unique<vfs::PseudoDir>()) {}

  // Add a file with the given name and data to the directory.
  FakeDirectory& AddFile(std::string filename, const std::string& data) {
    InternalAddFile(std::move(filename), data);
    return *this;
  }

  // Removes a file with the given name from the directory.
  FakeDirectory& RemoveFile(const std::string& filename) {
    InternalRemoveFile(filename);
    return *this;
  }

  // Serves the added files over the provided channel.
  void Serve(fidl::InterfaceRequest<fuchsia::io::Directory> channel,
             async_dispatcher_t* dispatcher) {
    root_->Serve(fuchsia::io::OpenFlags::DIRECTORY | fuchsia::io::OpenFlags::RIGHT_READABLE |
                     fuchsia::io::OpenFlags::DESCRIBE | fuchsia::io::OpenFlags::RIGHT_WRITABLE,
                 channel.TakeChannel(), dispatcher);
  }

 private:
  // Internal helper functions to assert when adding and removing files.
  void InternalAddFile(std::string filename, const std::string& data) {
    ASSERT_EQ(root_->AddEntry(std::move(filename), CreateVmoFile(data)), ZX_OK);
  }

  void InternalRemoveFile(const std::string& filename) {
    ASSERT_EQ(root_->RemoveEntry(filename), ZX_OK);
  }

  // Internal helper function to create a vmo-file from a string.
  static std::unique_ptr<vfs::VmoFile> CreateVmoFile(const std::string& data) {
    fsl::SizedVmo file_vmo;
    if (!fsl::VmoFromString(data, &file_vmo)) {
      return nullptr;
    }
    return std::make_unique<vfs::VmoFile>(std::move(file_vmo.vmo()), file_vmo.size(),
                                          vfs::VmoFile::WriteOption::WRITABLE,
                                          vfs::VmoFile::Sharing::CLONE_COW);
  }

  // Pseudo-directory to serve the resources from.
  std::unique_ptr<vfs::PseudoDir> root_;
};

}  // namespace weave::adaptation::testing

#endif  // SRC_CONNECTIVITY_WEAVE_ADAPTATION_TESTS_FAKE_DIRECTORY_H_
