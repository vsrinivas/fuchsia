// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_LINTING_TREE_CALLBACKS_H_
#define ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_LINTING_TREE_CALLBACKS_H_

#include <lib/fit/function.h>

#include <vector>

#include <fidl/source_span.h>
#include <fidl/tree_visitor.h>

namespace fidl {
namespace linter {

// Supports TreeVisitor actions via delegation instead of inheritance, by
// wrapping a TreeVisitor subclass that calls a list of callbacks for
// each visitor method. In otherwords, this class implements a hardcoded "map"
// from each source node type (represented by its TreeVisitor method) to a set
// of callbacks, rather than implementing the callback logic directly inside
// the overridden method.
class LintingTreeCallbacks {
 public:
  // Construct a new callbacks container. Call "On" methods, for each event
  // type (such as "OnAttribute"), to register a callback for that event.
  LintingTreeCallbacks();

  // Process a file (initiates the callbacks as each element is visited for
  // the given parsed source file).
  void Visit(std::unique_ptr<raw::File> const& element);

  // Register a callback for a "File" event. All of the remaining "On"
  // functions similarly match their cooresponding TreeVisitor methods.
  void OnFile(fit::function<void(const raw::File&)> callback) {
    file_callbacks_.push_back(std::move(callback));
  }
  void OnExitFile(fit::function<void(const raw::File&)> callback) {
    exit_file_callbacks_.push_back(std::move(callback));
  }

  // Note that an Attribute with name "Doc" has content from comments that
  // start with exactly three forward slashes (///), with those slashes
  // removed. The first line of the Doc Comment is consumed without triggering
  // OnLineComment(), but follow-on consecutive Doc Comment lines do trigger
  // OnLineComment().
  void OnAttribute(fit::function<void(const raw::Attribute&)> callback) {
    attribute_callbacks_.push_back(std::move(callback));
  }

  void OnSourceElement(fit::function<void(const raw::SourceElement&)> callback) {
    source_element_callbacks_.push_back(std::move(callback));
  }

  // The OnLineComment callback takes two parameters:
  // * |SourceSpan| containing the comment
  // * |line_prefix_view| a std::string_view of all characters on the same line,
  //   preceeding the comment
  void OnLineComment(fit::function<void(const SourceSpan&, std::string_view)> callback) {
    line_comment_callbacks_.push_back(std::move(callback));
  }
  // The OnLineComment callback takes two parameters:
  // * |SourceSpan| containing the whitespace characters, and if the whitespace characters
  //   end the line, it includes the newline character
  // * |line_prefix_view| a std::string_view of all characters on the same line,
  //   preceeding the whitespace
  void OnWhiteSpaceUpToNewline(fit::function<void(const SourceSpan&, std::string_view)> callback) {
    white_space_up_to_newline_callbacks_.push_back(std::move(callback));
  }
  void OnIgnoredToken(fit::function<void(const SourceSpan&)> callback) {
    ignored_token_callbacks_.push_back(std::move(callback));
  }

  void OnUsing(fit::function<void(const raw::Using&)> callback) {
    using_callbacks_.push_back(std::move(callback));
  }
  void OnBitsDeclaration(fit::function<void(const raw::BitsDeclaration&)> callback) {
    bits_declaration_callbacks_.push_back(std::move(callback));
  }
  void OnBitsMember(fit::function<void(const raw::BitsMember&)> callback) {
    bits_member_callbacks_.push_back(std::move(callback));
  }
  void OnExitBitsDeclaration(fit::function<void(const raw::BitsDeclaration&)> callback) {
    exit_bits_declaration_callbacks_.push_back(std::move(callback));
  }
  void OnConstDeclaration(fit::function<void(const raw::ConstDeclaration&)> callback) {
    const_declaration_callbacks_.push_back(std::move(callback));
  }
  void OnExitConstDeclaration(fit::function<void(const raw::ConstDeclaration&)> callback) {
    exit_const_declaration_callbacks_.push_back(std::move(callback));
  }
  void OnEnumDeclaration(fit::function<void(const raw::EnumDeclaration&)> callback) {
    enum_declaration_callbacks_.push_back(std::move(callback));
  }
  void OnEnumMember(fit::function<void(const raw::EnumMember&)> callback) {
    enum_member_callbacks_.push_back(std::move(callback));
  }
  void OnExitEnumDeclaration(fit::function<void(const raw::EnumDeclaration&)> callback) {
    exit_enum_declaration_callbacks_.push_back(std::move(callback));
  }
  void OnProtocolDeclaration(fit::function<void(const raw::ProtocolDeclaration&)> callback) {
    protocol_declaration_callbacks_.push_back(std::move(callback));
  }
  void OnExitProtocolDeclaration(fit::function<void(const raw::ProtocolDeclaration&)> callback) {
    exit_protocol_declaration_callbacks_.push_back(std::move(callback));
  }
  void OnMethod(fit::function<void(const raw::ProtocolMethod&)> callback) {
    method_callbacks_.push_back(std::move(callback));
  }
  void OnEvent(fit::function<void(const raw::ProtocolMethod&)> callback) {
    event_callbacks_.push_back(std::move(callback));
  }
  void OnParameter(fit::function<void(const raw::Parameter&)> callback) {
    parameter_callbacks_.push_back(std::move(callback));
  }
  void OnStructDeclaration(fit::function<void(const raw::StructDeclaration&)> callback) {
    struct_declaration_callbacks_.push_back(std::move(callback));
  }
  void OnStructMember(fit::function<void(const raw::StructMember&)> callback) {
    struct_member_callbacks_.push_back(std::move(callback));
  }
  void OnExitStructDeclaration(fit::function<void(const raw::StructDeclaration&)> callback) {
    exit_struct_declaration_callbacks_.push_back(std::move(callback));
  }
  void OnTableDeclaration(fit::function<void(const raw::TableDeclaration&)> callback) {
    table_declaration_callbacks_.push_back(std::move(callback));
  }
  void OnTypeConstructor(fit::function<void(const raw::TypeConstructor&)> callback) {
    type_constructor_callbacks_.push_back(std::move(callback));
  }
  void OnTableMember(fit::function<void(const raw::TableMember&)> callback) {
    table_member_callbacks_.push_back(std::move(callback));
  }
  void OnExitTableDeclaration(fit::function<void(const raw::TableDeclaration&)> callback) {
    exit_table_declaration_callbacks_.push_back(std::move(callback));
  }
  void OnUnionDeclaration(fit::function<void(const raw::UnionDeclaration&)> callback) {
    union_declaration_callbacks_.push_back(std::move(callback));
  }
  void OnUnionMember(fit::function<void(const raw::UnionMember&)> callback) {
    union_member_callbacks_.push_back(std::move(callback));
  }
  void OnExitUnionDeclaration(fit::function<void(const raw::UnionDeclaration&)> callback) {
    exit_union_declaration_callbacks_.push_back(std::move(callback));
  }
  void OnXUnionDeclaration(fit::function<void(const raw::XUnionDeclaration&)> callback) {
    xunion_declaration_callbacks_.push_back(std::move(callback));
  }
  void OnXUnionMember(fit::function<void(const raw::XUnionMember&)> callback) {
    xunion_member_callbacks_.push_back(std::move(callback));
  }
  void OnExitXUnionDeclaration(fit::function<void(const raw::XUnionDeclaration&)> callback) {
    exit_xunion_declaration_callbacks_.push_back(std::move(callback));
  }

 private:
  // tree_visitor_ is initialized to a locally-defined class
  // |CallbackTreeVisitor| defined in the out-of-line implementation of the
  // LintingTreeCallbacks constructor.
  //
  // The CallbackTreeVisitor overrides TreeVisitor to call the registered
  // callback methods. It is not necessary to define the class inline here.
  // We avoid having to declare all of the overridden methods unnecessarily
  // in this header; and avoid the alternative--defining the methods inline,
  // in this header--which would make inclusing the header a costly thing to
  // do.
  std::unique_ptr<fidl::raw::TreeVisitor> tree_visitor_;

  std::vector<fit::function<void(const raw::File&)>> file_callbacks_;
  std::vector<fit::function<void(const raw::File&)>> exit_file_callbacks_;
  std::vector<fit::function<void(const raw::Attribute&)>> attribute_callbacks_;
  std::vector<fit::function<void(const raw::SourceElement&)>> source_element_callbacks_;
  std::vector<fit::function<void(const SourceSpan&, std::string_view)>> line_comment_callbacks_;
  std::vector<fit::function<void(const SourceSpan&, std::string_view)>>
      white_space_up_to_newline_callbacks_;
  std::vector<fit::function<void(const SourceSpan&)>> ignored_token_callbacks_;
  std::vector<fit::function<void(const raw::Using&)>> using_callbacks_;
  std::vector<fit::function<void(const raw::BitsDeclaration&)>> bits_declaration_callbacks_;
  std::vector<fit::function<void(const raw::BitsDeclaration&)>> exit_bits_declaration_callbacks_;
  std::vector<fit::function<void(const raw::BitsMember&)>> bits_member_callbacks_;
  std::vector<fit::function<void(const raw::ConstDeclaration&)>> const_declaration_callbacks_;
  std::vector<fit::function<void(const raw::ConstDeclaration&)>> exit_const_declaration_callbacks_;
  std::vector<fit::function<void(const raw::EnumDeclaration&)>> enum_declaration_callbacks_;
  std::vector<fit::function<void(const raw::EnumDeclaration&)>> exit_enum_declaration_callbacks_;
  std::vector<fit::function<void(const raw::EnumMember&)>> enum_member_callbacks_;
  std::vector<fit::function<void(const raw::ProtocolDeclaration&)>> protocol_declaration_callbacks_;
  std::vector<fit::function<void(const raw::ProtocolDeclaration&)>>
      exit_protocol_declaration_callbacks_;
  std::vector<fit::function<void(const raw::ProtocolMethod&)>> method_callbacks_;
  std::vector<fit::function<void(const raw::ProtocolMethod&)>> event_callbacks_;
  std::vector<fit::function<void(const raw::Parameter&)>> parameter_callbacks_;
  std::vector<fit::function<void(const raw::StructMember&)>> struct_member_callbacks_;
  std::vector<fit::function<void(const raw::StructDeclaration&)>> struct_declaration_callbacks_;
  std::vector<fit::function<void(const raw::StructDeclaration&)>>
      exit_struct_declaration_callbacks_;
  std::vector<fit::function<void(const raw::TableMember&)>> table_member_callbacks_;
  std::vector<fit::function<void(const raw::TableDeclaration&)>> table_declaration_callbacks_;
  std::vector<fit::function<void(const raw::TableDeclaration&)>> exit_table_declaration_callbacks_;
  std::vector<fit::function<void(const raw::TypeConstructor&)>> type_constructor_callbacks_;
  std::vector<fit::function<void(const raw::UnionMember&)>> union_member_callbacks_;
  std::vector<fit::function<void(const raw::UnionDeclaration&)>> union_declaration_callbacks_;
  std::vector<fit::function<void(const raw::UnionDeclaration&)>> exit_union_declaration_callbacks_;
  std::vector<fit::function<void(const raw::XUnionMember&)>> xunion_member_callbacks_;
  std::vector<fit::function<void(const raw::XUnionDeclaration&)>> xunion_declaration_callbacks_;
  std::vector<fit::function<void(const raw::XUnionDeclaration&)>>
      exit_xunion_declaration_callbacks_;
};

}  // namespace linter
}  // namespace fidl

#endif  // ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_LINTING_TREE_CALLBACKS_H_
