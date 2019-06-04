// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/a11y/tests/semantic_tree_parser.h"

#include "src/lib/fxl/logging.h"

namespace accessibility_test {
bool SemanticTreeParser::ParseSemanticTree(
    const std::string &file_path,
    std::vector<fuchsia::accessibility::semantics::Node> *semantic_tree) {
  FXL_CHECK(semantic_tree);

  // Read the file.
  rapidjson::Document document = json_parser_.ParseFromFile(file_path);
  if (json_parser_.HasError()) {
    FXL_LOG(ERROR) << "Error parsing file:" << file_path;
    return false;
  }
  for (auto &node_object : document.GetArray()) {
    fuchsia::accessibility::semantics::Node node =
        fuchsia::accessibility::semantics::Node();
    node.set_node_id(node_object["id"].GetInt());
    for (auto &child_id : node_object["child_ids"].GetArray()) {
      node.mutable_child_ids()->push_back(child_id.GetInt());
    }
    auto attribute = node_object["attributes"].GetObject();
    node.set_attributes(fuchsia::accessibility::semantics::Attributes());
    node.mutable_attributes()->set_label(attribute["label"].GetString());
    semantic_tree->push_back(std::move(node));
  }

  return true;
}
}  // namespace accessibility_test
