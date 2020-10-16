// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_FORMATTER_H_
#define ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_FORMATTER_H_

#include <memory>
#include <optional>
#include <regex>
#include <sstream>

#include "tree_visitor.h"
#include "utils.h"

// A TreeVisitor (see tree_visitor.h) that pretty-prints FIDL code.

namespace fidl {
namespace raw {

class ScopedBool {
 public:
  ScopedBool(bool& source, bool value = true) : prev_value_(source), source_(source) {
    source_ = value;
  }
  ~ScopedBool() { source_ = prev_value_; }

 private:
  bool prev_value_;
  bool& source_;
};

class ScopedIncrement {
 public:
  ScopedIncrement(int& source) : source_(source) { source_++; }
  ~ScopedIncrement() { source_--; }

 private:
  int& source_;
};

// A Visitor that pretty prints its AST and makes its result available via
// formatted_output().
//
// Implementation note: The visitor mostly does the same thing on every node,
// which is encapsulated in OnSourceElementShared.  Where we have overridden a
// particular node's visitor, it usually means that slightly different behavior
// is needed for that language construct.  For example, using and const
// declarations respect leading blank lines if they are already there, and
// struct, enum, and protocol declarations require leading blank lines.
class FormattingTreeVisitor : public DeclarationOrderTreeVisitor {
 public:
  FormattingTreeVisitor() : last_location_(nullptr) {}

  virtual void OnProtocolDeclaration(std::unique_ptr<ProtocolDeclaration> const& element) override {
    OnBlankLineRequiringNode();
    DeclarationOrderTreeVisitor::OnProtocolDeclaration(element);
  }

  virtual void OnSourceElementStart(const SourceElement& element) override {
    OnSourceElementShared(element.start_);
  }

  virtual void OnSourceElementEnd(const SourceElement& element) override {
    OnSourceElementShared(element.end_);
  }

  virtual void OnAttribute(const Attribute& element) override {
    // Remove leading whitespace from the element.
    remove_leading_ws_ = true;
    TreeVisitor::OnAttribute(element);
    // Remove leading whitespace from the closing square bracket after the element.
    remove_leading_ws_ = true;
  }
  virtual void OnAttributeList(std::unique_ptr<AttributeList> const& element) override {
    // Disabling these in case we're in a protocol method and it thinks
    // the next line needs to be indented more.  We don't want an indent
    // after a newline following an attribute list.  It will be reenabled by
    // the next visited AST node in the method.
    newline_means_indent_more_ = is_param_decl_;
    protocol_method_alignment_ = is_param_decl_;
    // This prevents the above from being reenabled during our walk of the
    // AttributeList
    {
      ScopedBool suppress(is_member_decl_, false);
      TreeVisitor::OnAttributeList(element);
    }

    // After parameter attributes we need a whitespace.
    if (is_param_decl_) {
      ws_required_next_ = true;
    }
  }

  virtual void OnUsing(std::unique_ptr<Using> const& element) override {
    OnBlankLineRespectingNode();
    ScopedBool mem(is_member_decl_);
    TreeVisitor::OnUsing(element);
  }

  virtual void OnConstDeclaration(std::unique_ptr<ConstDeclaration> const& element) override {
    OnBlankLineRespectingNode();
    ScopedBool mem(is_member_decl_);
    TreeVisitor::OnConstDeclaration(element);
  }

  virtual void OnEnumMember(std::unique_ptr<EnumMember> const& element) override {
    OnBlankLineRespectingNode();
    ScopedBool mem(is_member_decl_);
    TreeVisitor::OnEnumMember(element);
  }

  virtual void OnEnumDeclaration(std::unique_ptr<EnumDeclaration> const& element) override {
    OnBlankLineRequiringNode();
    ScopedBool mem(is_enum_or_bits_or_resource_decl_, true);
    TreeVisitor::OnEnumDeclaration(element);
  }

  virtual void OnBitsMember(std::unique_ptr<BitsMember> const& element) override {
    OnBlankLineRespectingNode();
    ScopedBool mem(is_member_decl_);
    TreeVisitor::OnBitsMember(element);
  }

  virtual void OnBitsDeclaration(std::unique_ptr<BitsDeclaration> const& element) override {
    OnBlankLineRequiringNode();
    ScopedBool mem(is_enum_or_bits_or_resource_decl_, true);
    TreeVisitor::OnBitsDeclaration(element);
  }

  virtual void OnParameterList(std::unique_ptr<ParameterList> const& element) override {
    has_encountered_param_list_start_ = false;
    is_param_list_first_param_on_same_line_ = std::nullopt;
    ScopedBool method(is_param_decl_);
    TreeVisitor::OnParameterList(element);
  }

  virtual void OnProtocolMethod(std::unique_ptr<ProtocolMethod> const& element) override {
    protocol_method_alignment_ = true;
    protocol_method_alignment_size_ = -1;
    next_nonws_char_is_checkpoint_ = false;
    OnBlankLineRespectingNode();
    ScopedBool before(blank_space_before_colon_, false);
    ScopedBool mem(is_member_decl_);
    TreeVisitor::OnProtocolMethod(element);
  }

  virtual void OnResourceProperty(std::unique_ptr<ResourceProperty> const& element) override {
    TreeVisitor::OnResourceProperty(element);
  }

  virtual void OnResourceDeclaration(std::unique_ptr<ResourceDeclaration> const& element) override {
    OnBlankLineRequiringNode();
    ScopedBool mem(is_enum_or_bits_or_resource_decl_, true);
    TreeVisitor::OnResourceDeclaration(element);
  }

  virtual void OnComposeProtocol(std::unique_ptr<ComposeProtocol> const& element) override {
    OnBlankLineRespectingNode();
    TreeVisitor::OnComposeProtocol(element);
  }

  virtual void OnServiceDeclaration(std::unique_ptr<ServiceDeclaration> const& element) override {
    OnBlankLineRequiringNode();
    TreeVisitor::OnServiceDeclaration(element);
  }

  virtual void OnServiceMember(std::unique_ptr<ServiceMember> const& element) override {
    OnBlankLineRespectingNode();
    ScopedBool mem(is_member_decl_);
    TreeVisitor::OnServiceMember(element);
  }

  virtual void OnStructDeclaration(std::unique_ptr<StructDeclaration> const& element) override {
    OnBlankLineRequiringNode();
    TreeVisitor::OnStructDeclaration(element);
  }

  virtual void OnStructMember(std::unique_ptr<StructMember> const& element) override {
    OnBlankLineRespectingNode();
    ScopedBool mem(is_member_decl_);
    // If we are inside a method declaration this is actually a parameter declaration.
    TreeVisitor::OnStructMember(element);
  }

  virtual void OnTableDeclaration(std::unique_ptr<TableDeclaration> const& element) override {
    OnBlankLineRequiringNode();
    TreeVisitor::OnTableDeclaration(element);
  }

  virtual void OnTableMember(std::unique_ptr<TableMember> const& element) override {
    OnBlankLineRespectingNode();
    ScopedBool mem(is_member_decl_);
    ScopedBool before_colon(blank_space_before_colon_, false);
    ScopedBool after_colon(blank_space_after_colon_, true);
    TreeVisitor::OnTableMember(element);
  }

  virtual void OnUnionDeclaration(std::unique_ptr<UnionDeclaration> const& element) override {
    OnBlankLineRequiringNode();
    TreeVisitor::OnUnionDeclaration(element);
  }

  virtual void OnUnionMember(std::unique_ptr<UnionMember> const& element) override {
    OnBlankLineRespectingNode();
    ScopedBool mem(is_member_decl_);
    ScopedBool before_colon(blank_space_before_colon_, false);
    ScopedBool after_colon(blank_space_after_colon_, true);
    TreeVisitor::OnUnionMember(element);
  }

  virtual void OnTypeConstructor(std::unique_ptr<TypeConstructor> const& element) override {
    ScopedIncrement si(nested_type_depth_);
    ScopedBool before_colon(blank_space_before_colon_, is_enum_or_bits_or_resource_decl_);
    ScopedBool after_colon(blank_space_after_colon_, is_enum_or_bits_or_resource_decl_);
    TreeVisitor::OnTypeConstructor(element);
  }

  virtual void OnFile(std::unique_ptr<File> const& element) override;

  std::string* formatted_output() { return &formatted_output_; }

 private:
  const char* last_location_;

  static constexpr int kIndentSpaces = 4;

  std::string formatted_output_;

  // Indentations can be caused by nesting in the AST (e.g., if you've started
  // a compound statement) or, in some situations, if there is a newline in
  // the code (e.g., if you've decided to line break before the "->" operator
  // in the middle of a protocol definition).

  // What follows are adjustable properties.  We flip them from true to false
  // at various points in the tree walk depending on what behavior we want.

  // When this is true, you get a blank line and indentation at the end of the
  // segment.  This is true for top level decls that *require* blank lines:
  // e.g., structs, unions, and protocols.
  bool blank_line_requiring_node_ = false;

  // When this is true, you get a blank line and indentation at the end of the
  // segment if there is already a blank line there.  This is true for consts
  // and using declarations.
  bool blank_line_respecting_node_ = false;

  // When this is true, and a newline happens, you get an additional
  // indentation.
  bool newline_means_indent_more_ = false;

  // This is true in decl headers, but not after the ordinal in a protocol
  // method or in the element count for relevant types.
  bool blank_space_before_colon_ = true;

  // This is true in decl headers and after the ordinal in a protocol
  // method, but not in the element count for relevant types.
  bool blank_space_after_colon_ = true;

  // > characters require spaces after them unless you are in the middle of a
  // parameterized type: arrays, vectors, handles, and requests.
  int nested_type_depth_ = 0;

  // Protocol methods have fancy alignment: if the last open parenthesis was
  // at EOL, indentation is to the column that had beginning of method name +
  // kIndentSpaces.  Otherwise, it is to the column with the last open
  // parenthesis + 1.
  bool protocol_method_alignment_ = false;
  int protocol_method_alignment_size_ = -1;
  int distance_from_last_newline_ = 0;
  int offset_of_first_id_ = 0;
  bool next_nonws_char_is_checkpoint_ = false;
  // After closing a parameter attribute, we restore the protocol alignment.
  int protocol_method_alignment_size_backup = -1;

  bool has_encountered_param_list_start_ = false;
  // After encountering the opening parenthesis of a `ParameterList`, this denotes whether the first
  // param is on the same line.
  std::optional<bool> is_param_list_first_param_on_same_line_;

  // When we complete a node and know the next thing needs to be whitespace
  bool ws_required_next_ = false;

  // When we complete a node and the next thing cannot have whitespace
  bool no_ws_next_ = false;

  // Remove all leading whitespace (regardless of whether it is part of the
  // beginning / end of a line).
  bool remove_leading_ws_ = false;

  // If the string str at offset pos is the beginning of a comment, pos is
  // modified to be the newline character at EOL.
  static void MaybeWindPastComment(std::string str, int& pos) {
    if (utils::LineFromOffsetIsRegularComment(str, pos)) {
      while (pos < static_cast<int>(str.size()) && str[pos] != '\n') {
        pos++;
      }
    }
  }

  // A "Segment" is a part of the source that we format - it extends from the
  // end of the previously formatted AST node to the end of the first token
  // represented in this AST node.
  class Segment {
   public:
    Segment(std::string input, FormattingTreeVisitor* outer) : output_(input), visitor_(outer) {}

    void RemoveTrailingWSOnLine() {
      std::string re("[");
      re += utils::kWhitespaceNoNewlineChars;
      re += "]+\n";
      output_ = std::regex_replace(output_, std::regex(re), "\n");
    }

    // Removes all of the whitespace at the beginning of every line.  Will
    // add back indentation later.
    void RemoveLeadingWSOnLine() {
      std::string re("\n[");
      re += utils::kWhitespaceNoNewlineChars;
      re += "]+";
      output_ = std::regex_replace(output_, std::regex(re), "\n");
    }

    void RemoveLeadingWS() {
      std::string re("^[");
      re += utils::kWhitespaceNoNewlineChars;
      re += "]+";
      output_ = std::regex_replace(output_, std::regex(re), "");
    }

    void RemoveExtraBlankLines(bool respects_trailing_blankline);

    void InsertRequiredNewlines(bool is_top_level);

    // No non-' ' or '\n' whitespace
    // One ws token before / after every ws-requiring character
    // No non-newline ws before / after characters that don't want it.
    void RegularizeSpaces(bool& ws_required_next, bool& no_ws_next);

    // Precondition: By now, everything should have had its leading ws
    // stripped, and } characters are the first things on their own lines.
    void Indent(int& current_nesting);

    std::string GetOutput() { return output_; }

   private:
    // RequiresWSBeforeChar is called 1st.
    bool RequiresWSBeforeChar(char ch) {
      return (ch == '{') || (ch == '=') || (visitor_->blank_space_before_colon_ && ch == ':');
    }

    // NoSpacesBeforeChar is called 2nd (after RequiresWSBeforeChar).
    bool NoSpacesBeforeChar(char ch) {
      return NoWSBeforeChar(ch) || (ch == ')') || (ch == '?') || (ch == '<') ||
             (!visitor_->blank_space_before_colon_ && ch == ':') ||
             (visitor_->nested_type_depth_ > 0 && ch == '>');
    }

    // RequiresWSAfterChar is called 3rd (after NoSpacesBeforeChar).
    bool RequiresWSAfterChar(char ch) {
      return (ch == '=') || (ch == ',') || (ch == ')') ||
             (ch == '>' && (visitor_->nested_type_depth_ <= 1)) ||
             (ch == ':' && visitor_->blank_space_after_colon_);
    }

    bool NoWSBeforeChar(char ch) { return (ch == ';' || ch == ','); }

    bool NoWSAfterChar(char ch) {
      return (ch == ':' && !visitor_->blank_space_after_colon_) || (ch == '(') || (ch == '<');
    }

    // As specified on the tin: erases multiple spaces from output_ at
    // offset pos.  Optional |leave_this_many| parameter indicates number of
    // spaces to leave; default is 1.  Optional |incl_newline| param
    // indicates to erase newline characters as well as other WS.
    // It returns the number of spaces that were deleted.
    int EraseMultipleSpacesAt(int pos, int leave_this_many = 1, bool incl_newline = false);

    std::string output_;
    FormattingTreeVisitor* visitor_;
  };

  int current_nesting_ = 0;

  std::string FormatAndPrintSegment(const std::string& segment) {
    Segment seg(segment, this);
    seg.RemoveTrailingWSOnLine();
    seg.RemoveLeadingWSOnLine();
    seg.RemoveExtraBlankLines(blank_line_respecting_node_);
    seg.RegularizeSpaces(ws_required_next_, no_ws_next_);
    seg.InsertRequiredNewlines(blank_line_requiring_node_);
    seg.Indent(current_nesting_);
    if (remove_leading_ws_) {
      seg.RemoveLeadingWS();

      // We've respected the removal of leading white space for this segment, and we should stop now
      remove_leading_ws_ = false;
    }

    std::string output = seg.GetOutput();

    // We've respected prior blank lines for this decl (if it is a decl),
    // and we should stop now.
    blank_line_requiring_node_ = false;
    blank_line_respecting_node_ = false;

    // If this was the start of a member decl, it was indented by
    // kIndentSpaces.  Any other newlines inside the member decl should be
    // indented additionally.
    if (is_member_decl_) {
      protocol_method_alignment_ = true;
      newline_means_indent_more_ = true;
    }

    return output;
  }

  void OnBlankLineRequiringNode() {
    blank_line_requiring_node_ = true;
    blank_line_respecting_node_ = true;
  }

  void OnBlankLineRespectingNode() { blank_line_respecting_node_ = true; }

  bool is_enum_or_bits_or_resource_decl_ = false;
  bool is_member_decl_ = false;
  bool is_param_decl_ = false;

  // str is a gap plus the next meaningful token.
  void TrackProtocolMethodAlignment(const std::string& str);

  void OnSourceElementShared(const fidl::Token& current_token) {
    const char* ws_location = current_token.previous_end().data().data();
    // Printed code must increase in monotonic order, for two reasons.
    // First of all, we don't reorder anything.  Second of all, the start
    // token for an identifier list (for example) is the same as the start
    // token for the first identifier, so we need to make sure we don't
    // print that token twice.
    if (ws_location > last_location_) {
      auto gap_size = static_cast<size_t>(current_token.data().data() -
                                          current_token.previous_end().data().data());
      std::string gap(ws_location, gap_size);
      std::string content(current_token.data().data(), current_token.data().size());
      std::string total_string = FormatAndPrintSegment(gap + content);
      TrackProtocolMethodAlignment(total_string);
      formatted_output_ += total_string;
      last_location_ = ws_location;
    }
  }
};

}  // namespace raw
}  // namespace fidl

#endif  // ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_FORMATTER_H_
