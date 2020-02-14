// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/interpreter/src/nodes.h"

#include "src/developer/shell/interpreter/src/interpreter.h"

namespace shell {
namespace interpreter {

Node::Node(Interpreter* interpreter, uint64_t file_id, uint64_t node_id)
    : interpreter_(interpreter), id_(file_id, node_id) {
  interpreter->AddNode(file_id, node_id, this);
}

Node::~Node() { interpreter_->RemoveNode(id_.file_id, id_.node_id); }

}  // namespace interpreter
}  // namespace shell
