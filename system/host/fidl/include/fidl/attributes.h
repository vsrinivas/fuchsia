// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_ATTRIBUTES_H_
#define ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_ATTRIBUTES_H_

#include <set>
#include <vector>

#include "error_reporter.h"
#include "raw_ast.h"

namespace fidl {

// AttributePlacement indicates the placement of an attribute list, e.g.
// whether that list will be placed on an enum declaration, method, or
// union member.
enum AttributePlacement {
    kConstDecl,
    kEnumDecl,
    kEnumMember,
    kInterfaceDecl,
    kLibrary,
    kMethod,
    kStructDecl,
    kStructMember,
    kTableDecl,
    kTableMember,
    kUnionDecl,
    kUnionMember,
};

class AttributesBuilder {
public:
    AttributesBuilder(ErrorReporter* error_reporter)
        : error_reporter_(error_reporter) {}

    AttributesBuilder(ErrorReporter* error_reporter,
                      std::vector<std::unique_ptr<raw::Attribute>> attributes)
        : error_reporter_(error_reporter), attributes_(std::move(attributes)) {
        for (auto& attribute : attributes_) {
            names_.emplace(attribute->name);
        }
    }

    bool Insert(std::unique_ptr<raw::Attribute> attribute);
    std::vector<std::unique_ptr<raw::Attribute>> Done();

    static void ValidatePlacement(
        ErrorReporter* error_reporter, AttributePlacement placement,
        const std::vector<std::unique_ptr<raw::Attribute>>& attributes);

private:
    struct InsertResult {
        enum Kind {
            kOk,
            kDuplicate,
            kInvalidValue,
            kTypoOnKey,
        };

        InsertResult(Kind kind, std::string message_fragment)
            : kind(kind), message_fragment(message_fragment) {}

        Kind kind;
        std::string message_fragment;
    };

    InsertResult InsertHelper(std::unique_ptr<raw::Attribute> attribute);

    ErrorReporter* error_reporter_;
    std::vector<std::unique_ptr<raw::Attribute>> attributes_;
    std::set<std::string> names_;
};

} // namespace fidl

#endif // ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_ATTRIBUTES_H_
