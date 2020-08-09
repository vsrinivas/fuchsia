// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fidl_codec/builtin_semantic.h"

#include <string>

namespace fidl_codec {
namespace semantic {

std::string builtin_semantic_fuchsia_io =
    "library fuchsia.io {\n"
    "  Node::Clone {\n"
    "    request.object = handle : 'cloned';\n"
    "  }\n"
    "  Directory::Open {\n"
    "    request.object = handle / request.path;\n"
    "  }\n"
    "}\n";

std::string builtin_semantic_fuchsia_sys =
    "library fuchsia.sys {\n"
    "  Launcher::CreateComponent {\n"
    "   request.launch_info.directory_request =\n"
    "     HandleDescription('server', request.launch_info.url);\n"
    "   request.controller = HandleDescription('server-control', request.launch_info.url);\n"
    "  }\n"
    "}\n";

}  // namespace semantic
}  // namespace fidl_codec
