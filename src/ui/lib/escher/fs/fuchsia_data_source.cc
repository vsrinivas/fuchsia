// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/fs/fuchsia_data_source.h"

#include <lib/vfs/cpp/pseudo_dir.h>
#include <lib/vfs/cpp/pseudo_file.h>
#include <zircon/errors.h>

#include <memory>
#include <string>
#include <vector>

#include "src/lib/files/directory.h"

namespace escher {
namespace {

std::vector<std::string> StrSplit(const std::string& str, const std::string& delim) {
  std::vector<std::string> items;
  for (size_t start = 0; start < str.length();) {
    size_t end = str.find(delim, start);
    if (end == std::string::npos) {
      end = str.length();
    }
    items.push_back(str.substr(start, end - start));
    start = end + delim.length();
  }
  return items;
}

}  // namespace

FuchsiaDataSource::FuchsiaDataSource(const std::shared_ptr<vfs::PseudoDir>& root_dir)
    : root_dir_(root_dir) {}

FuchsiaDataSource::FuchsiaDataSource() : root_dir_(std::make_shared<vfs::PseudoDir>()) {}

bool FuchsiaDataSource::InitializeWithRealFiles(const std::vector<HackFilePath>& paths,
                                                const char* root) {
  const std::string kRoot(root);
  base_path_ = kRoot;
  bool success = true;
  for (const auto& path : paths) {
    success &= LoadFile(this, kRoot, path);

    auto segs = StrSplit(path, "/");
    FX_DCHECK(segs.size() > 0);
    auto dir = root_dir_.get();
    for (size_t i = 0; i + 1 < segs.size(); ++i) {
      const auto& seg = segs[i];
      vfs::internal::Node* subdir;
      if (ZX_OK != dir->Lookup(seg, &subdir)) {
        auto node = std::make_unique<vfs::PseudoDir>();
        subdir = node.get();
        auto status = dir->AddEntry(seg, std::move(node));
        FX_DCHECK(ZX_OK == status);
        if (status != ZX_OK) {
          return false;  // don't hang the system
        }
      }
      dir = static_cast<vfs::PseudoDir*>(subdir);
    }
    zx_status_t status = dir->AddEntry(
        segs[segs.size() - 1],
        std::make_unique<vfs::PseudoFile>(
            200 * 1024 * 1024 /* max file size, 200 MB */,
            /* read_handler= */
            [this, path](std::vector<uint8_t>* output, size_t max_file_size) {
              auto out = ReadFile(path);
              size_t len = out.length();
              if (len > max_file_size) {
                FX_LOGS(WARNING) << "File(" << path << ") size more than: " << max_file_size
                                 << ", truncating";
                len = max_file_size;
              }
              output->resize(len);
              std::copy(out.begin(), out.begin() + len, output->begin());
              return ZX_OK;
            },
            /* write_handler= */
            [this, path](std::vector<uint8_t> input) {
              // TODO(fxbug.dev/7189): The file is successfully updated, but the
              // terminal would complain "truncate: Invalid argument".
              HackFileContents content(input.size(), 0);
              std::copy(input.begin(), input.begin() + input.size(), content.begin());
              FX_LOGS(INFO) << "Updated file: " << path;
              WriteFile(path, std::move(content));
              return ZX_OK;
            }));

    if (status != ZX_OK && status != ZX_ERR_ALREADY_EXISTS) {
      FX_LOGS(WARNING) << "Failed to AddEntry(): " << status;
      success = false;
    }
  }
  return success;
}

}  // namespace escher
