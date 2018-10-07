// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/attributes.h"

namespace fidl {

bool HasSimpleLayout(const flat::Decl* decl) {
    return decl->GetAttribute("Layout") == "Simple";
}

size_t EditDistance(const std::string& sequence1, const std::string& sequence2) {
    size_t s1_length = sequence1.length();
    size_t s2_length = sequence2.length();
    size_t row1[s1_length+1];
    size_t row2[s1_length+1];
    size_t* last_row = row1;
    size_t* this_row = row2;
    for (size_t i = 0; i <= s1_length; i++)
        last_row[i] = i;
    for (size_t j = 0; j < s2_length; j++) {
        this_row[0] = j + 1;
        auto s2c = sequence2[j];
        for (size_t i = 1; i <= s1_length; i++) {
            auto s1c = sequence1[i - 1];
            this_row[i] = std::min(std::min(
                last_row[i] + 1, this_row[i - 1] + 1), last_row[i - 1] + (s1c == s2c ? 0 : 1));
        }
        std::swap(last_row, this_row);
    }
    return last_row[s1_length];
}

#define CHECK_TOO_CLOSE(ExpectedName, ActualName)                    \
    {                                                                \
        auto edit_distance = EditDistance(ExpectedName, ActualName); \
        if (0 < edit_distance && edit_distance < 2)                  \
            return InsertResult(InsertResult::kTypo, ExpectedName);  \
    }

bool AttributesBuilder::Insert(std::unique_ptr<raw::Attribute> attribute) {
    auto attribute_name = attribute->name;
    auto attribute_location = attribute->location();
    auto result = InsertHelper(std::move(attribute));
    switch (result.kind) {
    case InsertResult::Kind::kDuplicate: {
        std::string message("duplicate attribute with name '");
        message.append(attribute_name);
        message.append("'");
        error_reporter_->ReportError(attribute_location, message);
        return false;
    }
    case InsertResult::Kind::kTypo: {
        std::string message("suspect attribute with name '");
        message.append(attribute_name);
        message.append("'; did you mean '");
        message.append(result.likely_name);
        message.append("'?");
        error_reporter_->ReportWarning(attribute_location, message);
        return true;
    }
    case InsertResult::Kind::kOk:
        return true;
    } // switch
}

std::vector<std::unique_ptr<raw::Attribute>> AttributesBuilder::Done() {
    return std::move(attributes_);
}

AttributesBuilder::InsertResult AttributesBuilder::InsertHelper(std::unique_ptr<raw::Attribute> attribute) {
    if (!names_.emplace(attribute->name).second) {
        return InsertResult(InsertResult::kDuplicate, "");
    }
    auto attribute_name = attribute->name;
    attributes_.push_back(std::move(attribute));
    CHECK_TOO_CLOSE("Discoverable", attribute_name);
    CHECK_TOO_CLOSE("Doc", attribute_name);
    CHECK_TOO_CLOSE("FragileBase", attribute_name);
    CHECK_TOO_CLOSE("Internal", attribute_name);
    CHECK_TOO_CLOSE("Simple", attribute_name);
    return InsertResult(InsertResult::kOk, "");
}

} // namespace fidl
