// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_CHECK_DEF_H_
#define ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_CHECK_DEF_H_

///////////////////////////////////////////////////////////////
// Even though this file is namespaced to "fidl::lint", it
// could be promoted to the "fidl" namespace in the future.
//
// check and finding classes should not have any
// dependencies on the "Lint" process. They should be
// generic enough to be useful for capturing and reporting
// findings from other developer tools, such as fidlc.
///////////////////////////////////////////////////////////////

#include <iostream>
#include <string>

#include <fidl/template_string.h>

namespace fidl {
namespace linter {

// Each CheckDef corresponds to some associated linting logic that verifies code
// meets or fails to meet a FIDL Readability requirement.
class CheckDef {
public:
    // A check includes an ID (in kebab-case) and a string message or
    // message_template (with optional placeholders for customizing the message,
    // if any). The check logic (code) is external to this class.
    CheckDef(std::string id, TemplateString message_template)
        : id_(id), message_template_(message_template) {}

    inline const std::string& id() const {
        return id_;
    }
    inline const TemplateString& message_template() const {
        return message_template_;
    }

private:
    std::string id_; // dash-separated (kebab-case), and URL suffixable
    TemplateString message_template_;
};

} // namespace linter
} // namespace fidl

#endif  // ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_CHECK_DEF_H_
