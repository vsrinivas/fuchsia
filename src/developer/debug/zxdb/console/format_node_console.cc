// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/format_node_console.h"

#include "src/developer/debug/zxdb/common/ref_ptr_to.h"
#include "src/developer/debug/zxdb/expr/format.h"
#include "src/developer/debug/zxdb/expr/format_node.h"
#include "src/lib/fxl/memory/ref_counted.h"

namespace zxdb {

namespace {

// Provides a way to execute a single callback when a set of deferred actions is complete.
class AggregateDeferredCallback : public fxl::RefCountedThreadSafe<AggregateDeferredCallback> {
 public:
  // Returns a subordinate callback, the completion of which will contribute to the outer callback.
  fit::deferred_callback MakeSubordinate() {
    // This callback exists only to hold the bound reference to the outer callback.
    return fit::defer_callback([ref = RefPtrTo(this)]() {});
  }

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(AggregateDeferredCallback);
  FRIEND_MAKE_REF_COUNTED(AggregateDeferredCallback);

  explicit AggregateDeferredCallback(fit::deferred_callback cb) : outer_(std::move(cb)) {}

  fit::deferred_callback outer_;
};

// Some state needs to be tracked recursively as we iterate through the tree.
struct RecursiveState {
  // Forces various information off for exactly one level of printing. Use for array printing, for
  // example, to avoid duplicating the type information for every entry.
  bool inhibit_one_name = false;
  bool inhibit_one_type = false;

  // When one node redirects to a child (as in pointers), the child node does not get separate
  // indenting when it starts because we want to continue where the enclosing node left off.
  // This flag indicates this state.
  //
  // Set with AdvanceTransparentNesting().
  bool transparent_nesting = false;

  // How deep in the node tree we are, the top level is 0.
  int tree_depth = 0;

  // How many levels of visual indent we are, the top level is 0. This is often the same as the
  // tree depth but some things like pointer resolution are different levels of the tree but
  // are presented as the same visual level.
  int indent_depth = 0;

  // Number of pointers we've expanded so far in this level of recursion.
  int pointer_depth = 0;

  // Returns a modified version of the state for one additional level of recursion, advancing
  // both indent and tree depth.
  RecursiveState Advance() const;

  // Advances the tree depth but not the visual nesting depth.
  RecursiveState AdvanceTransparentNesting() const;
};

RecursiveState RecursiveState::Advance() const {
  RecursiveState result = *this;
  result.inhibit_one_name = false;
  result.inhibit_one_type = false;
  result.transparent_nesting = false;

  result.tree_depth++;
  result.indent_depth++;

  return result;
}

RecursiveState RecursiveState::AdvanceTransparentNesting() const {
  RecursiveState result = Advance();
  result.indent_depth--;  // Undo visual indent from Advance().
  result.transparent_nesting = true;
  return result;
}

void RecursiveDescribeFormatNode(FormatNode* node, const ConsoleFormatOptions& options,
                                 fxl::RefPtr<EvalContext> context, const RecursiveState& state,
                                 fit::deferred_callback cb) {
  if (state.tree_depth == options.max_depth)
    return;  // Reached max depth, give up.

  FillFormatNodeDescription(
      node, options, context,
      fit::defer_callback([weak_node = node->GetWeakPtr(), options, context, cb = std::move(cb),
                           state = state.Advance()]() mutable {
        // Description is complete.
        if (!weak_node || weak_node->children().empty())
          return;

        // Check for pointer expansion to avoid recursing into too many.
        if (weak_node->description_kind() == FormatNode::kPointer) {
          if (state.pointer_depth == options.pointer_expand_depth)
            return;  // Don't recurse into another level of pointer.
          state.pointer_depth++;
        }

        // Recurse into children.
        auto aggregator = fxl::MakeRefCounted<AggregateDeferredCallback>(std::move(cb));
        for (const auto& child : weak_node->children()) {
          RecursiveDescribeFormatNode(child.get(), options, context, state,
                                      aggregator->MakeSubordinate());
        }
      }));
}

void AppendNode(const FormatNode* node, const ConsoleFormatOptions& options,
                const RecursiveState& state, OutputBuffer* out);

// Returns true if the combination of options means the type should be shown.
bool TypeForcedOn(const ConsoleFormatOptions& options, const RecursiveState& state) {
  return !state.inhibit_one_type && options.verbosity == ConsoleFormatOptions::Verbosity::kAllTypes;
}

// Get a possibly-elided version of the type name for a medium verbosity level.
std::string GetElidedTypeName(const ConsoleFormatOptions& options, const std::string& name) {
  if (options.verbosity != ConsoleFormatOptions::Verbosity::kMinimal)
    return name;  // No eliding except in minimal mode.

  // Pick different thresholds depending on if we're doing multiline or not. The threshold and
  // the eliding length are different so we don't elide one or two characters.
  size_t always_allow_below = 64;
  size_t elide_to = 50;
  if (options.wrapping == ConsoleFormatOptions::Wrapping::kNone) {
    always_allow_below /= 2;
    elide_to /= 2;
  }

  // Names shorter than this are always displayed in full.
  if (name.size() <= always_allow_below)
    return name;

  return name.substr(0, elide_to) + "…";
}

// Checks if indentation is necessary and outputs the correct number of spaces if so.
//
// force_for_transparent_nesting will override the "transparent nesting" flag and do the indent.
// This is used for closing brackets which should always be indented, while for the opening
// a transparently nested thing just gets appended after its parent and the indent is skipped.
void AppendIndent(const ConsoleFormatOptions& options, const RecursiveState& state,
                  OutputBuffer* out, bool force_for_transparent_nesting = false) {
  if (options.wrapping == ConsoleFormatOptions::Wrapping::kExpanded &&
      (!state.transparent_nesting || force_for_transparent_nesting))
    out->Append(std::string(options.indent_amount * state.indent_depth, ' '));
}

// Appends the formatted name and "=" as needed by this node.
void AppendNodeNameAndType(const FormatNode* node, const ConsoleFormatOptions& options,
                           const RecursiveState& state, OutputBuffer* out) {
  AppendIndent(options, state, out);

  if (TypeForcedOn(options, state))
    out->Append(Syntax::kComment, "(" + node->type() + ") ");

  if (!state.inhibit_one_name && !node->name().empty()) {
    // Node name. Base class names are less important so get dimmed. Especially STL base classes
    // can get very long so we also elide them unless verbose mode is turned on.
    if (node->child_kind() == FormatNode::kBaseClass) {
      out->Append(Syntax::kComment, GetElidedTypeName(options, node->name()));
    } else {
      // Normal variable-style node name.
      out->Append(Syntax::kVariable, node->name());
    }
    out->Append(" = ");
  }
}

void AppendArray(const FormatNode* node, const ConsoleFormatOptions& options,
                 const RecursiveState& state, OutputBuffer* out) {
  if (node->children().empty()) {
    // Special-case the empty struct because we never want wrapping.
    out->Append("{}");
    return;
  }

  RecursiveState child_state = state.Advance();
  // In multiline mode, show the names of the array indices ("[0] = ...").
  child_state.inhibit_one_name = (options.wrapping == ConsoleFormatOptions::Wrapping::kNone);
  child_state.inhibit_one_type = true;  // Don't show the type for every child.

  out->Append("{");
  if (options.wrapping == ConsoleFormatOptions::Wrapping::kExpanded)
    out->Append("\n");

  for (size_t i = 0; i < node->children().size(); i++) {
    const FormatNode* child = node->children()[i].get();

    // Arrays can have "empty" nodes which use the name to indicate clipping ("...").
    if (child->state() == FormatNode::kEmpty)
      out->Append(Syntax::kComment, child->name());
    else
      AppendNode(child, options, child_state, out);

    // Append commas in single-line mode for all but the last one.
    if (i + 1 < node->children().size() &&
        options.wrapping == ConsoleFormatOptions::Wrapping::kNone)
      out->Append(", ");
  }
  AppendIndent(options, state, out, true);
  out->Append("}");
}

void AppendCollection(const FormatNode* node, const ConsoleFormatOptions& options,
                      const RecursiveState& state, OutputBuffer* out) {
  if (node->children().empty()) {
    // Special-case the empty array because we never want wrapping.
    out->Append("{}");
    return;
  }

  RecursiveState child_state = state.Advance();

  out->Append("{");
  if (options.wrapping == ConsoleFormatOptions::Wrapping::kExpanded)
    out->Append("\n");

  for (size_t i = 0; i < node->children().size(); i++) {
    AppendNode(node->children()[i].get(), options, child_state, out);

    // Append commas in single-line mode for all but the last one.
    if (i + 1 < node->children().size() &&
        options.wrapping == ConsoleFormatOptions::Wrapping::kNone)
      out->Append(", ");
  }
  AppendIndent(options, state, out, true);
  out->Append("}");
}

void AppendPointer(const FormatNode* node, const ConsoleFormatOptions& options,
                   const RecursiveState& state, OutputBuffer* out) {
  // When type information is forced on, the type will have already been printed. Otherwise we print
  // a "(*)" to indicate the value is a pointer.
  if (!TypeForcedOn(options, state))
    out->Append(Syntax::kComment, "(*)");

  // Pointers will have a child node that expands to the pointed-to value. If this is in a
  // "described" state with no error, then show the data.
  //
  // Don't show errors dereferencing pointers here since for long structure listings with
  // potentially multiple bad pointers, these get very verbose. The user can print the specific
  // value and see the error if desired.
  if (node->children().empty() || node->children()[0]->state() != FormatNode::kDescribed ||
      node->children()[0]->err().has_error()) {
    // Just have the pointer address.
    out->Append(node->description());
  } else {
    // Have the pointed-to data, dim the address and show the data. Omit types because we already
    // printed the pointer type. We do not want to indent another level.
    RecursiveState child_state = state.AdvanceTransparentNesting();
    child_state.inhibit_one_name = true;
    child_state.inhibit_one_type = true;

    out->Append(Syntax::kComment, node->description() + " 🡺 ");
    AppendNode(node->children()[0].get(), options, child_state, out);
  }
}

void AppendReference(const FormatNode* node, const ConsoleFormatOptions& options,
                     const RecursiveState& state, OutputBuffer* out) {
  // References will have a child node that expands to the referenced value. If this is in a
  // "described" state, then we have the data and should show it. Otherwise omit this.
  if (node->children().empty() || node->children()[0]->state() != FormatNode::kDescribed) {
    // The caller should have expanded the references if it wants them shown. If not, display
    // a placeholder with the address. Show the whole thing dimmed to help indicate this isn't the
    // actual value.
    out->Append(Syntax::kComment, "(&)" + node->description());
  } else {
    // Have the pointed-to data, just show the value. We do not want to indent another level.
    RecursiveState child_state = state.AdvanceTransparentNesting();
    child_state.inhibit_one_name = true;
    child_state.inhibit_one_type = true;

    AppendNode(node->children()[0].get(), options, child_state, out);
  }
}

// Appends the description for a normal node (number or whatever).
void AppendStandard(const FormatNode* node, const ConsoleFormatOptions& options,
                    const RecursiveState& state, OutputBuffer* out) {
  out->Append(node->description());
}

void AppendNode(const FormatNode* node, const ConsoleFormatOptions& options,
                const RecursiveState& state, OutputBuffer* out) {
  AppendNodeNameAndType(node, options, state, out);
  if (state.tree_depth == options.max_depth) {
    // Hit max depth, give up.
    out->Append(Syntax::kComment, "…");
  } else if (node->err().has_error()) {
    // Write the error.
    out->Append(Syntax::kComment, "<" + node->err().msg() + ">");
  } else {
    // Normal formatting.
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
      case FormatNode::kRustTuple:
        // TODO(brettw): need more specific formatting. For now just output the default.
        AppendStandard(node, options, state, out);
        break;
    }
  }

  // Transparent nesting means that this node doesn't get its own separate indenting or newlines,
  // these will be handled by the enclosing one.
  if (options.wrapping == ConsoleFormatOptions::Wrapping::kExpanded && !state.transparent_nesting)
    out->Append("\n");
}

}  // namespace

void DescribeFormatNodeForConsole(FormatNode* node, const ConsoleFormatOptions& options,
                                  fxl::RefPtr<EvalContext> context, fit::deferred_callback cb) {
  RecursiveDescribeFormatNode(node, options, context, RecursiveState(), std::move(cb));
}

OutputBuffer FormatNodeForConsole(const FormatNode& node, const ConsoleFormatOptions& options) {
  OutputBuffer out;
  AppendNode(&node, options, RecursiveState(), &out);
  return out;
}

fxl::RefPtr<AsyncOutputBuffer> FormatValueForConsole(ExprValue value,
                                                     const ConsoleFormatOptions& options,
                                                     fxl::RefPtr<EvalContext> context,
                                                     const std::string& value_name) {
  auto node = std::make_unique<FormatNode>(value_name, std::move(value));
  FormatNode* node_ptr = node.get();

  auto out = fxl::MakeRefCounted<AsyncOutputBuffer>();
  DescribeFormatNodeForConsole(node_ptr, options, context,
                               fit::defer_callback([node = std::move(node), options, out]() {
                                 // Asynchronous expansion is complete, now format the completed
                                 // output.
                                 out->Complete(FormatNodeForConsole(*node, options));
                               }));
  return out;
}

fxl::RefPtr<AsyncOutputBuffer> FormatVariableForConsole(const Variable* var,
                                                        const ConsoleFormatOptions& options,
                                                        fxl::RefPtr<EvalContext> context) {
  auto out = fxl::MakeRefCounted<AsyncOutputBuffer>();
  context->GetVariableValue(
      RefPtrTo(var), [name = var->GetAssignedName(), context, options, out](
                         const Err& err, fxl::RefPtr<Symbol>, ExprValue value) {
        // Variable value resolved. Print it.
        out->Complete(FormatValueForConsole(std::move(value), options, context, name));
      });
  return out;
}

}  // namespace zxdb
