// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_ATTRIBUTES_H_
#define ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_ATTRIBUTES_H_

#include "flat_ast.h"
#include "raw_ast.h"

namespace fidl {

bool HasSimpleLayout(const flat::Decl* decl);

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

private:
    struct InsertResult {
        enum Kind {
            kOk,
            kDuplicate,
            kTypo,
        };

        InsertResult(Kind kind, std::string likely_name)
            : kind(kind), likely_name(likely_name) {}

        Kind kind;
        std::string likely_name;
    };

    InsertResult InsertHelper(std::unique_ptr<raw::Attribute> attribute);

    ErrorReporter* error_reporter_;
    std::vector<std::unique_ptr<raw::Attribute>> attributes_;
    std::set<std::string> names_;
};

} // namespace fidl

#endif // ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_ATTRIBUTES_H_
