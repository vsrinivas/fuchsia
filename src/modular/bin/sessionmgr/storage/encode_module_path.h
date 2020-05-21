// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file is a description of the pages and keys used by the modular runtime.

#ifndef SRC_MODULAR_BIN_SESSIONMGR_STORAGE_CONSTANTS_AND_UTILS_H_
#define SRC_MODULAR_BIN_SESSIONMGR_STORAGE_CONSTANTS_AND_UTILS_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/fidl/cpp/string.h>

#include <string>

namespace modular {

// Returns a string form of |module_path| that guarantees no collisions with
// other encoded module paths.
std::string EncodeModulePath(const std::vector<std::string>& module_path);

}  // namespace modular

#endif  // SRC_MODULAR_BIN_SESSIONMGR_STORAGE_CONSTANTS_AND_UTILS_H_
