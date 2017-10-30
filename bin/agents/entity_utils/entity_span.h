// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_AGENTS_ENTITY_UTILS_ENTITY_SPAN_H_
#define PERIDOT_BIN_AGENTS_ENTITY_UTILS_ENTITY_SPAN_H_

#include <string>
#include <vector>

#include "lib/context/fidl/value.fidl.h"

namespace maxwell {

// An entity and its location in the Context Engine, under topic: "raw/text".
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
  EntitySpan(const std::string& content,
             const std::string& type,
             const int start,
             const int end);

  // Constructs a new EntitySpan by parsing a JSON-formatted string with the
  // fields given in the above constructor as keys.
  static EntitySpan FromJson(const std::string& json_string);

  static std::vector<EntitySpan> FromContextValues(
      const fidl::Array<ContextValuePtr>& values);

  std::string GetContent() const { return content_; }
  std::string GetType() const { return type_; }
  int GetStart() const { return start_; }
  int GetEnd() const { return end_; }

  // Return this Entity as a JSON-formatted string.
  std::string GetJsonString() const { return json_string_; }

 private:
  // A common initialization method called by constructors.
  void Init(const std::string& name,
            const std::string& type,
            const int start,
            const int end);

  std::string content_;
  std::string type_;
  int start_;
  int end_;
  std::string json_string_;
};

}  // namespace maxwell

#endif  // PERIDOT_BIN_AGENTS_ENTITY_UTILS_ENTITY_SPAN_H_
