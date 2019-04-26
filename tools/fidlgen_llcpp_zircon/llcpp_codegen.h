// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDLGEN_LLCPP_ZIRCON_LLCPP_CODEGEN_H_
#define TOOLS_FIDLGEN_LLCPP_ZIRCON_LLCPP_CODEGEN_H_

#include <filesystem>
#include <string>
#include <vector>

// Validate without touching the checked-in sources.
// Returns true when sources are up-to-date.
bool DoValidate(std::filesystem::path zircon_build_root,
                std::filesystem::path fidlgen_llcpp_path,
                std::filesystem::path tmp_dir,
                std::vector<std::filesystem::path>* out_dependencies);

// Update the checked-in sources.
void DoUpdate(std::filesystem::path zircon_build_root,
              std::filesystem::path fidlgen_llcpp_path,
              std::vector<std::filesystem::path>* out_dependencies);

#endif  // TOOLS_FIDLGEN_LLCPP_ZIRCON_LLCPP_CODEGEN_H_
