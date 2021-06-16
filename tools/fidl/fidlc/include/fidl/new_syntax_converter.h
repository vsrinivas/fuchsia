// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDL_FIDLC_INCLUDE_FIDL_NEW_SYNTAX_CONVERTER_H_
#define TOOLS_FIDL_FIDLC_INCLUDE_FIDL_NEW_SYNTAX_CONVERTER_H_

// The ConvertingTreeVisitor takes a raw::File, and translates its textual
// representation from one syntax to another.
#include <regex>
#include <stack>

#include "flat/name.h"
#include "flat_ast.h"
#include "new_syntax_conversion.h"
#include "tree_visitor.h"
#include "underlying_type.h"

namespace fidl::conv {

class Converting;

class ConvertingTreeVisitor : public raw::DeclarationOrderTreeVisitor {
  friend Converting;

 public:
  explicit ConvertingTreeVisitor(fidl::utils::Syntax syntax, const flat::Library* library)
      : to_syntax_(syntax),
        last_conversion_end_(nullptr),
        in_response_with_error_(false),
        last_comment_(0),
        library_(library) {}
  // TODO(azaslavsky): I'll eventually remove the commented out block below.  At
  //   the moment it serves as a useful list of TreeVisitor methods that are
  //   intended to be left unmodified by the ConvertingTreeVisitor.
  // void OnBinaryOperatorConstant(std::unique_ptr<BinaryOperatorConstant> const&) override;
  // void OnCompoundIdentifier(std::unique_ptr<CompoundIdentifier> const&) override;
  // void OnConstant(std::unique_ptr<Constant> const&) override;
  // void OnEnumMember(std::unique_ptr<raw::EnumMember> const&) override;
  // void OnIdentifier(std::unique_ptr<Identifier> const&) override;
  // void OnIdentifierConstant(std::unique_ptr<IdentifierConstant> const&) override;
  // void OnLibraryDecl(std::unique_ptr<fidl::raw::LibraryDecl> const&) override;
  // void OnLiteral(std::unique_ptr<fidl::raw::Literal> const&) override;
  // void OnLiteralConstant(std::unique_ptr<LiteralConstant> const&) override;
  // void OnNullability(types::Nullability nullability) override;
  // void OnPrimitiveSubtype(types::PrimitiveSubtype subtype) override;
  // void OnProtocolDeclaration(std::unique_ptr<ProtocolDeclaration> const&) override;
  // void OnResourceDeclaration(std::unique_ptr<fidl::raw::ResourceDeclaration> const&) override;
  // void OnServiceDeclaration(std::unique_ptr<raw::ServiceDeclaration> const&) override;
  // void OnSourceElementStart(const raw::SourceElement&) override;
  // void OnSourceElementEnd(const raw::SourceElement&) override;

  // These "On*" methods should only be called on files written in the new
  // syntax, so immediately assert and error any time we enter one.
  void OnIdentifierLayoutParameter(
      std::unique_ptr<raw::IdentifierLayoutParameter> const& element) override {
    AbortUnimplemented();
  }
  void OnInlineLayoutReference(
      std::unique_ptr<raw::InlineLayoutReference> const& element) override {
    AbortUnimplemented();
  }
  void OnLayout(std::unique_ptr<raw::Layout> const& element) override { AbortUnimplemented(); }
  void OnLayoutMember(std::unique_ptr<raw::LayoutMember> const& element) override {
    AbortUnimplemented();
  }
  void OnLayoutParameter(std::unique_ptr<raw::LayoutParameter> const& element) override {
    AbortUnimplemented();
  }
  void OnLayoutParameterList(std::unique_ptr<raw::LayoutParameterList> const& element) override {
    AbortUnimplemented();
  }
  void OnLayoutReference(std::unique_ptr<raw::LayoutReference> const& element) override {
    AbortUnimplemented();
  }
  void OnLiteralLayoutParameter(
      std::unique_ptr<raw::LiteralLayoutParameter> const& element) override {
    AbortUnimplemented();
  }
  void OnNamedLayoutReference(std::unique_ptr<raw::NamedLayoutReference> const& element) override {
    AbortUnimplemented();
  }
  void OnOrdinaledLayoutMember(
      std::unique_ptr<raw::OrdinaledLayoutMember> const& element) override {
    AbortUnimplemented();
  }
  void OnStructLayoutMember(std::unique_ptr<raw::StructLayoutMember> const& element) override {
    AbortUnimplemented();
  }
  void OnTypeConstraints(std::unique_ptr<raw::TypeConstraints> const& element) override {
    AbortUnimplemented();
  }
  void OnTypeConstructorNew(std::unique_ptr<raw::TypeConstructorNew> const& element) override {
    AbortUnimplemented();
  }
  void OnTypeDecl(std::unique_ptr<raw::TypeDecl> const& element) override { AbortUnimplemented(); }
  void OnTypeLayoutParameter(std::unique_ptr<raw::TypeLayoutParameter> const& element) override {
    AbortUnimplemented();
  }
  void OnValueLayoutMember(std::unique_ptr<raw::ValueLayoutMember> const& element) override {
    AbortUnimplemented();
  }

  // The remaining "On*" methods are loosely organized by keyword.  All of them
  // must be overwritten by the implementation.

  // Attributes.
  void OnAttributeOld(const raw::AttributeOld& element) override;
  void OnAttributeListOld(std::unique_ptr<raw::AttributeListOld> const& element) override;

  // Bits.
  void OnBitsDeclaration(std::unique_ptr<raw::BitsDeclaration> const& element) override;
  void OnBitsMember(std::unique_ptr<raw::BitsMember> const& element) override;

  // Constants.
  void OnConstDeclaration(std::unique_ptr<raw::ConstDeclaration> const& element) override;

  // Enums.
  void OnEnumDeclaration(std::unique_ptr<raw::EnumDeclaration> const& element) override;
  void OnEnumMember(std::unique_ptr<raw::EnumMember> const& element) override;

  // Files.
  void OnFile(std::unique_ptr<raw::File> const& element) override;

  // Methods.
  void OnParameter(std::unique_ptr<raw::Parameter> const& element) override;
  void OnParameterListOld(std::unique_ptr<raw::ParameterListOld> const& element) override;
  void OnProtocolCompose(std::unique_ptr<raw::ProtocolCompose> const& element) override;
  void OnProtocolMethod(std::unique_ptr<raw::ProtocolMethod> const& element) override;

  // Resource Property.
  void OnResourceProperty(std::unique_ptr<fidl::raw::ResourceProperty> const& element) override;

  // Services.
  void OnServiceMember(std::unique_ptr<raw::ServiceMember> const& element) override;

  // Structs.
  void OnStructDeclaration(std::unique_ptr<raw::StructDeclaration> const& element) override;
  void OnStructMember(std::unique_ptr<raw::StructMember> const& element) override;

  // Tables.
  void OnTableDeclaration(std::unique_ptr<raw::TableDeclaration> const& element) override;
  void OnTableMember(std::unique_ptr<raw::TableMember> const& element) override;

  // Types.
  void OnTypeConstructorOld(std::unique_ptr<raw::TypeConstructorOld> const& element) override;

  // Unions.
  void OnUnionDeclaration(std::unique_ptr<raw::UnionDeclaration> const& element) override;
  void OnUnionMember(std::unique_ptr<raw::UnionMember> const& element) override;

  // Using.
  void OnUsing(std::unique_ptr<raw::Using> const& element) override;

  // Used to return a string with the converted output upon converter
  // completion.
  std::string converted_output() {
    if (to_syntax_ == utils::Syntax::kOld) {
      return converted_output_;
    }

    static std::regex find_deprecated_syntax("(^|\n)\\s*deprecated_syntax\\s*;\\s*\n");
    return std::regex_replace(converted_output_, find_deprecated_syntax, "\n",
                              std::regex_constants::format_first_only);
  }

 private:
  // String built over the course of the visitor's execution containing the
  // converted output.
  std::string converted_output_;

  // Tracks which syntax we will be converting to.  Setting this value to
  // kExisting is useful to validate that ConvertingTreeVisitor is working
  // properly: any compile-able FIDL file should be returned from this converter
  // with no changes if kOld is used.
  const fidl::utils::Syntax to_syntax_;

  // A stack of currently active conversions.  Each conversion in the stack
  // operates on a set of characters that are strictly contained within those of
  // its parent (ex, if the first entry in the stack is converting the "bar baz"
  // portion of "foo bar baz quux," the second entry may only convert spans
  // within that range, like "bar" or "baz").
  std::stack<std::unique_ptr<Conversion>> open_conversions_;

  // A char pointer tracing the last end point of the last conversion seen thus
  // far.  This is used to verify that prefix ranges are not copied multiple
  // times when doing nested conversions, and to ensure that the remaining text
  // after the final conversion gets copied.
  const char* last_conversion_end_;

  // A list of all C-Style "//"-leading comments in the file (ie, all comments
  // except doc comments).  We need to store this because some of the conversion
  // spans may include weirdly-placed comments that we do not want to lose.
  // Instead, such comments should be appended to the conversion's prefix.
  std::vector<std::unique_ptr<Token>> comments_;

  // Attributes are something of a special case.  Consider this struct with no
  // attributes, where the top-level StructDeclaration conversion span
  // (bounded by single arrows: «___») necessarily precedes all of its child
  // spans (each bounded by inside of ««___»»):
  //
  //   «struct S» {
  //     ««bool foo»»;
  //     ««int8 vector<bar>»»;
  //   };
  //
  // In fact, every conversion (except for attributes) exhibits this property.
  // But if we add an attribute to the declaration above, this no longer holds:
  //
  //   ««[MaxBytes]»»
  //   «struct S» {
  //     ««bool foo»»;
  //     ««int8 vector<bar>»»;
  //   };
  //
  // Running AddChildText() on the children of this StructDeclaration in order
  // would result in the [MaxBytes] appearing after the top-level
  // type S = struct statement, which is not correct.
  //
  // The solution to this problem is for every raw AST node that can carry
  // attributes to visit its AttributeList child twice: once prior to starting
  // the conversion for the parent node itself, and then again as part of the
  // normal flow of the default TreeVisitor.  To prevent attributes from
  // appearing twice, each time an AttributeList is visited, it adds its pointer
  // to this set if it is not already present.  If it is already present
  // (meaning this is the second visit), then the visit is a noop, resulting in
  // only one conversion in the correct place.
  std::set<raw::AttributeListOld*> attribute_lists_seen_;

  // ParameterLists for responses of methods that also return errors must be
  // converted slightly differently - whereas a regular two-way method with no
  // response parameters like `Foo() -> ()` would be left untouched, such a
  // method with an error, like `Foo() -> () error zx.status` must have an empty
  // struct in the response position, like `MyMethod() -> () error zx.status`.
  // This boolean keeps track of whether or not we have entered such a response.
  bool in_response_with_error_;

  // Keeps track of the last comment in the comments_ list to have been "tested"
  // for being inside a conversion span.  The char pointer at the vector index
  // pointed to by this member should never exceed the char pointer held in
  // last_conversion_end_.
  std::size_t last_comment_;

  // A pointer to the flat::Library representation of the file being visited.
  // This will be used when resolving and converting type definitions that
  // are behind aliases, defined in the imported libraries, and so forth.
  const flat::Library* library_;

  // Meant to be called from inside the "OnTypeConstructor" method in the
  // implementation.  For that method to do its work properly, it must be able
  // to deduce the built-in type underpinning the type declaration.  For
  // example, if OnTypeConstructor is currently looking at the type declaration
  // "Foo<Bar>:4," what do "Foo" and "Bar" represent?  The conversion applied
  // will look very different depending on which built-ins those identifiers
  // resolve to.
  std::optional<UnderlyingType> resolve(const std::unique_ptr<raw::TypeConstructorOld>& type_ctor);

  void static AbortUnimplemented() {
    assert(
        false &&
        "input files to fidlconv must not contain any raw AST nodes exclusive to the new syntax");
  }
};

class Converting {
 public:
  // Helper method for starting a new conversion.  It takes three arguments: a
  // Conversion object specifying the type of conversion being attempted, as
  // well as two tokens representing the start and end point of the span that
  // will need to be modified.  For example, if we are attempting to convert the
  // element "const uint8 FOO = 5;" the first argument will be a unique_ptr to a
  // NameAndTypeConversion (to re-order "uint8" after "FOO"), the second will be
  // a token pointing to "uint8," and the third a token pointing to "FOO."
  //
  // By specifying the start and end points within the element being converted,
  // we are able to advance the last_conversion_end_ pointer to the end, which
  // prevents double conversion.  Further, all text between the previous value
  // of last_conversion_end_ and the start token may be blindly copied, since we
  // are now sure that there are not conversions taking place in that span.
  Converting(ConvertingTreeVisitor* ctv, std::unique_ptr<Conversion> conversion, const Token& start,
             const Token& end);

  // If a conversion is not the last remaining entry in the open_conversions_
  // stack, its stringified output is simply passed to the top entry of that
  // stack, to be incorporated as a nested element in that entry.  If it is the
  // last entry, the text is written to the output string instead.
  ~Converting();

 private:
  ConvertingTreeVisitor* ctv_;
};

}  // namespace fidl::conv

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_NEW_SYNTAX_CONVERTER_H_
