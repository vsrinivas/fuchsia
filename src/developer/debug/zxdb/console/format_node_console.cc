// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/format_node_console.h"

#include "src/developer/debug/zxdb/common/ref_ptr_to.h"
#include "src/developer/debug/zxdb/common/string_util.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/format_name.h"
#include "src/developer/debug/zxdb/console/string_util.h"
#include "src/developer/debug/zxdb/expr/format.h"
#include "src/developer/debug/zxdb/expr/format_node.h"
#include "src/developer/debug/zxdb/symbols/variable.h"
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

  // Returns true if items at this level should be elided because the printing depth is too deep.
  bool DepthTooDeep() const { return tree_depth >= options.max_depth; }

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

  auto on_complete = fit::defer_callback([weak_node = node->GetWeakPtr(), context,
                                          cb = std::move(cb), state = state.Advance()]() mutable {
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
    for (const auto& child : weak_node->children())
      RecursiveDescribeFormatNode(child.get(), context, state, aggregator->MakeSubordinate());
  });

  if (node->state() != FormatNode::kDescribed)
    FillFormatNodeDescription(node, state.options, context, std::move(on_complete));
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
    FormatIdentifierOptions opts;
    opts.show_global_qual = false;
    opts.elide_templates = false;
    opts.bold_last = false;
    out->Append(FormatIdentifier(node->value().type()->GetIdentifier(), opts));
    return;
  }

  // Special-case "Vec" with the macro that's normally used and omit the type.
  if (StringStartsWith(node->type(), "alloc::vec::Vec<")) {
    out->Append("vec!");
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

  FormatIdentifierOptions ident_opts;
  ident_opts.show_global_qual = false;
  ident_opts.elide_templates = false;
  ident_opts.bold_last = false;
  out->Append(FormatIdentifier(parsed, ident_opts));
}

// Writes the suffix for when there are multiple items in a collection or an array.
void AppendItemSuffix(const RecursiveState& state, bool last_item, OutputBuffer* out) {
  if (state.ShouldExpand())
    out->Append("\n");  // Expanded children always end with a newline.
  else if (!last_item)
    out->Append(Syntax::kOperatorNormal, ", ");  // Get commas for everything but the last.
}

// Returns true if type information should be displayed before the variable name, as in
//   <TYPE> name = value
bool ShouldPrependTypeNameBeforeName(const FormatNode* node, const RecursiveState& state) {
  if (!state.TypeForcedOn())
    return false;  // Never show types unless requested.

  if (node->type().empty())
    return false;  // Don't show empty types.

  if (!IsRust(node))
    return true;  // Non-Rust code always gets the type.

  if (node->description_kind() == FormatNode::kCollection ||
      node->description_kind() == FormatNode::kArray ||
      node->description_kind() == FormatNode::kRustTuple ||
      node->description_kind() == FormatNode::kRustTupleStruct) {
    // Rust structs and tuple structs are special. They encode the type after the variable, so:
    // "foo = StructName{...}" and we don't want to prepend it before "foo". But this doesn't happen
    // for tuples.
    //
    // Rust tuples don't have the name shown ever. The individual members will have their types
    // displayed which will encode the type of the tuple.
    return false;
  }

  return true;
}

// Appends the formatted name and "=" as needed by this node.
void AppendNodeNameAndType(const FormatNode* node, const RecursiveState& state, OutputBuffer* out) {
  if (ShouldPrependTypeNameBeforeName(node, state)) {
    out->Append(Syntax::kOperatorDim, "(");
    out->Append(Syntax::kComment, node->type());
    out->Append(Syntax::kOperatorDim, ") ");
  }

  if (!state.inhibit_one_name && !node->name().empty()) {
    // Node name. Base class names are less important so get dimmed. Especially STL base classes
    // can get very long so we also elide them unless verbose mode is turned on.
    if (node->child_kind() == FormatNode::kBaseClass) {
      out->Append(Syntax::kComment, GetElidedTypeName(state, node->name()));
    } else {
      // Normal variable-style node name.
      out->Append(Syntax::kVariable, node->name());
    }

    // Rust uses colons for most things, while C uses equals. The exception for Rust is local
    // variables which "=" to better match how the variables are declared.
    if (IsRust(node) && node->child_kind() != FormatNode::kVariable)
      out->Append(Syntax::kOperatorNormal, ": ");
    else
      out->Append(Syntax::kOperatorNormal, " = ");
  }
}

// Writes the node's children. The caller sets up how it wants the children to be formatted by
// passing both the recursive state for the node, and the state it wants to use for the children.
//
// The opening and closing strings are used to wrap the contents, e.g. "{" and "}". If these are
// empty it's assumed that the first line doesn't need a newline after it in multiline mode
// (normally the opening character would be followed by a newline and an indent in multiline mode).
void AppendNodeChildren(const FormatNode* node, const RecursiveState& node_state,
                        const std::string& opening, const std::string& closing,
                        const RecursiveState& child_state, OutputBuffer* out) {
  out->Append(Syntax::kOperatorNormal, opening);

  if (child_state.DepthTooDeep()) {
    // Don't print the child names if those children will be elided. Otherwise this prints a
    // whole struct out with no data: "{foo=…, bar=…, baz=…}" which is wasteful of space and not
    // helpful.
    out->Append(Syntax::kComment, "…");
    out->Append(Syntax::kOperatorNormal, closing);
    return;
  }

  // Special-case empty ones because we never want wrapping.
  if (node->children().empty()) {
    out->Append(Syntax::kOperatorNormal, closing);
    return;
  }

  if (!opening.empty() && node_state.ShouldExpand())
    out->Append("\n");

  int child_indent = child_state.GetIndentAmount();
  for (size_t i = 0; i < node->children().size(); i++) {
    const FormatNode* child = node->children()[i].get();

    if (child->state() == FormatNode::kEmpty) {
      // Arrays can have "empty" nodes which use the name to indicate clipping ("...").
      if (!child->name().empty())
        out->Append(Syntax::kComment, child->name());
    } else {
      out->Append(DoFormatNode(child, child_state, child_indent));
    }

    // Separator (comma or newline).
    AppendItemSuffix(node_state, i + 1 == node->children().size(), out);
  }

  out->Append(Syntax::kOperatorNormal, node_state.GetIndentString() + closing);
}

OutputBuffer DoFormatArrayOrTupleNode(const FormatNode* node, const RecursiveState& state) {
  OutputBuffer out;

  RecursiveState child_state = state.Advance();
  // In multiline mode, show the names of the array indices ("[0] = ...").
  child_state.inhibit_one_name = !state.ShouldExpand();

  if (node->description_kind() == FormatNode::kArray) {
    // Arrays all have the same type so don't show the type for every child.
    child_state.inhibit_one_type = true;

    if (IsRust(node)) {
      // Rust sequences use "[...]".
      AppendRustCollectionName(node, state, &out);
      AppendNodeChildren(node, state, "[", "]", child_state, &out);
    } else {
      AppendNodeChildren(node, state, "{", "}", child_state, &out);
    }
  } else {
    // Rust tuple or tuple struct. These should not be empty.
    if (node->description_kind() == FormatNode::kRustTupleStruct) {
      // Tuple structs get the name, regular tuples have none.
      AppendRustCollectionName(node, state, &out);
    }

    // Tuples of length 1 never show the child names, even in expanded mode because it's not
    // helpful and looks weird.o
    //
    // We show the indices of tuple members in expanded mode for larger tuples even though
    // Rust wouldn't do that because highly nested data structures can get very difficult to
    // follow. Something like a closure or a generator can go on for >100 lines and have many
    // tuple members (>10). Without these indices the structure is not interpretable. This doesn't
    // come up when using println in the language very often because normally these types of
    // tuples are not printed.
    if (node->children().size() == 1)
      child_state.inhibit_one_name = true;

    AppendNodeChildren(node, state, "(", ")", child_state, &out);
  }

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
      out.Append(Syntax::kOperatorNormal, "{}");
    return out;
  }

  AppendNodeChildren(node, state, "{", "}", state.Advance(), &out);
  return out;
}

OutputBuffer DoFormatPointerNode(const FormatNode* node, const RecursiveState& state) {
  OutputBuffer out;

  // When type information is forced on, the type will have already been printed. Otherwise we print
  // a "(*)" to indicate the value is a pointer.
  if (!state.TypeForcedOn())
    out.Append(Syntax::kOperatorDim, "(*)");

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

    out.Append(Syntax::kComment, node->description() + " " + GetRightArrow() + " ");
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

// A Rust enum is currently like a reference in that is has a child that's the resolved type.
// A resolved enum type will be a TupleStruct (unnamed members) or a Struct (named members). Enums
// with no members are encoded as empty structs which the struct printing code will handle.
OutputBuffer DoFormatRustEnum(const FormatNode* node, const RecursiveState& state) {
  if (node->children().empty()) {
    // The caller should have expanded the enum if it wants it to be shown. If not, display the
    // description which will generally be the short name of the enum.
    return OutputBuffer(Syntax::kComment, node->description());
  }

  // Have the pointed-to data, just show the value. We do not want to indent another level. The
  // type is also hidden because it will be a generated type. If requested, the type of the overall
  // enum will have already been printed.
  RecursiveState child_state = state.AdvanceNoIndent();
  child_state.inhibit_one_name = true;
  child_state.inhibit_one_type = true;

  // Use the "one way" version to propagate our formatting mode. This node can't get different
  // formatting than its child.
  return DoFormatNodeOneWay(node->children()[0].get(), child_state, 0);
}

// A generic node with one child for things like std::optional or std::atomic.
OutputBuffer DoFormatWrapper(const FormatNode* node, const RecursiveState& state) {
  if (node->children().empty())
    return OutputBuffer(node->description() + node->wrapper_prefix() + node->wrapper_suffix());

  RecursiveState child_state = state.AdvanceNoIndent();

  OutputBuffer out;
  out.Append(node->description());
  out.Append(Syntax::kOperatorNormal, node->wrapper_prefix());
  out.Append(DoFormatNodeOneWay(node->children()[0].get(), child_state, 0));
  out.Append(Syntax::kOperatorNormal, node->wrapper_suffix());
  return out;
}

// Appends the description for a normal node (number or whatever).
OutputBuffer DoFormatStandardNode(const FormatNode* node, const RecursiveState& state,
                                  Syntax syntax = Syntax::kNormal) {
  return OutputBuffer(syntax, node->description());
}

// Groups have no heading, they're always just a list of children.
OutputBuffer DoFormatGroupNode(const FormatNode* node, const RecursiveState& state) {
  OutputBuffer out;
  AppendNodeChildren(node, state, std::string(), std::string(), state, &out);
  return out;
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
  if (state.DepthTooDeep() || node->state() != FormatNode::kDescribed) {
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
        // All these things just print the description only.
        out.Append(DoFormatStandardNode(node, state));
        break;
      case FormatNode::kGroup:
        out.Append(DoFormatGroupNode(node, state));
        break;
      case FormatNode::kArray:
      case FormatNode::kRustTuple:
      case FormatNode::kRustTupleStruct:
        out.Append(DoFormatArrayOrTupleNode(node, state));
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
        out.Append(DoFormatRustEnum(node, state));
        break;
      case FormatNode::kWrapper:
        out.Append(DoFormatWrapper(node, state));
        break;
      case FormatNode::kString:
        // For strings, use the number format to determine if the output should be displayed
        // as the string data or a numeric array.
        if (state.options.num_format == FormatOptions::NumFormat::kDefault) {
          out.Append(DoFormatStandardNode(node, state, Syntax::kStringNormal));
        } else {
          out.Append(DoFormatArrayOrTupleNode(node, state));
        }
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

  // Nodes with few children we try to fit on one line if possible in smart mode. Generally
  // structures with more than 4 children will always spill over to multiline mode, so this check is
  // more of an optimization to avoid trying and failing to format the whole thing as a single line
  // first. Even if it fits, many members on a line are not readable.
  if (state.smart_indent_is_expanded && node->children().size() <= 4) {
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
  node->set_child_kind(FormatNode::kVariable);

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
      RefPtrTo(var), [name = var->GetAssignedName(), context, options, out](ErrOrValue value) {
        if (value.has_error()) {
          // In the error case, construct a node with the error set so the formatting with other
          // types of errors is consistent.
          FormatNode err_node(name);
          err_node.SetDescribedError(value.err());
          out->Complete(FormatNodeForConsole(err_node, options));
        } else {
          out->Complete(FormatValueForConsole(value.take_value(), options, context, name));
        }
      });
  return out;
}

fxl::RefPtr<AsyncOutputBuffer> FormatExpressionsForConsole(
    const std::vector<std::string>& expressions, const ConsoleFormatOptions& options,
    fxl::RefPtr<EvalContext> context) {
  // Put all of the expressions in a "group" node and then evaluate and format this node. This will
  // allow single- and multi-line formatting for the values and also handles all of the asynchronous
  // state across all of the expressions automatically.
  auto out = fxl::MakeRefCounted<AsyncOutputBuffer>();
  auto list_node = std::make_unique<FormatNode>(FormatNode::GroupTag());
  FormatNode* list_node_ptr = list_node.get();

  for (const std::string& expr : expressions) {
    // Use the expression as the description.
    auto child_node = std::make_unique<FormatNode>(expr, expr);
    list_node->children().push_back(std::move(child_node));
  }

  DescribeFormatNodeForConsole(
      list_node_ptr, options, context,
      fit::defer_callback([list_node = std::move(list_node), options, out]() {
        RecursiveState state(options);
        out->Complete(FormatNodeForConsole(*list_node, options));
      }));
  return out;
}

}  // namespace zxdb
