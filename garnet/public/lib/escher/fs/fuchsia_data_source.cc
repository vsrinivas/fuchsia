// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/fs/fuchsia_data_source.h"

#include <fs/pseudo-dir.h>
#include <fs/pseudo-file.h>
#include <zircon/errors.h>
#include <string>
#include <vector>

#include "lib/fxl/files/directory.h"

namespace escher {
namespace {

std::vector<std::string> StrSplit(const std::string& str,
                                  const std::string& delim) {
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

FuchsiaDataSource::FuchsiaDataSource(const fbl::RefPtr<fs::PseudoDir>& root_dir)
    : root_dir_(root_dir) {}

FuchsiaDataSource::FuchsiaDataSource()
    : root_dir_(fbl::MakeRefCounted<fs::PseudoDir>()) {}

bool FuchsiaDataSource::InitializeWithRealFiles(
    const std::vector<HackFilePath>& paths, const char* prefix) {
  const std::string kPrefix(prefix);
  bool success = true;
  for (const auto& path : paths) {
    success &= LoadFile(this, kPrefix, path);

    auto segs = StrSplit(path, "/");
    FXL_DCHECK(segs.size() > 0);
    auto dir = root_dir_;
    for (size_t i = 0; i + 1 < segs.size(); ++i) {
      const auto& seg = segs[i];
      fbl::RefPtr<fs::Vnode> subdir;
      if (ZX_OK != dir->Lookup(&subdir, seg)) {
        subdir = fbl::MakeRefCounted<fs::PseudoDir>();
        FXL_DCHECK(ZX_OK == dir->AddEntry(seg, subdir));
      }
      dir = fbl::RefPtr<fs::PseudoDir>::Downcast(std::move(subdir));
    }
    zx_status_t status = dir->AddEntry(
        segs[segs.size() - 1],
        fbl::MakeRefCounted<fs::UnbufferedPseudoFile>(
            /* read_handler= */
            [this, path](fbl::String* output) {
              *output = ReadFile(path);
              return ZX_OK;
            },
            /* write_handler= */
            [this, path](fbl::StringPiece input) {
              // TODO(ES-98): The file is successfully updated, but the
              // terminal would complain "truncate: Invalid argument".
              FXL_LOG(INFO) << "Updated file: " << path;
              WriteFile(path, HackFileContents(input.data(), input.size()));
              return ZX_OK;
            }));

    if (status != ZX_OK && status != ZX_ERR_ALREADY_EXISTS) {
      FXL_LOG(WARNING) << "Failed to AddEntry(): " << status;
      success = false;
    }
  }
  return success;
}

}  // namespace escher
