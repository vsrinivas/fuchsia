// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "build/tools/json_merge/json_merge.h"

#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "rapidjson/filewritestream.h"
#include "rapidjson/istreamwrapper.h"
#include "rapidjson/ostreamwrapper.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/writer.h"

namespace {

bool MergeObject(rapidjson::Value& merge_to, const rapidjson::Value& merge_from,
                 rapidjson::MemoryPoolAllocator<>& allocator, const std::string& input_name,
                 std::ostream& errors, bool deep_merge) {
  for (auto value_it = merge_from.MemberBegin(); value_it != merge_from.MemberEnd(); ++value_it) {
    auto old_member = merge_to.FindMember(value_it->name);
    if (old_member != merge_to.MemberEnd()) {
      if (deep_merge && old_member->value.IsObject() && value_it->value.IsObject()) {
        MergeObject(old_member->value, value_it->value, allocator, input_name, errors, true);
        continue;
      }

      errors << input_name << " has a conflicting value for key \"" << value_it->name.GetString()
             << "\"!\n";
      return false;
    }

    merge_to.AddMember(rapidjson::Value(value_it->name, allocator).Move(),
                       rapidjson::Value(value_it->value, allocator).Move(), allocator);
  }

  return true;
}

const int kRelaxedFlags = rapidjson::kParseCommentsFlag | rapidjson::kParseTrailingCommasFlag;

}  // namespace

int JSONMerge(const std::vector<struct input_file>& inputs, std::ostream& output,
              std::ostream& errors, const MergeConfig& config) {
  rapidjson::Document merged;
  merged.SetObject();
  auto& allocator = merged.GetAllocator();

  for (auto input_it = inputs.begin(); input_it != inputs.end(); ++input_it) {
    rapidjson::IStreamWrapper isw(*input_it->contents.get());
    rapidjson::Document input_doc;
    rapidjson::ParseResult parse_result = config.relaxed_input
                                              ? input_doc.ParseStream<kRelaxedFlags>(isw)
                                              : input_doc.ParseStream(isw);
    if (!parse_result) {
      errors << "Failed to parse " << input_it->name << "!\n";
      errors << rapidjson::GetParseError_En(parse_result.Code()) << " (offset "
             << parse_result.Offset() << ")\n";
      return 1;
    }
    if (!input_doc.IsObject()) {
      errors << input_it->name << " is not a JSON object!\n";
      return 1;
    }

    if (!MergeObject(merged, input_doc, allocator, input_it->name, errors, config.deep_merge))
      return 1;
  }

  rapidjson::OStreamWrapper osw(output);
  if (config.minify) {
    rapidjson::Writer<rapidjson::OStreamWrapper> writer(osw);
    merged.Accept(writer);
  } else {
    rapidjson::PrettyWriter<rapidjson::OStreamWrapper> writer(osw);
    merged.Accept(writer);
  }

  return 0;
}
