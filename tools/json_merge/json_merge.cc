// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "build/tools/json_merge/json_merge.h"

#include "third_party/rapidjson/rapidjson/document.h"
#include "third_party/rapidjson/rapidjson/error/en.h"
#include "third_party/rapidjson/rapidjson/filewritestream.h"
#include "third_party/rapidjson/rapidjson/istreamwrapper.h"
#include "third_party/rapidjson/rapidjson/ostreamwrapper.h"
#include "third_party/rapidjson/rapidjson/prettywriter.h"
#include "third_party/rapidjson/rapidjson/writer.h"

int JSONMerge(const std::vector<struct input_file>& inputs,
              std::ostream& output, std::ostream& errors, bool minify) {
  rapidjson::Document merged;
  merged.SetObject();
  auto& allocator = merged.GetAllocator();

  for (auto input_it = inputs.begin(); input_it != inputs.end(); ++input_it) {
    rapidjson::IStreamWrapper isw(*input_it->contents.get());
    rapidjson::Document input_doc;
    rapidjson::ParseResult parse_result = input_doc.ParseStream(isw);
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

    for (auto value_it = input_doc.MemberBegin();
         value_it != input_doc.MemberEnd(); ++value_it) {
      if (merged.HasMember(value_it->name)) {
        errors << input_it->name << " has a conflicting value for key \""
               << value_it->name.GetString() << "\"!\n";
        return 1;
      }
      merged.AddMember(value_it->name,
                       rapidjson::Value(value_it->value, allocator).Move(),
                       allocator);
    }
  }

  rapidjson::OStreamWrapper osw(output);
  if (minify) {
    rapidjson::Writer<rapidjson::OStreamWrapper> writer(osw);
    merged.Accept(writer);
  } else {
    rapidjson::PrettyWriter<rapidjson::OStreamWrapper> writer(osw);
    merged.Accept(writer);
  }

  return 0;
}
