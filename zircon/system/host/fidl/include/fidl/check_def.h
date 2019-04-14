// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_CHECK_H_
#define ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_CHECK_H_

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
    // if any). The code is external to this class.
    //
    // Example:
    //   CheckDef check(
    //       "invalid_case_for_primitive_alias",
    //       "Primitive aliases must be named in lower_snake_case")
    //
    // Checks defined in linter.cpp are created by "AddCheck()" with their
    // linting logic (by lambda), as per this example:
    //    callbacks_.OnUsing(
    //        [& linter = *this,
    //         check = AddCheck(
    //             "invalid-case-for-primitive-alias",
    //             "Primitive aliases must be named in lower_snake_case")]
    //        //
    //        (const raw::Using& element) {
    //            CHECK_IDENTIFIER_CASE(element.maybe_alias, lower_snake_case)
    //        });
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

#endif // ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_CHECK_H_
