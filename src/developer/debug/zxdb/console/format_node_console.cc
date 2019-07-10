// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/format_node_console.h"

#include "src/developer/debug/zxdb/expr/format_expr_value_options.h"
#include "src/developer/debug/zxdb/expr/format_node.h"

namespace zxdb {

namespace {

// Some state needs to be tracked recursively as we iterate through the tree.
struct RecursiveState {
  // Forces name/type information off for exactly one level of printing. Use for array printing,
  // for example, to avoid duplicating the type information for every entry.
  bool inhibit_one_name = false;
  bool inhibit_one_type = false;

  // Returns a modified version of the state for one additional level of recursion.
  RecursiveState Advance() const;
};

RecursiveState RecursiveState::Advance() const {
  RecursiveState result = *this;
  result.inhibit_one_name = false;
  result.inhibit_one_type = false;

  return result;
}

void AppendNode(const FormatNode* node, const ConsoleFormatNodeOptions& options,
                const RecursiveState& state, OutputBuffer* out);

// Returns true if the combination of options means the type should be shown.
bool TypeForcedOn(const ConsoleFormatNodeOptions& options, const RecursiveState& state) {
  return !state.inhibit_one_type &&
         options.verbosity == FormatExprValueOptions::Verbosity::kAllTypes;
}

// Appends the formatted name and "=" as needed by this node.
void AppendNodeNameAndType(const FormatNode* node, const ConsoleFormatNodeOptions& options,
                           const RecursiveState& state, OutputBuffer* out) {
  if (TypeForcedOn(options, state))
    out->Append(Syntax::kComment, "(" + node->type() + ") ");

  if (!state.inhibit_one_name && !node->name().empty()) {
    out->Append(Syntax::kVariable, node->name());
    out->Append(" = ");
  }
}

void AppendArray(const FormatNode* node, const ConsoleFormatNodeOptions& options,
                 const RecursiveState& state, OutputBuffer* out) {
  RecursiveState child_state = state.Advance();
  child_state.inhibit_one_name = true;
  child_state.inhibit_one_type = true;  // Don't show the type for every child.

  out->Append("{");
  bool needs_comma = false;
  for (const auto& child : node->children()) {
    if (needs_comma)
      out->Append(", ");
    else
      needs_comma = true;

    // Arrays can have "empty" nodes which use the name to indicate clipping ("...").
    if (child->state() == FormatNode::kEmpty)
      out->Append(Syntax::kComment, child->name());
    else
      AppendNode(child.get(), options, child_state, out);
  }
  out->Append("}");
}

void AppendCollection(const FormatNode* node, const ConsoleFormatNodeOptions& options,
                      const RecursiveState& state, OutputBuffer* out) {
  RecursiveState child_state = state.Advance();

  out->Append("{");
  bool needs_comma = false;
  for (const auto& child : node->children()) {
    if (needs_comma)
      out->Append(", ");
    else
      needs_comma = true;

    AppendNode(child.get(), options, child_state, out);
  }
  out->Append("}");
}

void AppendPointer(const FormatNode* node, const ConsoleFormatNodeOptions& options,
                   const RecursiveState& state, OutputBuffer* out) {
  // When type information is forced on, the type will have already been printed. Otherwise we print
  // a "(*)" to indicate the value is a pointer.
  if (!TypeForcedOn(options, state))
    out->Append(Syntax::kComment, "(*)");

  // Pointers will have a child node that expands to the pointed-to value. If this is in a
  // "described" state, then we have the data and should show it. Otherwise omit this.
  if (node->children().empty() || node->children()[0]->state() != FormatNode::kDescribed) {
    // Just have the pointer address.
    out->Append(node->description());
  } else {
    // Have the pointed-to data, dim the address and show the data. Omit types because we already
    // printed the pointer type.
    RecursiveState child_state = state.Advance();
    child_state.inhibit_one_name = true;
    child_state.inhibit_one_type = true;

    out->Append(Syntax::kComment, node->description() + " 🡺 ");
    AppendNode(node->children()[0].get(), options, child_state, out);
  }
}

void AppendReference(const FormatNode* node, const ConsoleFormatNodeOptions& options,
                     const RecursiveState& state, OutputBuffer* out) {
  // References will have a child node that expands to the referenced value. If this is in a
  // "described" state, then we have the data and should show it. Otherwise omit this.
  if (node->children().empty() || node->children()[0]->state() != FormatNode::kDescribed) {
    // The caller should have expanded the references if it wants them shown. If not, display
    // a placeholder with the address. Show the whole thing dimmed to help indicate this isn't the
    // actual value.
    out->Append(Syntax::kComment, "(&)" + node->description());
  } else {
    // Have the pointed-to data, just show the value.
    RecursiveState child_state = state.Advance();
    child_state.inhibit_one_name = true;
    child_state.inhibit_one_type = true;

    AppendNode(node->children()[0].get(), options, child_state, out);
  }
}

// Appends the description for a normal node (number or whatever).
void AppendStandard(const FormatNode* node, const ConsoleFormatNodeOptions& options,
                    const RecursiveState& state, OutputBuffer* out) {
  out->Append(node->description());
}

void AppendNode(const FormatNode* node, const ConsoleFormatNodeOptions& options,
                const RecursiveState& state, OutputBuffer* out) {
  AppendNodeNameAndType(node, options, state, out);

  switch (node->description_kind()) {
    case FormatNode::kNone:
    case FormatNode::kBaseType:
    case FormatNode::kFunctionPointer:
    case FormatNode::kOther:
    case FormatNode::kString:
      // All these things just print the description only.
      AppendStandard(node, options, state, out);
      break;
    case FormatNode::kArray:
      AppendArray(node, options, state, out);
      break;
    case FormatNode::kCollection:
      AppendCollection(node, options, state, out);
      break;
    case FormatNode::kPointer:
      AppendPointer(node, options, state, out);
      break;
    case FormatNode::kReference:
      AppendReference(node, options, state, out);
      break;
    case FormatNode::kRustEnum:
      // TODO(brettw)
      break;
    case FormatNode::kRustTuple:
      // TODO(brettw)
      break;
  }
}

}  // namespace

OutputBuffer FormatNodeForConsole(const FormatNode& node, const ConsoleFormatNodeOptions& options) {
  OutputBuffer out;
  AppendNode(&node, options, RecursiveState(), &out);
  return out;
}

}  // namespace zxdb
