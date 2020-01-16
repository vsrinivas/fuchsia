// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_LIB_PSEUDO_DIR_PSEUDO_DIR_UTILS_H_
#define SRC_MODULAR_LIB_PSEUDO_DIR_PSEUDO_DIR_UTILS_H_

#include <lib/vfs/cpp/pseudo_dir.h>
#include <lib/vfs/cpp/pseudo_file.h>

#include <memory>
#include <string>

namespace modular {

// Recursively constructs the path |file_path| with contents |file_contents|.
// |file_path| must be a well-formed path that ends with a file name.
std::unique_ptr<vfs::PseudoDir> MakeFilePathWithContents(const std::string& file_path,
                                                         const std::string& file_contents);

};  // namespace modular

#endif  // SRC_MODULAR_LIB_PSEUDO_DIR_PSEUDO_DIR_UTILS_H_
