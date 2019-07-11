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
  // Forces name/type information off for exactly one level of printing. Use for array printing,
  // for example, to avoid duplicating the type information for every entry.
  bool inhibit_one_name = false;
  bool inhibit_one_type = false;

  // Counter for the levels of nesting. The top level is 0.
  int depth = 0;

  // Number of pointers we've expanded so far in this level of recursion.
  int pointer_depth = 0;

  // Returns a modified version of the state for one additional level of recursion.
  RecursiveState Advance() const;
};

RecursiveState RecursiveState::Advance() const {
  RecursiveState result = *this;
  result.inhibit_one_name = false;
  result.inhibit_one_type = false;

  result.depth++;

  return result;
}

void RecursiveDescribeFormatNode(FormatNode* node, const ConsoleFormatOptions& options,
                                 fxl::RefPtr<EvalContext> context, const RecursiveState& state,
                                 fit::deferred_callback cb) {
  if (state.depth == options.max_depth)
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
std::string GetElidedTypeName(const std::string& name) {
  // Names shorter than this are always displayed in full.
  if (name.size() <= 32)
    return name;

  // This value was picked to be smaller than the above value so we don't elide one or two
  // characters (which looks dumb). It was selected to be long enough so that with the common prefix
  // of "std::__2::" (which occurs on many long types), you still get enough to read approximately
  // what the type is.
  return name.substr(0, 20) + "…";
}

// Appends the formatted name and "=" as needed by this node.
void AppendNodeNameAndType(const FormatNode* node, const ConsoleFormatOptions& options,
                           const RecursiveState& state, OutputBuffer* out) {
  if (TypeForcedOn(options, state))
    out->Append(Syntax::kComment, "(" + node->type() + ") ");

  if (!state.inhibit_one_name && !node->name().empty()) {
    // Node name. Base class names are less important so get dimmed. Especially STL base classes
    // can get very long so we also elide them unless verbose mode is turned on.
    if (node->child_kind() == FormatNode::kBaseClass) {
      if (options.verbosity == ConsoleFormatOptions::Verbosity::kMinimal)
        out->Append(Syntax::kComment, GetElidedTypeName(node->name()));
      else
        out->Append(Syntax::kComment, node->name());
    } else {
      // Normal variable-style node name.
      out->Append(Syntax::kVariable, node->name());
    }
    out->Append(" = ");
  }
}

void AppendArray(const FormatNode* node, const ConsoleFormatOptions& options,
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

void AppendCollection(const FormatNode* node, const ConsoleFormatOptions& options,
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
    // printed the pointer type.
    RecursiveState child_state = state.Advance();
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
    // Have the pointed-to data, just show the value.
    RecursiveState child_state = state.Advance();
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
  if (state.depth == options.max_depth) {
    // Hit max depth, give up.
    out->Append(Syntax::kComment, "…");
    return;
  }

  if (node->err().has_error()) {
    // Write the error.
    out->Append(Syntax::kComment, "<" + node->err().msg() + ">");
    return;
  }

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
