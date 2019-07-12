// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/format_node_console.h"

#include "src/developer/debug/zxdb/common/ref_ptr_to.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
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
  explicit RecursiveState(const ConsoleFormatOptions& opts) : options(opts) {}

  // Returns a modified version of the state for one additional level of recursion, advancing
  // both indent and tree depth.
  RecursiveState Advance() const;

  // Advances the tree depth but not the visual nesting depth.
  RecursiveState AdvanceNoIndent() const;

  // Returns true if the combination of options means the type should be shown.
  bool TypeForcedOn() const {
    return !inhibit_one_type && options.verbosity == ConsoleFormatOptions::Verbosity::kAllTypes;
  }

  // Returns true if the current item should be formatted in expanded tree mode or in one line.
  bool ShouldExpand() const {
    return options.wrapping == ConsoleFormatOptions::Wrapping::kExpanded ||
           (options.wrapping == ConsoleFormatOptions::Wrapping::kSmart && smart_indent_is_expanded);
  }

  int GetIndentAmount() const {
    if (!ShouldExpand())
      return 0;
    return options.indent_amount * indent_depth;
  }

  // Returns the string to indent a node at the given level.
  std::string GetIndentString() const { return std::string(GetIndentAmount(), ' '); }

  const ConsoleFormatOptions options;

  // Forces various information off for exactly one level of printing. Use for array printing, for
  // example, to avoid duplicating the type information for every entry.
  bool inhibit_one_name = false;
  bool inhibit_one_type = false;

  // How deep in the node tree we are, the top level is 0.
  int tree_depth = 0;

  // How many levels of visual indent we are, the top level is 0. This is often the same as the
  // tree depth but some things like pointer resolution are different levels of the tree but
  // are presented as the same visual level.
  int indent_depth = 0;

  // Number of pointers we've expanded so far in this level of recursion.
  int pointer_depth = 0;

  // Controls what mode of smart indenting (single-line or expanded) the current node should be
  // formatted in.
  bool smart_indent_is_expanded = true;
};

RecursiveState RecursiveState::Advance() const {
  RecursiveState result = *this;
  result.inhibit_one_name = false;
  result.inhibit_one_type = false;

  result.tree_depth++;
  result.indent_depth++;

  return result;
}

RecursiveState RecursiveState::AdvanceNoIndent() const {
  RecursiveState result = Advance();
  result.indent_depth--;  // Undo visual indent from Advance().
  return result;
}

void RecursiveDescribeFormatNode(FormatNode* node, fxl::RefPtr<EvalContext> context,
                                 const RecursiveState& state, fit::deferred_callback cb) {
  if (state.tree_depth == state.options.max_depth)
    return;  // Reached max depth, give up.

  FillFormatNodeDescription(
      node, state.options, context,
      fit::defer_callback([weak_node = node->GetWeakPtr(), context, cb = std::move(cb),
                           state = state.Advance()]() mutable {
        // Description is complete.
        if (!weak_node || weak_node->children().empty())
          return;

        // Check for pointer expansion to avoid recursing into too many.
        if (weak_node->description_kind() == FormatNode::kPointer) {
          if (state.pointer_depth == state.options.pointer_expand_depth)
            return;  // Don't recurse into another level of pointer.
          state.pointer_depth++;
        }

        // Recurse into children.
        auto aggregator = fxl::MakeRefCounted<AggregateDeferredCallback>(std::move(cb));
        for (const auto& child : weak_node->children()) {
          RecursiveDescribeFormatNode(child.get(), context, state, aggregator->MakeSubordinate());
        }
      }));
}

bool IsRust(const FormatNode* node) {
  if (!node->value().type())
    return false;
  return node->value().type()->GetLanguage() == DwarfLang::kRust;
}

// Forward decls, see the implementation for comments.
OutputBuffer DoFormatNode(const FormatNode* node, const RecursiveState& state, int line_start_char);
OutputBuffer DoFormatNodeOneWay(const FormatNode* node, const RecursiveState& state,
                                int indent_amount);

// Get a possibly-elided version of the type name for a medium verbosity level.
std::string GetElidedTypeName(const RecursiveState& state, const std::string& name) {
  if (state.options.verbosity != ConsoleFormatOptions::Verbosity::kMinimal)
    return name;  // No eliding except in minimal mode.

  // Pick different thresholds depending on if we're doing multiline or not. The threshold and
  // the eliding length are different so we don't elide one or two characters.
  size_t always_allow_below = 64;
  size_t elide_to = 50;
  if (!state.ShouldExpand()) {
    always_allow_below /= 2;
    elide_to /= 2;
  }

  // Names shorter than this are always displayed in full.
  if (name.size() <= always_allow_below)
    return name;

  return name.substr(0, elide_to) + "…";
}

// Rust collections (structs) normally have the type name before the bracket like "MyStruct{...}".
// This function appends this name.
//
// For some struct types including common standard containers the names can get very long. As a
// result in "minimal" verbosity mode we never show the namespaces and also elide template
// parameters when the template is long.
void AppendRustCollectionName(const FormatNode* node, const RecursiveState& state,
                              OutputBuffer* out) {
  if (!IsRust(node) || !node->value().type())
    return;

  if (state.options.verbosity != ConsoleFormatOptions::Verbosity::kMinimal) {
    // Use the full identifier name in the more verbose modes.
    out->Append(FormatIdentifier(node->value().type()->GetIdentifier(), false));
    return;
  }

  // In minimal mode, extract just the last component of the identifier.
  const Identifier& ident = node->value().type()->GetIdentifier();
  if (ident.empty())
    return;
  ParsedIdentifier parsed =
      ToParsedIdentifier(Identifier(IdentifierQualification::kRelative, ident.components().back()));

  if (parsed.components()[0].has_template()) {
    // Some containers have very long template names. Since Rust structs always have the name, the
    // lengths can get a little bit nuts. Elide the template contents when too long by converting
    // all template parameters to one string and eliding.
    const auto& template_contents = parsed.components()[0].template_contents();
    std::string template_string;
    for (size_t i = 0; i < template_contents.size(); i++) {
      if (i > 0)
        template_string += ", ";
      template_string += template_contents[i];
    }

    // Replace the template parameters with our single string, elided if necessary.
    parsed.components()[0] = ParsedIdentifierComponent(parsed.components()[0].name(),
                                                       {GetElidedTypeName(state, template_string)});
  }

  out->Append(FormatIdentifier(parsed, false));
}

// Writes the suffix for when there are multiple items in a collection or an array.
void AppendItemSuffix(const RecursiveState& state, bool last_item, OutputBuffer* out) {
  if (state.ShouldExpand())
    out->Append("\n");  // Expanded children always end with a newline.
  else if (!last_item)
    out->Append(", ");  // Get commas for everything but the last.
}

// Appends the formatted name and "=" as needed by this node.
void AppendNodeNameAndType(const FormatNode* node, const RecursiveState& state, OutputBuffer* out) {
  // Type name when forced on. Never show types here for Rust collections since those are always
  // printed as part of the colelction printing code after the name ("foo = StructName{...}").
  if (state.TypeForcedOn() &&
      !(node->description_kind() == FormatNode::kCollection && IsRust(node)))
    out->Append(Syntax::kComment, "(" + node->type() + ") ");

  if (!state.inhibit_one_name && !node->name().empty()) {
    // Node name. Base class names are less important so get dimmed. Especially STL base classes
    // can get very long so we also elide them unless verbose mode is turned on.
    if (node->child_kind() == FormatNode::kBaseClass) {
      out->Append(Syntax::kComment, GetElidedTypeName(state, node->name()));
    } else {
      // Normal variable-style node name.
      out->Append(Syntax::kVariable, node->name());
    }
    out->Append(" = ");
  }
}

OutputBuffer DoFormatArrayNode(const FormatNode* node, const RecursiveState& state) {
  // Special-case the empty struct because we never want wrapping.
  if (node->children().empty())
    return OutputBuffer("{}");

  RecursiveState child_state = state.Advance();
  // In multiline mode, show the names of the array indices ("[0] = ...").
  child_state.inhibit_one_name = !state.ShouldExpand();
  child_state.inhibit_one_type = true;  // Don't show the type for every child.
  int child_indent = child_state.GetIndentAmount();

  OutputBuffer out("{");
  if (state.ShouldExpand())
    out.Append("\n");

  for (size_t i = 0; i < node->children().size(); i++) {
    const FormatNode* child = node->children()[i].get();

    // Arrays can have "empty" nodes which use the name to indicate clipping ("...").
    if (child->state() == FormatNode::kEmpty)
      out.Append(Syntax::kComment, child->name());
    else
      out.Append(DoFormatNode(child, child_state, child_indent));

    // Separator (comma or newline).
    AppendItemSuffix(state, i + 1 == node->children().size(), &out);
  }
  out.Append(state.GetIndentString() + "}");
  return out;
}

OutputBuffer DoFormatCollectionNode(const FormatNode* node, const RecursiveState& state) {
  OutputBuffer out;

  // Rust structs are always prefixed with the struct name. This does nothing for non-Rust.
  AppendRustCollectionName(node, state, &out);

  // Special-case empty collections because we never want wrapping.
  if (node->children().empty()) {
    // Rust empty structs have no brackets.
    if (!IsRust(node))
      out.Append("{}");
    return out;
  }

  RecursiveState child_state = state.Advance();
  int child_indent = child_state.GetIndentAmount();

  out.Append("{");
  if (state.ShouldExpand())
    out.Append("\n");

  for (size_t i = 0; i < node->children().size(); i++) {
    out.Append(DoFormatNode(node->children()[i].get(), child_state, child_indent));

    // Separator (comma or newline).
    AppendItemSuffix(state, i + 1 == node->children().size(), &out);
  }
  out.Append(state.GetIndentString() + "}");
  return out;
}

OutputBuffer DoFormatPointerNode(const FormatNode* node, const RecursiveState& state) {
  OutputBuffer out;

  // When type information is forced on, the type will have already been printed. Otherwise we print
  // a "(*)" to indicate the value is a pointer.
  if (!state.TypeForcedOn())
    out.Append(Syntax::kComment, "(*)");

  // Pointers will have a child node that expands to the pointed-to value. If this is in a
  // "described" state with no error, then show the data.
  //
  // Don't show errors dereferencing pointers here since for long structure listings with
  // potentially multiple bad pointers, these get very verbose. The user can print the specific
  // value and see the error if desired.
  if (node->children().empty() || node->children()[0]->state() != FormatNode::kDescribed ||
      node->children()[0]->err().has_error()) {
    // Just have the pointer address.
    out.Append(node->description());
  } else {
    // Have the pointed-to data, dim the address and show the data. Omit types because we already
    // printed the pointer type. We do not want to indent another level.
    RecursiveState child_state = state.AdvanceNoIndent();
    child_state.inhibit_one_name = true;
    child_state.inhibit_one_type = true;

    out.Append(Syntax::kComment, node->description() + " 🡺 ");
    // Use the "one way" version to propagate our formatting mode. This node cant't get different
    // formatting than its child.
    out.Append(DoFormatNodeOneWay(node->children()[0].get(), child_state, 0));
  }
  return out;
}

OutputBuffer DoFormatReferenceNode(const FormatNode* node, const RecursiveState& state) {
  // References will have a child node that expands to the referenced value. If this is in a
  // "described" state, then we have the data and should show it. Otherwise omit this.
  if (node->children().empty() || node->children()[0]->state() != FormatNode::kDescribed) {
    // The caller should have expanded the references if it wants them shown. If not, display
    // a placeholder with the address. Show the whole thing dimmed to help indicate this isn't the
    // actual value.
    return OutputBuffer(Syntax::kComment, "(&)" + node->description());
  }

  // Have the pointed-to data, just show the value. We do not want to indent another level.
  RecursiveState child_state = state.AdvanceNoIndent();
  child_state.inhibit_one_name = true;
  child_state.inhibit_one_type = true;

  // Use the "one way" version to propagate our formatting mode. This node cant't get different
  // formatting than its child.
  return DoFormatNodeOneWay(node->children()[0].get(), child_state, 0);
}

// Appends the description for a normal node (number or whatever).
OutputBuffer DoFormatStandardNode(const FormatNode* node, const RecursiveState& state) {
  return OutputBuffer(node->description());
}

// Inner implementation for DoFormatNode() (see that for more). This does the actual formatting one
// time according to the current formatting options and state.
//
// Some callers may want to call this directly rather than DoFormatNode() if they want to bypass the
// two-pass formatting. This is useful for pointers which are two nodes (when auto dereferenced) but
// appear as one and the expansion mode of the outer should be passed transparently to the nested
// node.
OutputBuffer DoFormatNodeOneWay(const FormatNode* node, const RecursiveState& state,
                                int indent_amount) {
  OutputBuffer out;

  if (indent_amount)
    out.Append(std::string(indent_amount, ' '));

  AppendNodeNameAndType(node, state, &out);
  if (state.tree_depth == state.options.max_depth) {
    // Hit max depth, give up.
    out.Append(Syntax::kComment, "…");
  } else if (node->err().has_error()) {
    // Write the error.
    out.Append(Syntax::kComment, "<" + node->err().msg() + ">");
  } else {
    // Normal formatting.
    switch (node->description_kind()) {
      case FormatNode::kNone:
      case FormatNode::kBaseType:
      case FormatNode::kFunctionPointer:
      case FormatNode::kOther:
      case FormatNode::kString:
        // All these things just print the description only.
        out.Append(DoFormatStandardNode(node, state));
        break;
      case FormatNode::kArray:
        out.Append(DoFormatArrayNode(node, state));
        break;
      case FormatNode::kCollection:
        out.Append(DoFormatCollectionNode(node, state));
        break;
      case FormatNode::kPointer:
        out.Append(DoFormatPointerNode(node, state));
        break;
      case FormatNode::kReference:
        out.Append(DoFormatReferenceNode(node, state));
        break;
      case FormatNode::kRustEnum:
      case FormatNode::kRustTuple:
        // TODO(brettw): need more specific formatting. For now just output the default.
        out.Append(DoFormatStandardNode(node, state));
        break;
    }
  }

  return out;
}

// Formats one node. This layer provides the smart wrapping logic where we may try to format an
// item twice, first to see if it fits on one line, and possibly again ig it doesn't.
//
// The caller of this function is in charge of computing (but not inserting) the appropriate level
// of indent and also appending trailing newlines if necessary.
//
// The caller must compute the indenting amount because whether or not there's indenting for this
// node depends the expansion level of the caller, not this node. (This item could be in single-line
// mode and still be indented if the caller is expanded.)
OutputBuffer DoFormatNode(const FormatNode* node, const RecursiveState& state, int indent_amount) {
  if (state.options.wrapping != ConsoleFormatOptions::Wrapping::kSmart)
    return DoFormatNodeOneWay(node, state, indent_amount);  // Nothing fancy to do.

  if (state.smart_indent_is_expanded) {
    // The previous node was in an expanded state. First try to format this one as non-expanded.
    // to see if it fits.
    RecursiveState one_line_state = state;
    one_line_state.smart_indent_is_expanded = false;

    OutputBuffer one_line = DoFormatNodeOneWay(node, one_line_state, indent_amount);
    if (static_cast<int>(one_line.UnicodeCharWidth()) <= state.options.smart_indent_cols)
      return one_line;  // Fits.

    // Too big, format as expanded.
    return DoFormatNodeOneWay(node, state, indent_amount);
  }

  // The previous node was already in one-line mode, stay in that mode.
  return DoFormatNodeOneWay(node, state, indent_amount);
}

}  // namespace

void DescribeFormatNodeForConsole(FormatNode* node, const ConsoleFormatOptions& options,
                                  fxl::RefPtr<EvalContext> context, fit::deferred_callback cb) {
  RecursiveDescribeFormatNode(node, context, RecursiveState(options), std::move(cb));
}

OutputBuffer FormatNodeForConsole(const FormatNode& node, const ConsoleFormatOptions& options) {
  return DoFormatNode(&node, RecursiveState(options), 0);
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
