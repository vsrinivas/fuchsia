// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_EXAMPLES_DEMO_TEXT_NODE_INCLUDE_TEXT_NODE_H_
#define SRC_CAMERA_EXAMPLES_DEMO_TEXT_NODE_INCLUDE_TEXT_NODE_H_

#include <lib/ui/scenic/cpp/commands.h>
#include <lib/ui/scenic/cpp/resources.h>

class TextNode : public scenic::Node {
 public:
  explicit TextNode(scenic::Session* session);
  TextNode(TextNode&& moved);
  ~TextNode();

  // Sets the text contents (lower 128 ASCII set) of the node.
  zx_status_t SetText(const std::string s);
};

#endif  // SRC_CAMERA_EXAMPLES_DEMO_TEXT_NODE_INCLUDE_TEXT_NODE_H_
