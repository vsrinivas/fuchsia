// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <stack>

#include <zxtest/zxtest.h>

#include "fidl/raw_ast.h"
#include "fidl/tree_visitor.h"
#include "test_library.h"

// This test provides a way to write comprehensive unit tests on the fidlc
// parser. Each test case provides a SourceElement type and a list of source
// strings, with expected source spans of that type marked with special
// characters (see kMarkerLeft and kMarkerRight). The markers can be nested and
// are expected to specify all occurences of that type of SourceElement.

// Test cases are defined near the bottom of the file as a
// std::vector<TestCase>.

// For each test case:
// - extract_expected_spans creates a multiset of source spans from a marked
//   source string.
// - SourceSpanChecker inherits from TreeVisitor, and it collects all the actual
//   spans of a given ElementType by walking the AST in each test case.
// - then the expected spans are compared against the actual spans via set
//   arithmetic.

namespace {

#define FOR_ENUM_VARIANTS(DO)   \
  DO(Identifier)                \
  DO(CompoundIdentifier)        \
  DO(StringLiteral)             \
  DO(NumericLiteral)            \
  DO(TrueLiteral)               \
  DO(FalseLiteral)              \
  DO(Ordinal64)                 \
  DO(IdentifierConstant)        \
  DO(LiteralConstant)           \
  DO(BinaryOperatorConstant)    \
  DO(Attribute)                 \
  DO(AttributeList)             \
  DO(TypeConstructor)           \
  DO(Library)                   \
  DO(Using)                     \
  DO(ConstDeclaration)          \
  DO(BitsMember)                \
  DO(BitsDeclaration)           \
  DO(EnumMember)                \
  DO(EnumDeclaration)           \
  DO(Parameter)                 \
  DO(ParameterList)             \
  DO(ProtocolCompose)           \
  DO(ProtocolMethod)            \
  DO(ProtocolDeclaration)       \
  DO(ResourceDeclaration)       \
  DO(ResourceProperty)          \
  DO(ServiceMember)             \
  DO(ServiceDeclaration)        \
  DO(StructMember)              \
  DO(StructDeclaration)         \
  DO(TableMember)               \
  DO(TableDeclaration)          \
  DO(UnionMember)               \
  DO(UnionDeclaration)          \
  DO(AttributeArg)              \
  DO(AttributeNew)              \
  DO(AttributeListNew)          \
  DO(Modifiers)                 \
  DO(IdentifierLayoutParameter) \
  DO(LiteralLayoutParameter)    \
  DO(TypeLayoutParameter)       \
  DO(LayoutParameterList)       \
  DO(OrdinaledLayoutMember)     \
  DO(StructLayoutMember)        \
  DO(ValueLayoutMember)         \
  DO(Layout)                    \
  DO(InlineLayoutReference)     \
  DO(NamedLayoutReference)      \
  DO(ParameterListNew)          \
  DO(TypeConstraints)           \
  DO(TypeConstructorNew)        \
  DO(TypeDecl)

#define MAKE_ENUM_VARIANT(VAR) VAR,
enum ElementType { FOR_ENUM_VARIANTS(MAKE_ENUM_VARIANT) };

#define MAKE_ENUM_NAME(VAR) #VAR,
const std::string kElementTypeNames[] = {FOR_ENUM_VARIANTS(MAKE_ENUM_NAME)};

std::string element_type_str(ElementType type) { return kElementTypeNames[type]; }

// Used to delineate spans in source code. E.g.,
// const uint32 «three» = 3;
constexpr std::string_view kMarkerLeft = "«";
constexpr std::string_view kMarkerRight = "»";

// Used to delineate the decl_start_tokens that have been temporarily added to
// the raw AST for fidlconv.
constexpr std::string_view kDeclStartTokenLeft = "⸢";
constexpr std::string_view kDeclStartTokenRight = "⸥";

class SourceSpanVisitor : public fidl::raw::TreeVisitor {
 public:
  SourceSpanVisitor(ElementType test_case_type) : test_case_type_(test_case_type) {}

  const std::multiset<std::string>& spans() { return spans_; }

  const std::multiset<std::string>& decl_start_tokens() { return decl_start_tokens_; }

  void OnIdentifier(std::unique_ptr<fidl::raw::Identifier> const& element) override {
    CheckSpanOfType(ElementType::Identifier, *element);
  }
  void OnCompoundIdentifier(
      std::unique_ptr<fidl::raw::CompoundIdentifier> const& element) override {
    CheckSpanOfType(ElementType::CompoundIdentifier, *element);
    TreeVisitor::OnCompoundIdentifier(element);
  }
  void OnStringLiteral(fidl::raw::StringLiteral& element) override {
    CheckSpanOfType(ElementType::StringLiteral, element);
    TreeVisitor::OnStringLiteral(element);
  }
  void OnNumericLiteral(fidl::raw::NumericLiteral& element) override {
    CheckSpanOfType(ElementType::NumericLiteral, element);
    TreeVisitor::OnNumericLiteral(element);
  }
  void OnTrueLiteral(fidl::raw::TrueLiteral& element) override {
    CheckSpanOfType(ElementType::TrueLiteral, element);
    TreeVisitor::OnTrueLiteral(element);
  }
  void OnFalseLiteral(fidl::raw::FalseLiteral& element) override {
    CheckSpanOfType(ElementType::FalseLiteral, element);
    TreeVisitor::OnFalseLiteral(element);
  }
  void OnOrdinal64(fidl::raw::Ordinal64& element) override {
    CheckSpanOfType(ElementType::Ordinal64, element);
    TreeVisitor::OnOrdinal64(element);
  }
  void OnIdentifierConstant(
      std::unique_ptr<fidl::raw::IdentifierConstant> const& element) override {
    CheckSpanOfType(ElementType::IdentifierConstant, *element);
    TreeVisitor::OnIdentifierConstant(element);
  }
  void OnLiteralConstant(std::unique_ptr<fidl::raw::LiteralConstant> const& element) override {
    CheckSpanOfType(ElementType::LiteralConstant, *element);
    TreeVisitor::OnLiteralConstant(element);
  }
  void OnBinaryOperatorConstant(
      std::unique_ptr<fidl::raw::BinaryOperatorConstant> const& element) override {
    CheckSpanOfType(ElementType::BinaryOperatorConstant, *element);
    TreeVisitor::OnBinaryOperatorConstant(element);
  }
  void OnAttributeOld(const fidl::raw::AttributeOld& element) override {
    CheckSpanOfType(ElementType::Attribute, element);
    TreeVisitor::OnAttributeOld(element);
  }
  void OnAttributeListOld(std::unique_ptr<fidl::raw::AttributeListOld> const& element) override {
    CheckSpanOfType(ElementType::AttributeList, *element);
    TreeVisitor::OnAttributeListOld(element);
  }
  void OnTypeConstructorOld(
      std::unique_ptr<fidl::raw::TypeConstructorOld> const& element) override {
    CheckSpanOfType(ElementType::TypeConstructor, *element);
    TreeVisitor::OnTypeConstructorOld(element);
  }
  void OnLibraryDecl(std::unique_ptr<fidl::raw::LibraryDecl> const& element) override {
    CheckSpanOfType(ElementType::Library, *element);
    TreeVisitor::OnLibraryDecl(element);
  }
  void OnUsing(std::unique_ptr<fidl::raw::Using> const& element) override {
    CheckSpanOfType(ElementType::Using, *element);
    TreeVisitor::OnUsing(element);
  }
  void OnConstDeclaration(std::unique_ptr<fidl::raw::ConstDeclaration> const& element) override {
    CheckSpanOfType(ElementType::ConstDeclaration, *element);
    TreeVisitor::OnConstDeclaration(element);
  }
  void OnBitsMember(std::unique_ptr<fidl::raw::BitsMember> const& element) override {
    CheckSpanOfType(ElementType::BitsMember, *element);
    TreeVisitor::OnBitsMember(element);
  }
  void OnBitsDeclaration(std::unique_ptr<fidl::raw::BitsDeclaration> const& element) override {
    CheckSpanOfType(ElementType::BitsDeclaration, *element);
    CheckDeclStartToken(ElementType::BitsDeclaration, *element->decl_start_token);
    TreeVisitor::OnBitsDeclaration(element);
  }
  void OnEnumMember(std::unique_ptr<fidl::raw::EnumMember> const& element) override {
    CheckSpanOfType(ElementType::EnumMember, *element);
    TreeVisitor::OnEnumMember(element);
  }
  void OnEnumDeclaration(std::unique_ptr<fidl::raw::EnumDeclaration> const& element) override {
    CheckSpanOfType(ElementType::EnumDeclaration, *element);
    CheckDeclStartToken(ElementType::EnumDeclaration, *element->decl_start_token);
    TreeVisitor::OnEnumDeclaration(element);
  }
  void OnParameter(std::unique_ptr<fidl::raw::Parameter> const& element) override {
    CheckSpanOfType(ElementType::Parameter, *element);
    TreeVisitor::OnParameter(element);
  }
  void OnParameterListOld(std::unique_ptr<fidl::raw::ParameterListOld> const& element) override {
    CheckSpanOfType(ElementType::ParameterList, *element);
    TreeVisitor::OnParameterListOld(element);
  }
  void OnParameterListNew(std::unique_ptr<fidl::raw::ParameterListNew> const& element) override {
    CheckSpanOfType(ElementType::ParameterListNew, *element);
    TreeVisitor::OnParameterListNew(element);
  }
  void OnProtocolCompose(std::unique_ptr<fidl::raw::ProtocolCompose> const& element) override {
    CheckSpanOfType(ElementType::ProtocolCompose, *element);
    TreeVisitor::OnProtocolCompose(element);
  }
  void OnProtocolDeclaration(
      std::unique_ptr<fidl::raw::ProtocolDeclaration> const& element) override {
    CheckSpanOfType(ElementType::ProtocolDeclaration, *element);
    TreeVisitor::OnProtocolDeclaration(element);
  }
  void OnProtocolMethod(std::unique_ptr<fidl::raw::ProtocolMethod> const& element) override {
    CheckSpanOfType(ElementType::ProtocolMethod, *element);
    TreeVisitor::OnProtocolMethod(element);
  }
  void OnResourceProperty(std::unique_ptr<fidl::raw::ResourceProperty> const& element) override {
    CheckSpanOfType(ElementType::ResourceProperty, *element);
    TreeVisitor::OnResourceProperty(element);
  }
  void OnResourceDeclaration(
      std::unique_ptr<fidl::raw::ResourceDeclaration> const& element) override {
    CheckSpanOfType(ElementType::ResourceDeclaration, *element);
    TreeVisitor::OnResourceDeclaration(element);
  }
  void OnServiceMember(std::unique_ptr<fidl::raw::ServiceMember> const& element) override {
    CheckSpanOfType(ElementType::ServiceMember, *element);
    TreeVisitor::OnServiceMember(element);
  }
  void OnServiceDeclaration(
      std::unique_ptr<fidl::raw::ServiceDeclaration> const& element) override {
    CheckSpanOfType(ElementType::ServiceDeclaration, *element);
    TreeVisitor::OnServiceDeclaration(element);
  }
  void OnStructMember(std::unique_ptr<fidl::raw::StructMember> const& element) override {
    CheckSpanOfType(ElementType::StructMember, *element);
    TreeVisitor::OnStructMember(element);
  }
  void OnStructDeclaration(std::unique_ptr<fidl::raw::StructDeclaration> const& element) override {
    CheckSpanOfType(ElementType::StructDeclaration, *element);
    CheckDeclStartToken(ElementType::StructDeclaration, *element->decl_start_token);
    TreeVisitor::OnStructDeclaration(element);
  }
  void OnTableMember(std::unique_ptr<fidl::raw::TableMember> const& element) override {
    CheckSpanOfType(ElementType::TableMember, *element);
    TreeVisitor::OnTableMember(element);
  }
  void OnTableDeclaration(std::unique_ptr<fidl::raw::TableDeclaration> const& element) override {
    CheckSpanOfType(ElementType::TableDeclaration, *element);
    CheckDeclStartToken(ElementType::TableDeclaration, *element->decl_start_token);
    TreeVisitor::OnTableDeclaration(element);
  }
  void OnUnionMember(std::unique_ptr<fidl::raw::UnionMember> const& element) override {
    CheckSpanOfType(ElementType::UnionMember, *element);
    TreeVisitor::OnUnionMember(element);
  }
  void OnUnionDeclaration(std::unique_ptr<fidl::raw::UnionDeclaration> const& element) override {
    CheckSpanOfType(ElementType::UnionDeclaration, *element);
    CheckDeclStartToken(ElementType::UnionDeclaration, *element->decl_start_token);
    TreeVisitor::OnUnionDeclaration(element);
  }

  // TODO(fxbug.dev/70247): Remove these guards and old syntax visitors.
  // --- start new syntax ---
  void OnAttributeArg(fidl::raw::AttributeArg const& element) override {
    CheckSpanOfType(ElementType::AttributeArg, element);
    TreeVisitor::OnAttributeArg(element);
  }
  void OnAttributeNew(fidl::raw::AttributeNew const& element) override {
    CheckSpanOfType(ElementType::AttributeNew, element);
    TreeVisitor::OnAttributeNew(element);
  }
  void OnAttributeListNew(std::unique_ptr<fidl::raw::AttributeListNew> const& element) override {
    CheckSpanOfType(ElementType::AttributeListNew, *element);
    TreeVisitor::OnAttributeListNew(element);
  }
  void OnModifiers(std::unique_ptr<fidl::raw::Modifiers> const& element) override {
    CheckSpanOfType(ElementType::Modifiers, *element);
    TreeVisitor::OnModifiers(element);
  }
  void OnIdentifierLayoutParameter(
      std::unique_ptr<fidl::raw::IdentifierLayoutParameter> const& element) override {
    CheckSpanOfType(ElementType::IdentifierLayoutParameter, *element);
    TreeVisitor::OnIdentifierLayoutParameter(element);
  }
  void OnLiteralLayoutParameter(
      std::unique_ptr<fidl::raw::LiteralLayoutParameter> const& element) override {
    CheckSpanOfType(ElementType::LiteralLayoutParameter, *element);
    TreeVisitor::OnLiteralLayoutParameter(element);
  }
  void OnTypeLayoutParameter(
      std::unique_ptr<fidl::raw::TypeLayoutParameter> const& element) override {
    CheckSpanOfType(ElementType::TypeLayoutParameter, *element);
    TreeVisitor::OnTypeLayoutParameter(element);
  }
  void OnLayoutParameterList(
      std::unique_ptr<fidl::raw::LayoutParameterList> const& element) override {
    CheckSpanOfType(ElementType::LayoutParameterList, *element);
    TreeVisitor::OnLayoutParameterList(element);
  }
  void OnOrdinaledLayoutMember(
      std::unique_ptr<fidl::raw::OrdinaledLayoutMember> const& element) override {
    CheckSpanOfType(ElementType::OrdinaledLayoutMember, *element);
    TreeVisitor::OnOrdinaledLayoutMember(element);
  }
  void OnStructLayoutMember(
      std::unique_ptr<fidl::raw::StructLayoutMember> const& element) override {
    CheckSpanOfType(ElementType::StructLayoutMember, *element);
    TreeVisitor::OnStructLayoutMember(element);
  }
  void OnValueLayoutMember(std::unique_ptr<fidl::raw::ValueLayoutMember> const& element) override {
    CheckSpanOfType(ElementType::ValueLayoutMember, *element);
    TreeVisitor::OnValueLayoutMember(element);
  }
  void OnLayout(std::unique_ptr<fidl::raw::Layout> const& element) override {
    CheckSpanOfType(ElementType::Layout, *element);
    TreeVisitor::OnLayout(element);
  }
  void OnInlineLayoutReference(
      std::unique_ptr<fidl::raw::InlineLayoutReference> const& element) override {
    CheckSpanOfType(ElementType::InlineLayoutReference, *element);
    TreeVisitor::OnInlineLayoutReference(element);
  }
  void OnNamedLayoutReference(
      std::unique_ptr<fidl::raw::NamedLayoutReference> const& element) override {
    CheckSpanOfType(ElementType::NamedLayoutReference, *element);
    TreeVisitor::OnNamedLayoutReference(element);
  }
  void OnTypeConstraints(std::unique_ptr<fidl::raw::TypeConstraints> const& element) override {
    CheckSpanOfType(ElementType::TypeConstraints, *element);
    TreeVisitor::OnTypeConstraints(element);
  }
  void OnTypeConstructorNew(
      std::unique_ptr<fidl::raw::TypeConstructorNew> const& element) override {
    CheckSpanOfType(ElementType::TypeConstructorNew, *element);
    TreeVisitor::OnTypeConstructorNew(element);
  }
  void OnTypeDecl(std::unique_ptr<fidl::raw::TypeDecl> const& element) override {
    CheckSpanOfType(ElementType::TypeDecl, *element);
    TreeVisitor::OnTypeDecl(element);
  }

 private:
  // Called on every node of the AST that we visit. We collect spans of the
  // ElementType we are looking for as we traverse the tree, and store them in a
  // multiset.
  void CheckSpanOfType(const ElementType type, const fidl::raw::SourceElement& element) {
    if (type != test_case_type_) {
      return;
    }
    spans_.insert(std::string(element.span().data()));
  }

  // TODO(fxbug.dev/70247): when fidlconv is removed, make sure to remove all of
  //  the "decl_start_token" stuff as well, as that is the only tool that uses
  //  it.
  void CheckDeclStartToken(const ElementType type, const fidl::Token& token) {
    if (type != test_case_type_) {
      return;
    }
    decl_start_tokens_.insert(std::string(token.span().data()));
  }

  ElementType test_case_type_;
  std::multiset<std::string> spans_;
  std::multiset<std::string> decl_start_tokens_;
};

std::string replace_markers(const std::string& source, std::string_view left_replace,
                            std::string_view right_replace, const std::string_view marker_left,
                            const std::string_view marker_right) {
  std::string result(source);

  const auto replace_all = [&](std::string_view pattern, std::string_view replace_with) {
    std::string::size_type i = result.find(pattern);
    while (i != std::string::npos) {
      result.replace(i, pattern.length(), replace_with);
      i = result.find(pattern, i + replace_with.length());
    }
  };

  replace_all(marker_left, left_replace);
  replace_all(marker_right, right_replace);
  return result;
}

std::string remove_markers(const std::string& source) {
  auto removed_span_markers = replace_markers(source, "", "", kMarkerLeft, kMarkerRight);
  return replace_markers(removed_span_markers, "", "", kDeclStartTokenLeft, kDeclStartTokenRight);
}

// Extracts marked source spans from a given source string.
// If source spans are incorrectly marked (missing or extra markers), returns
// std::nullopt; otherwise, returns a multiset of expected spans.
std::optional<std::multiset<std::string>> extract_expected_spans(
    const std::string& source, std::vector<std::string>* errors, const std::string_view marker_left,
    const std::string_view marker_right) {
  std::stack<size_t> stack;
  std::multiset<std::string> spans;

  const auto match = [&](size_t i, std::string_view marker) {
    return marker.compare(source.substr(i, marker.length())) == 0;
  };

  for (size_t i = 0; i < source.length();) {
    if (match(i, marker_left)) {
      i += marker_left.length();
      stack.push(i);
    } else if (match(i, marker_right)) {
      if (stack.empty()) {
        std::stringstream error_msg;
        error_msg << "unexpected closing marker '" << marker_right << "' at position " << i
                  << " in source string";
        errors->push_back(error_msg.str());
        // Return an empty set if errors
        return std::nullopt;
      }

      const std::string span = remove_markers(source.substr(stack.top(),  // index of left marker
                                                            i - stack.top())  // length of span
      );
      stack.pop();
      spans.insert(span);
      i += marker_right.length();
    } else {
      i += 1;
    }
  }

  if (!stack.empty()) {
    std::stringstream error_msg;
    error_msg << "expected closing marker '" << marker_right << "'";
    errors->push_back(error_msg.str());
    // Return an empty set if errors
    return std::nullopt;
  }

  return spans;
}

struct TestCase {
  ElementType type;
  std::vector<std::string> marked_sources;
};

const std::vector<TestCase> test_cases = {
    {ElementType::Identifier,
     {
         R"FIDL(library «x»; struct «S» { «int64» «i»; };)FIDL",
         R"FIDL(library «x»; struct «S» { «handle»:«THREAD» «h»; };)FIDL",
     }},
    {ElementType::CompoundIdentifier,
     {
         R"FIDL(library «foo.bar.baz»;)FIDL",
     }},
    {ElementType::StringLiteral,
     {
         R"FIDL(library x; const string x = «"hello"»;)FIDL",
         R"FIDL(library x; [attr=«"foo"»]const string x = «"goodbye"»;)FIDL",
     }},
    {ElementType::NumericLiteral,
     {
         R"FIDL(library x; const uint8 x = «42»;)FIDL",
     }},
    {ElementType::TrueLiteral,
     {
         R"FIDL(library x; const bool x = «true»;)FIDL",
     }},
    {ElementType::FalseLiteral,
     {
         R"FIDL(library x; const bool x = «false»;)FIDL",
     }},
    {ElementType::Ordinal64,
     {
         R"FIDL(library x; union U { «1:» uint8 one; };)FIDL",
     }},
    {ElementType::IdentifierConstant,
     {
         R"FIDL(library x; const bool x = true; const bool y = «x»;)FIDL",
     }},
    {ElementType::LiteralConstant,
     {
         R"FIDL(library x; const bool x = «true»;)FIDL",
         R"FIDL(library x; const uint8 x = «42»;)FIDL",
         R"FIDL(library x; const string x = «"hi"»;)FIDL",
     }},
    {ElementType::BinaryOperatorConstant,
     {
         R"FIDL(library x;
const uint8 one = 0x0001;
const uint16 two_fifty_six = 0x0100;
const uint16 two_fifty_seven = «one | two_fifty_six»;
         )FIDL",
         R"FIDL(library x; const uint16 two_fifty_seven = «0x0001 | 0x0100»;)FIDL",
     }},
    {ElementType::ConstDeclaration,
     {
         R"FIDL(library example;
«const uint32 C_SIMPLE   = 11259375»;
«const uint32 C_HEX_S    = 0xABCDEF»;
«const uint32 C_HEX_L    = 0XABCDEF»;
«const uint32 C_BINARY_S = 0b101010111100110111101111»;
«const uint32 C_BINARY_L = 0B101010111100110111101111»;
      )FIDL"}},
    {ElementType::EnumDeclaration,
     {
         R"FIDL(library example; «⸢enum⸥ TestEnum { A = 1; B = 2; }»;)FIDL",
         R"FIDL(library example; «[attr] ⸢strict⸥ enum TestEnum { A = 1; B = 2; }»;)FIDL",
         R"FIDL(library example; «⸢flexible⸥ enum TestEnum { A = 1; B = 2; }»;)FIDL",
     }},
    {ElementType::EnumMember,
     {
         R"FIDL(library x; enum y { «[attr] A = identifier»; };)FIDL",
     }},
    {ElementType::BitsDeclaration,
     {
         R"FIDL(library example; «⸢bits⸥ TestBits { A = 1; B = 2; }»;)FIDL",
         R"FIDL(library example; «⸢strict⸥ bits TestBits { A = 1; B = 2; }»;)FIDL",
         R"FIDL(library example; «[attr] ⸢flexible⸥ bits TestBits { A = 1; B = 2; }»;)FIDL",
     }},
    {ElementType::BitsMember,
     {
         R"FIDL(library x; bits y { «A = 0x1»; «B = 0x2»; };)FIDL",
     }},
    {ElementType::AttributeList,
     {
         R"FIDL(«[a]» library x;)FIDL",
         R"FIDL(«[a, b="1"]» library x;)FIDL",
     }},
    {ElementType::Attribute,
     {
         R"FIDL([«a»] library x;)FIDL",
         R"FIDL([«a», «b="1"»] library x;)FIDL",
     }},
    {ElementType::Library,
     {
         R"FIDL(«library x»; using y;)FIDL",
         R"FIDL(«library x.y.z»; using y;)FIDL",
     }},
    {ElementType::Using,
     {
         R"FIDL(library x; «using y»;)FIDL",
         R"FIDL(library x; «using y as z»;)FIDL",
     }},
    {ElementType::ResourceDeclaration, {R"FIDL(
     library example; «resource_definition Res : uint32 { properties { Enum subtype; }; }»;)FIDL"}},
    {ElementType::ResourceProperty, {R"FIDL(
     library example; resource_definition Res : uint32 { properties { «Enum subtype»; }; };)FIDL"}},
    {ElementType::ProtocolDeclaration,
     {
         R"FIDL(library x; «protocol X {}»;)FIDL",
         R"FIDL(library x; «[attr] protocol X { compose OtherProtocol; }»;)FIDL",
     }},
    {ElementType::ProtocolMethod,  // Method
     {
         R"FIDL(library x; protocol X { «Method(int32 a) -> (bool res)»; };)FIDL",
         R"FIDL(library x; protocol X { «-> Event(bool res)»; };)FIDL",
     }},
    {ElementType::ProtocolMethod,
     {
         R"FIDL(library x; protocol X { «Method()»; };)FIDL",
         R"FIDL(library x; protocol X { «[attr] Method(int32 a, bool b)»; };)FIDL",
         R"FIDL(library x; protocol X { «Method(int32 a) -> ()»; };)FIDL",
         R"FIDL(library x; protocol X { «Method(int32 a) -> (bool res, int32 res2)»; };)FIDL",
     }},
    {ElementType::ProtocolMethod,  // Event
     {
         R"FIDL(library x; protocol X { «-> Event()»; };)FIDL",
         R"FIDL(library x; protocol X { «[attr] -> Event(bool res, int32 res2)»; };)FIDL",
     }},
    {ElementType::ProtocolCompose,
     {
         R"FIDL(library x; protocol X { «compose OtherProtocol»; };)FIDL",
         R"FIDL(library x; protocol X { «[attr] compose OtherProtocol»; };)FIDL",
         R"FIDL(library x; protocol X {
            «/// Foo
            compose OtherProtocol»;
          };)FIDL",
     }},
    {ElementType::ParameterList,
     {
         R"FIDL(library x; protocol X { Method«()»; };)FIDL",
         R"FIDL(library x; protocol X { Method«(int32 a, bool b)»; };)FIDL",
     }},
    {ElementType::Parameter,
     {
         R"FIDL(library x; protocol X { Method(«int32 a», «bool b»); };)FIDL",
         R"FIDL(library x; protocol X { -> Event(«int32 a», «bool b»); };)FIDL",
     }},
    {ElementType::ServiceDeclaration,
     {
         R"FIDL(library x; «service X {}»;)FIDL",
         R"FIDL(library x; protocol P {}; «service X { P Z; }»;)FIDL",
     }},
    {ElementType::ServiceMember,
     {
         R"FIDL(library x; protocol P {}; service X { «P Z»; };)FIDL",
         R"FIDL(library x; protocol P {}; service X { «[attr] P Z»; };)FIDL",
     }},
    {ElementType::StructDeclaration,
     {
         R"FIDL(library x; «⸢struct⸥ X { bool y; [attr] int32 z = 2; }»;)FIDL",
         R"FIDL(library x; «⸢resource⸥ struct X { bool y; [attr] int32 z = 2; }»;)FIDL",
         R"FIDL(library x; «[attr] ⸢resource⸥ struct X { bool y; [attr] int32 z = 2; }»;)FIDL",
     }},
    {ElementType::StructMember,
     {
         R"FIDL(library x; struct X { «bool y»; «[attr] int32 z = 2»; };)FIDL",
     }},
    {ElementType::TableDeclaration,
     {
         R"FIDL(library x; «[attr] ⸢resource⸥ table X {
          1: bool y;
          2: reserved;
          [attr] 3: int32 z;
      }»;)FIDL",
         R"FIDL(library x; «⸢resource⸥ table X {
          1: bool y;
      }»;)FIDL",
         R"FIDL(library x; «⸢table⸥ X {
          1: bool y;
      }»;)FIDL",
     }},
    {ElementType::TableMember,
     {
         R"FIDL(library x; [attr] table X {
          «1: bool y»;
          «2: reserved»;
          «[attr] 3: int32 z»;
      };)FIDL",
     }},
    {ElementType::UnionDeclaration,
     {
         R"FIDL(library x; «[attr] ⸢union⸥ X {
          1: int64 intval;
          2: reserved;
          [attr] 3: float64 floatval;
          4: string:MAX_STRING_SIZE stringval;
      }»;)FIDL",
         R"FIDL(library x; «[attr] ⸢strict⸥ union X {
          1: int64 intval;
      }»;)FIDL",
         R"FIDL(library x; «⸢flexible⸥ union X {
          1: int64 intval;
      }»;)FIDL",
         R"FIDL(library x; «⸢resource⸥ union X {
          1: int64 intval;
      }»;)FIDL",
         R"FIDL(library x; «⸢flexible⸥ resource union X {
          1: int64 intval;
      }»;)FIDL",
         R"FIDL(library x; «[attr] ⸢resource⸥ flexible union X {
          1: int64 intval;
      }»;)FIDL",
     }},
    {ElementType::UnionMember,
     {
         R"FIDL(library x; [attr] union X {
          «1: int64 intval»;
          «2: reserved»;
          «[attr] 3: float64 floatval»;
          «4: string:MAX_STRING_SIZE stringval»;
      };)FIDL",
     }},
    {ElementType::TypeConstructor,
     {
         R"FIDL(library x; const «int32» x = 1;)FIDL",
         R"FIDL(library x; const «handle:<VMO, zx.rights.READ>?» x = 1;)FIDL",
         R"FIDL(library x; const «Foo<«Bar<«handle:VMO»>:20»>?» x = 1;)FIDL",
         R"FIDL(library x; const «handle:VMO» x = 1;)FIDL",
     }},
};

// TODO(fxbug.dev/70247): Remove these guards and old syntax visitors.
// --- start new syntax ---
const std::vector<TestCase> new_syntax_test_cases = {
    {ElementType::AttributeArg,
     {
         R"FIDL(library x; @attr(«"foo"») const bool MY_BOOL = false;)FIDL",
     }},
    {ElementType::AttributeNew,
     {
         R"FIDL(library x; «@foo("foo")» «@bar» const bool MY_BOOL = false;)FIDL",
         R"FIDL(library x;
          «@foo("foo")»
          «@bar»
          const bool MY_BOOL = false;
        )FIDL",
     }},
    {ElementType::Modifiers,
     {
         R"FIDL(library x; type MyBits = «flexible» bits { MY_VALUE = 1; };)FIDL",
         R"FIDL(library x; type MyBits = «strict» bits : uint32 { MY_VALUE = 1; };)FIDL",
         R"FIDL(library x; type MyEnum = «flexible» enum : uint32 { MY_VALUE = 1; };)FIDL",
         R"FIDL(library x; type MyEnum = «strict» enum { MY_VALUE = 1; };)FIDL",
         R"FIDL(library x; type MyStruct = «resource» struct {};)FIDL",
         R"FIDL(library x; type MyTable = «resource» table { 1: my_member bool; };)FIDL",
         R"FIDL(library x; type MyUnion = «resource» union { 1: my_member bool; };)FIDL",
         R"FIDL(library x; type MyUnion = «flexible» union { 1: my_member bool; };)FIDL",
         R"FIDL(library x; type MyUnion = «strict» union { 1: my_member bool; };)FIDL",
         R"FIDL(library x; type MyUnion = «resource strict» union { 1: my_member bool; };)FIDL",
         // Note that the following 3 tests have union members named like modifiers.
         R"FIDL(library x; type MyUnion = «resource flexible» union { 1: my_member resource; };)FIDL",
         R"FIDL(library x; type MyUnion = «strict resource» union { 1: my_member flexible; };)FIDL",
         R"FIDL(library x; type MyUnion = «flexible resource» union { 1: my_member strict; };)FIDL",
     }},
    {ElementType::NamedLayoutReference,
     {
         R"FIDL(library x;
          type S = struct {
            intval «int64»;
            boolval «bool» = false;
            stringval «string»:MAX_STRING_SIZE;
            inner struct {
              floatval «float64»;
              uintval «uint8» = 7;
              vecval «vector»<«vector»<Foo>>;
              arrval «array»<uint8,4>;
            };
          };
         )FIDL",
     }},
    {ElementType::IdentifierLayoutParameter,
     {
         R"FIDL(library x; type a = bool; const b uint8 = 4; type y = array<«a»,«b»>;)FIDL",
     }},
    {ElementType::LiteralLayoutParameter,
     {
         R"FIDL(library x; type y = array<uint8,«4»>;)FIDL",
         R"FIDL(library x; type y = vector<array<uint8,«4»>>;)FIDL",
     }},
    {ElementType::TypeLayoutParameter,
     {
         R"FIDL(library x; type y = array<uint8,4>;)FIDL",
         R"FIDL(library x; type y = vector<«array<uint8,4>»>;)FIDL",
     }},
    {ElementType::LayoutParameterList,
     {
         R"FIDL(library x; type y = array«<uint8,4>»;)FIDL",
         R"FIDL(library x; type y = vector«<array«<uint8,4>»>»;)FIDL",
     }},
    {ElementType::OrdinaledLayoutMember,
     {
         R"FIDL(library x;
          type T = table {
            «1: intval int64»;
            «2: reserved»;
            «3: floatval float64»;
            «4: stringval string:100»;
            «5: inner union {
              «1: boolval bool»;
              «2: reserved»;
            }:optional»;
          };
         )FIDL",
     }},
    {ElementType::StructLayoutMember,
     {
         R"FIDL(library x;
          type S = struct {
            «intval int64»;
            «boolval bool = false»;
            «stringval string:100»;
            «inner struct {
              «floatval float64»;
              «uintval uint8 = 7»;
            }»;
          };
         )FIDL",
     }},
    {ElementType::ValueLayoutMember,
     {
         R"FIDL(library x;
          type E = enum {
            «A = 1»;
            «B = 2»;
          };
         )FIDL",
     }},
    {ElementType::Layout,
     {
         R"FIDL(library x;
          type B = «bits {
            A = 1;
          }»;
          type E = «strict enum {
            A = 1;
          }»;
          type S = «resource struct {
            intval int64;
          }»;
          type U = «flexible resource union {
            1: intval int64;
          }»:optional;
         )FIDL",
     }},
    {ElementType::InlineLayoutReference,
     {
         R"FIDL(library x;
          type S = «struct {
            intval int64;
            boolval bool = false;
            stringval string:MAX_STRING_SIZE;
            inner «union {
              1: floatval float64;
            }»:optional;
          }»;
         )FIDL",
     }},
    {ElementType::NamedLayoutReference,
     {
         R"FIDL(library x;
          type S = struct {
            intval «int64»;
            boolval «bool» = false;
            stringval «string»:MAX_STRING_SIZE;
            inner struct {
              floatval «float64»;
              uintval «uint8» = 7;
              vecval «vector»<«vector»<Foo>>;
              arrval «array»<uint8,4>;
            };
          };
         )FIDL",
     }},
    {ElementType::ParameterListNew,
     {
         R"FIDL(library x; protocol X { Method«()» -> «()»; };)FIDL",
         R"FIDL(library x; protocol X { Method«(struct {})» -> «(struct {})»; };)FIDL",
         R"FIDL(library x; protocol X { Method«(struct { a int32; b bool; })» -> «(struct { c uint8; d bool; })»; };)FIDL",
         R"FIDL(library x; protocol X { -> Event«()»; };)FIDL",
         R"FIDL(library x; protocol X { -> Event«(struct {})»; };)FIDL",
         R"FIDL(library x; protocol X { -> Event«(struct { a int32; b bool; })»; };)FIDL",
     }},
    {ElementType::TypeConstraints,
     {
         R"FIDL(library x; type y = array<uint8,4>;)FIDL",
         R"FIDL(library x; type y = vector<vector<uint8>:«16»>:«<16,optional>»;)FIDL",
         R"FIDL(library x; type y = union { 1: foo bool; }:«optional»;)FIDL",
         R"FIDL(library x; using zx; type y = zx.handle:«optional»;)FIDL",
         R"FIDL(library x; using zx; type y = zx.handle:«<VMO,zx.READ,optional>»;)FIDL",
     }},
    {ElementType::TypeConstructorNew,
     {
         R"FIDL(library x; type y = «array<uint8,4>»;)FIDL",
         R"FIDL(library x; type y = «vector<«array<Foo,4>»>»;)FIDL",
         R"FIDL(library x; type y = «string:100»;)FIDL",
         R"FIDL(library x; type y = «string:<100,optional>»;)FIDL",
         R"FIDL(library x;
          type e = «flexible enum : «uint32» {
            A = 1;
          }»;
         )FIDL",
         R"FIDL(library x;
          type S = «struct {
            intval «int64»;
            boolval «bool» = false;
            stringval «string:MAX_STRING_SIZE»;
            inner «struct {
              floatval «float64»;
              uintval «uint8» = 7;
              vecval «vector<«vector<Foo>»>»;
              arrval «array<uint8,4>»;
            }»;
          }»;
         )FIDL",
         R"FIDL(library x; protocol X { Method(«struct { a «int32»; b «bool»; }») -> («struct {}») error «uint32»; };)FIDL",
         R"FIDL(library x;
          resource_definition foo : «uint8» {
              properties {
                  rights «rights»;
              };
          };
         )FIDL",
     }},
    {ElementType::TypeDecl,
     {
         R"FIDL(library x;
          «type E = enum : int8 {
            A = 1;
          }»;
          «type S = struct {
            intval int64;
          }»;
          «type U = union {
            1: intval int64;
          }:optional»;
         )FIDL",
     }},
    {ElementType::Identifier,
     {
         R"FIDL(library «x»;
          type «MyEnum» = strict enum {
            «A» = 1;
          };
         )FIDL",
         R"FIDL(library «x»;
          type «MyStruct» = resource struct {
            «boolval» «bool»;
            «boolval» «resource»;
            «boolval» «flexible»;
            «boolval» «struct»;
          };
         )FIDL",
         R"FIDL(library «x»;
          type «MyUnion» = flexible union {
            1: «intval» «int64»;
            2: reserved;
          };
         )FIDL",
     }},
};
// --- end new syntax ---

constexpr std::string_view kPassedMsg = "\x1B[32mPassed\033[0m";
constexpr std::string_view kFailedMsg = "\x1B[31mFailed\033[0m";
constexpr std::string_view kErrorMsg = "\x1B[31mERROR:\033[0m";

void RunParseTests(const std::vector<TestCase>& cases, const std::string& insert_left_padding,
                   const std::string& insert_right_padding, fidl::utils::Syntax syntax) {
  std::cerr << '\n'
            << std::left << '\t' << "\x1B[34mWhere left padding = \"" << insert_left_padding
            << "\" and right padding = \"" << insert_right_padding << "\":\033[0m\n";

  bool all_passed = true;
  for (const auto& test_case : cases) {
    std::cerr << std::left << '\t' << std::setw(48) << element_type_str(test_case.type);
    std::vector<std::string> errors;

    for (const auto& unpadded_source : test_case.marked_sources) {
      // Insert the specified left/right padding.
      auto marked_source =
          replace_markers(unpadded_source, insert_left_padding + kMarkerLeft.data(),
                          kMarkerRight.data() + insert_right_padding, kMarkerLeft, kMarkerRight);
      auto source_with_decl_token_markers_removed =
          replace_markers(marked_source, "", "", kDeclStartTokenLeft, kDeclStartTokenRight);
      auto clean_source = remove_markers(marked_source);

      // Parse the source with markers removed
      fidl::ExperimentalFlags experimental_flags;
      if (syntax == fidl::utils::Syntax::kNew) {
        experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
      }
      TestLibrary library(clean_source, experimental_flags);
      std::unique_ptr<fidl::raw::File> ast;
      if (!library.Parse(&ast)) {
        errors.push_back("failed to parse");
        break;
      }

      // Get the expected decl_start_tokens from the marked source
      auto expected_decl_start_tokens_result =
          extract_expected_spans(marked_source, &errors, kDeclStartTokenLeft, kDeclStartTokenRight);
      // Returns an empty set when there are errors
      if (!expected_decl_start_tokens_result) {
        break;
      }
      auto expected_decl_start_tokens = expected_decl_start_tokens_result.value();

      // Get the expected spans from the marked source
      auto expected_spans_result = extract_expected_spans(source_with_decl_token_markers_removed,
                                                          &errors, kMarkerLeft, kMarkerRight);
      // Returns an empty set when there are errors
      if (!expected_spans_result) {
        break;
      }
      auto expected_spans = expected_spans_result.value();

      // Get the actual spans by walking the AST
      SourceSpanVisitor visitor(test_case.type);
      visitor.OnFile(ast);
      std::multiset<std::string> actual_spans = visitor.spans();
      std::multiset<std::string> actual_decl_start_tokens = visitor.decl_start_tokens();

      // Repeat the actual vs expected comparison twice - once for the spans,
      // and then again for the decl_start_tokens.
      auto expecteds =
          std::vector<std::multiset<std::string>>({expected_spans, expected_decl_start_tokens});
      auto actuals =
          std::vector<std::multiset<std::string>>({actual_spans, actual_decl_start_tokens});
      auto left_markers = std::vector<std::string_view>({kMarkerLeft, kDeclStartTokenLeft});
      auto right_markers = std::vector<std::string_view>({kMarkerRight, kDeclStartTokenRight});
      for (size_t i = 0; i < expecteds.size(); ++i) {
        // Report errors where the checker found unexpected spans
        // (spans in actual but not expected)
        std::multiset<std::string> actual_minus_expected;
        std::set_difference(actuals[i].begin(), actuals[i].end(), expecteds[i].begin(),
                            expecteds[i].end(),
                            std::inserter(actual_minus_expected, actual_minus_expected.begin()));
        for (const auto& span : actual_minus_expected) {
          std::stringstream error_msg;
          error_msg << "unexpected occurrence of type " << element_type_str(test_case.type) << ": "
                    << left_markers[i] << span << right_markers[i];
          errors.push_back(error_msg.str());
        }

        // Report errors if the checker failed to find expected spans
        // (spans in expected but not actual)
        std::multiset<std::string> expected_minus_actual;
        std::set_difference(expecteds[i].begin(), expecteds[i].end(), actuals[i].begin(),
                            actuals[i].end(),
                            std::inserter(expected_minus_actual, expected_minus_actual.begin()));
        for (const auto& span : expected_minus_actual) {
          std::stringstream error_msg;
          error_msg << "expected (but didn't find) " << (i == 0 ? "span" : "decl_start_token")
                    << " of type " << element_type_str(test_case.type) << ": " << left_markers[i]
                    << span << right_markers[i];
          errors.push_back(error_msg.str());
        }
      }
    }

    if (errors.empty()) {
      std::cerr << kPassedMsg << '\n';
    } else {
      std::cerr << kFailedMsg << '\n';
      all_passed = false;
      for (const auto& error : errors) {
        std::cerr << "\t  " << kErrorMsg << ' ' << error << '\n';
      }
    }
  }

  // Assert after all tests are over so that we can get output for each test
  // case even if one of them fails.
  ASSERT_TRUE(all_passed, "At least one test case failed");
}

TEST(SpanTests, GoodParseTest) {
  RunParseTests(new_syntax_test_cases, "", "", fidl::utils::Syntax::kNew);
  RunParseTests(new_syntax_test_cases, " ", "", fidl::utils::Syntax::kNew);
  RunParseTests(new_syntax_test_cases, "", " ", fidl::utils::Syntax::kNew);
  RunParseTests(new_syntax_test_cases, " ", " ", fidl::utils::Syntax::kNew);
}

}  // namespace
