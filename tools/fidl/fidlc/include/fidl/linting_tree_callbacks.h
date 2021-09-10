// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDL_FIDLC_INCLUDE_FIDL_LINTING_TREE_CALLBACKS_H_
#define TOOLS_FIDL_FIDLC_INCLUDE_FIDL_LINTING_TREE_CALLBACKS_H_

#include <lib/fit/function.h>

#include <vector>

#include <fidl/source_span.h>
#include <fidl/tree_visitor.h>

namespace fidl::linter {

// Supports TreeVisitor actions via delegation instead of inheritance, by
// wrapping a TreeVisitor subclass that calls a list of callbacks for
// each visitor method. In other words, this class implements a hardcoded "map"
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
  // functions similarly match their corresponding TreeVisitor methods.
  void OnFile(fit::function<void(const raw::File&)> callback) {
    file_callbacks_.push_back(std::move(callback));
  }
  void OnExitFile(fit::function<void(const raw::File&)> callback) {
    exit_file_callbacks_.push_back(std::move(callback));
  }

  void OnSourceElement(fit::function<void(const raw::SourceElement&)> callback) {
    source_element_callbacks_.push_back(std::move(callback));
  }

  // The OnLineComment callback takes two parameters:
  // * |SourceSpan| containing the comment
  // * |line_prefix_view| a std::string_view of all characters on the same line,
  //   preceding the comment
  void OnLineComment(fit::function<void(const SourceSpan&, std::string_view)> callback) {
    line_comment_callbacks_.push_back(std::move(callback));
  }
  // The OnLineComment callback takes two parameters:
  // * |SourceSpan| containing the whitespace characters, and if the whitespace characters
  //   end the line, it includes the newline character
  // * |line_prefix_view| a std::string_view of all characters on the same line,
  //   preceding the whitespace
  void OnWhiteSpaceUpToNewline(fit::function<void(const SourceSpan&, std::string_view)> callback) {
    white_space_up_to_newline_callbacks_.push_back(std::move(callback));
  }
  void OnIgnoredToken(fit::function<void(const SourceSpan&)> callback) {
    ignored_token_callbacks_.push_back(std::move(callback));
  }

  void OnAliasDeclaration(fit::function<void(const raw::AliasDeclaration&)> callback) {
    alias_callbacks_.push_back(std::move(callback));
  }
  void OnUsing(fit::function<void(const raw::Using&)> callback) {
    using_callbacks_.push_back(std::move(callback));
  }
  void OnConstDeclaration(fit::function<void(const raw::ConstDeclaration&)> callback) {
    const_declaration_callbacks_.push_back(std::move(callback));
  }
  void OnExitConstDeclaration(fit::function<void(const raw::ConstDeclaration&)> callback) {
    exit_const_declaration_callbacks_.push_back(std::move(callback));
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
  void OnAttribute(fit::function<void(const raw::Attribute&)> callback) {
    attribute_callbacks_.push_back(std::move(callback));
  }
  void OnOrdinaledLayoutMember(fit::function<void(const raw::OrdinaledLayoutMember&)> callback) {
    ordinaled_layout_member_callbacks_.push_back(std::move(callback));
  }
  void OnStructLayoutMember(fit::function<void(const raw::StructLayoutMember&)> callback) {
    struct_layout_member_callbacks_.push_back(std::move(callback));
  }
  void OnValueLayoutMember(fit::function<void(const raw::ValueLayoutMember&)> callback) {
    value_layout_member_callbacks_.push_back(std::move(callback));
  }
  void OnLayout(fit::function<void(const raw::Layout&)> callback) {
    layout_callbacks_.push_back(std::move(callback));
  }
  void OnExitLayout(fit::function<void(const raw::Layout&)> callback) {
    exit_layout_callbacks_.push_back(std::move(callback));
  }
  void OnTypeDecl(fit::function<void(const raw::TypeDecl&)> callback) {
    type_decl_callbacks_.push_back(std::move(callback));
  }
  void OnExitTypeDecl(fit::function<void(const raw::TypeDecl&)> callback) {
    exit_type_decl_callbacks_.push_back(std::move(callback));
  }
  void OnIdentifierLayoutParameter(
      fit::function<void(const raw::IdentifierLayoutParameter&)> callback) {
    identifier_layout_parameter_callbacks_.push_back(std::move(callback));
  }
  void OnTypeConstructor(fit::function<void(const raw::TypeConstructor&)> callback) {
    type_constructor_callbacks_.push_back(std::move(callback));
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
  // in this header--which would make including the header a costly thing to
  // do.
  std::unique_ptr<fidl::raw::TreeVisitor> tree_visitor_;

  std::vector<fit::function<void(const raw::File&)>> file_callbacks_;
  std::vector<fit::function<void(const raw::File&)>> exit_file_callbacks_;
  std::vector<fit::function<void(const raw::SourceElement&)>> source_element_callbacks_;
  std::vector<fit::function<void(const SourceSpan&, std::string_view)>> line_comment_callbacks_;
  std::vector<fit::function<void(const SourceSpan&, std::string_view)>>
      white_space_up_to_newline_callbacks_;
  std::vector<fit::function<void(const SourceSpan&)>> ignored_token_callbacks_;
  std::vector<fit::function<void(const raw::AliasDeclaration&)>> alias_callbacks_;
  std::vector<fit::function<void(const raw::Using&)>> using_callbacks_;
  std::vector<fit::function<void(const raw::ConstDeclaration&)>> const_declaration_callbacks_;
  std::vector<fit::function<void(const raw::ConstDeclaration&)>> exit_const_declaration_callbacks_;
  std::vector<fit::function<void(const raw::ProtocolDeclaration&)>> protocol_declaration_callbacks_;
  std::vector<fit::function<void(const raw::ProtocolDeclaration&)>>
      exit_protocol_declaration_callbacks_;
  std::vector<fit::function<void(const raw::ProtocolMethod&)>> method_callbacks_;
  std::vector<fit::function<void(const raw::ProtocolMethod&)>> event_callbacks_;

  std::vector<fit::function<void(const raw::Attribute&)>> attribute_callbacks_;
  std::vector<fit::function<void(const raw::OrdinaledLayoutMember&)>>
      ordinaled_layout_member_callbacks_;
  std::vector<fit::function<void(const raw::StructLayoutMember&)>> struct_layout_member_callbacks_;
  std::vector<fit::function<void(const raw::ValueLayoutMember&)>> value_layout_member_callbacks_;
  std::vector<fit::function<void(const raw::Layout&)>> layout_callbacks_;
  std::vector<fit::function<void(const raw::Layout&)>> exit_layout_callbacks_;
  std::vector<fit::function<void(const raw::IdentifierLayoutParameter&)>>
      identifier_layout_parameter_callbacks_;
  std::vector<fit::function<void(const raw::TypeDecl&)>> type_decl_callbacks_;
  std::vector<fit::function<void(const raw::TypeDecl&)>> exit_type_decl_callbacks_;
  std::vector<fit::function<void(const raw::TypeConstructor&)>> type_constructor_callbacks_;
};

}  // namespace fidl::linter

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_LINTING_TREE_CALLBACKS_H_
