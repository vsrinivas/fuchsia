// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>

namespace maxwell {

// An entity and its location in the Context Engine, under topic:
// /story/focused/explicit/raw/text.
// For example, this could be an email address from the basic_text_reporter
// module.
// TODO(travismart): When functionality improves to deal with multiple Flutter
// widgets, add fields about which text this EntitySpan came from.
class EntitySpan {
 public:
  // Constructs a new EntitySpan with the provided content, type and bounds.
  // |content| denotes the entity content, e.g. an email address.
  // |type| denotes the entity type (e.g. "email").
  // |start| and |end| represent the character offsets within the source string
  // (|end| points to the character after |content|).
  EntitySpan(std::string content, std::string type, int start, int end);

  std::string GetContent() const { return content_; }
  std::string GetType() const { return type_; }
  int GetStart() const { return start_; }
  int GetEnd() const { return end_; }

  // Return this Entity as a JSON-formatted string.
  std::string GetJsonString() const { return json_string_; }

 private:
  std::string content_;
  std::string type_;
  int start_;
  int end_;
  std::string json_string_;
};

}  // namespace maxwell
