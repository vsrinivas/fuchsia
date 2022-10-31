// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <set>
#include <stack>

#include <zxtest/zxtest.h>

#include "tools/fidl/fidlc/include/fidl/raw_ast.h"
#include "tools/fidl/fidlc/include/fidl/tree_visitor.h"
#include "tools/fidl/fidlc/tests/test_library.h"

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
  DO(BoolLiteral)               \
  DO(Ordinal64)                 \
  DO(IdentifierConstant)        \
  DO(LiteralConstant)           \
  DO(BinaryOperatorConstant)    \
  DO(Attribute)                 \
  DO(AttributeArg)              \
  DO(AttributeList)             \
  DO(TypeConstructor)           \
  DO(Library)                   \
  DO(Using)                     \
  DO(ConstDeclaration)          \
  DO(Parameter)                 \
  DO(ParameterList)             \
  DO(ProtocolCompose)           \
  DO(ProtocolMethod)            \
  DO(ProtocolDeclaration)       \
  DO(ResourceDeclaration)       \
  DO(ResourceProperty)          \
  DO(ServiceMember)             \
  DO(ServiceDeclaration)        \
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

class SourceSpanVisitor : public fidl::raw::TreeVisitor {
 public:
  explicit SourceSpanVisitor(ElementType test_case_type) : test_case_type_(test_case_type) {}

  const std::multiset<std::string>& spans() { return spans_; }

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
  void OnBoolLiteral(fidl::raw::BoolLiteral& element) override {
    CheckSpanOfType(ElementType::BoolLiteral, element);
    TreeVisitor::OnBoolLiteral(element);
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
  void OnParameterList(std::unique_ptr<fidl::raw::ParameterList> const& element) override {
    CheckSpanOfType(ElementType::ParameterListNew, *element);
    TreeVisitor::OnParameterList(element);
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
  void OnAttributeArg(std::unique_ptr<fidl::raw::AttributeArg> const& element) override {
    CheckSpanOfType(ElementType::AttributeArg, *element);
    TreeVisitor::OnAttributeArg(element);
  }
  void OnAttribute(std::unique_ptr<fidl::raw::Attribute> const& element) override {
    CheckSpanOfType(ElementType::Attribute, *element);
    TreeVisitor::OnAttribute(element);
  }
  void OnAttributeList(std::unique_ptr<fidl::raw::AttributeList> const& element) override {
    CheckSpanOfType(ElementType::AttributeList, *element);
    TreeVisitor::OnAttributeList(element);
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
  void OnTypeConstructor(std::unique_ptr<fidl::raw::TypeConstructor> const& element) override {
    CheckSpanOfType(ElementType::TypeConstructorNew, *element);
    TreeVisitor::OnTypeConstructor(element);
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

  ElementType test_case_type_;
  std::multiset<std::string> spans_;
};

std::string replace_markers(std::string_view source, std::string_view left_replace,
                            std::string_view right_replace) {
  std::string result(source);

  const auto replace_all = [&](std::string_view pattern, std::string_view replace_with) {
    std::string::size_type i = result.find(pattern);
    while (i != std::string::npos) {
      result.replace(i, pattern.length(), replace_with);
      i = result.find(pattern, i + replace_with.length());
    }
  };

  replace_all(kMarkerLeft, left_replace);
  replace_all(kMarkerRight, right_replace);
  return result;
}

std::string remove_markers(std::string_view source) { return replace_markers(source, "", ""); }

// Extracts marked source spans from a given source string.
// If source spans are incorrectly marked (missing or extra markers), returns
// empty set; otherwise, returns a multiset of expected spans.
std::multiset<std::string> extract_expected_spans(std::string_view source,
                                                  std::vector<std::string>* errors) {
  std::stack<size_t> stack;
  std::multiset<std::string> spans;

  const auto match = [&](size_t i, std::string_view marker) {
    return marker.compare(source.substr(i, marker.length())) == 0;
  };

  for (size_t i = 0; i < source.length();) {
    if (match(i, kMarkerLeft)) {
      i += kMarkerLeft.length();
      stack.push(i);
    } else if (match(i, kMarkerRight)) {
      if (stack.empty()) {
        std::stringstream error_msg;
        error_msg << "unexpected closing marker '" << kMarkerRight << "' at position " << i
                  << " in source string";
        errors->push_back(error_msg.str());
        // Return an empty set if errors
        spans.clear();
        break;
      }

      const std::string span = remove_markers(source.substr(stack.top(),  // index of left marker
                                                            i - stack.top())  // length of span
      );
      stack.pop();
      spans.insert(span);
      i += kMarkerRight.length();
    } else {
      i += 1;
    }
  }

  if (!stack.empty()) {
    std::stringstream error_msg;
    error_msg << "expected closing marker '" << kMarkerRight << "'";
    errors->push_back(error_msg.str());
    // Return an empty set if errors
    spans.clear();
  }

  return spans;
}

struct TestCase {
  ElementType type;
  std::vector<std::string> marked_sources;
};

const std::vector<TestCase> test_cases = {
    {ElementType::AttributeArg,
     {
         R"FIDL(library x; @attr(«"foo"») const MY_BOOL bool = false;)FIDL",
         R"FIDL(library x; @attr(«a="foo"»,«b="bar"») const MY_BOOL bool = false;)FIDL",
         R"FIDL(library x;
          const MY_BOOL bool = false;
          @attr(«a=true»,«b=MY_BOOL»,«c="foo"»)
          const MY_OTHER_BOOL bool = false;
         )FIDL",
     }},
    {ElementType::Attribute,
     {
         R"FIDL(library x; «@foo("foo")» «@bar» const MY_BOOL bool = false;)FIDL",
         R"FIDL(library x;
          «@foo("foo")»
          «@bar»
          const MY_BOOL bool = false;
         )FIDL",
         R"FIDL(library x;
          protocol Foo {
            Bar(«@foo» struct {});
          };
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
            «@attr 3: floatval float64»;
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
            «@attr stringval string:100»;
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
            «@attr B = 2»;
          };
         )FIDL",
         R"FIDL(library x;
          type B = bits {
            «A = 0x1»;
            «@attr B = 0x2»;
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
         R"FIDL(library x;
          protocol P {
            M(«struct {
              intval int64;
              boolval bool = false;
              stringval string:MAX_STRING_SIZE;
              inner «union {
                1: floatval float64;
              }»:optional;
            }»);
          };
         )FIDL",
         R"FIDL(library x;
          protocol Foo {
            Bar(«@foo struct {}»);
          };
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
         R"FIDL(library x; const x «int32» = 1;)FIDL",
         R"FIDL(library x; const x «zx.handle:<VMO, zx.rights.READ, optional>» = 1;)FIDL",
         R"FIDL(library x; const x «Foo<«Bar<«zx.handle:VMO»>:20»>:optional» = 1;)FIDL",
         R"FIDL(library x; const x «zx.handle:VMO» = 1;)FIDL",
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
         R"FIDL(library x;
          protocol Foo {
            Bar(«@foo struct {}»);
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

    // The following tests "duplicate" some of the auto-converted old syntax test cases above for
    // situations specific only to the new syntax.
    {ElementType::StringLiteral,
     {
         R"FIDL(library x; @attr(a=«"foo"»,b=«"bar"») const MY_BOOL bool = false;)FIDL",
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
    {ElementType::ServiceDeclaration,
     {
         R"FIDL(library x; «service X {}»;)FIDL",
         R"FIDL(library x; protocol P {}; «service X { Z client_end:P; }»;)FIDL",
     }},
    {ElementType::ServiceMember,
     {
         R"FIDL(library x; protocol P {}; service X { «Z client_end:P»; };)FIDL",
         R"FIDL(library x; protocol P {}; service X { «@attr Z client_end:P»; };)FIDL",
     }},
    {ElementType::ProtocolCompose,
     {
         R"FIDL(library x; protocol X { «compose OtherProtocol»; };)FIDL",
         R"FIDL(library x; protocol X { «@attr compose OtherProtocol»; };)FIDL",
         R"FIDL(library x; protocol X {
            «/// Foo
            compose OtherProtocol»;
          };)FIDL",
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
     library example; «resource_definition Res : uint32 { properties { subtype Enum; }; }»;)FIDL"}},
    {ElementType::ResourceProperty, {R"FIDL(
     library example; resource_definition Res : uint32 { properties { «subtype Enum»; }; };)FIDL"}},
    {ElementType::ProtocolDeclaration,
     {
         R"FIDL(library x; «protocol X {}»;)FIDL",
         R"FIDL(library x; «@attr protocol X { compose OtherProtocol; }»;)FIDL",
     }},
    {ElementType::ProtocolMethod,  // Method
     {
         R"FIDL(library x; protocol X { «Method(struct { a int32; }) -> (struct { res bool; })»; };)FIDL",
         R"FIDL(library x; protocol X { «-> Event(struct { res bool; })»; };)FIDL",
     }},
    {ElementType::ProtocolMethod,
     {
         R"FIDL(library x; protocol X { «Method()»; };)FIDL",
         R"FIDL(library x; protocol X { «@attr Method(struct { a int32; b bool; })»; };)FIDL",
         R"FIDL(library x; protocol X { «Method(struct { a int32; }) -> ()»; };)FIDL",
         R"FIDL(library x; protocol X { «Method(struct { a int32; }) -> (struct { res bool; res2 int32; })»; };)FIDL",
     }},
    {ElementType::ProtocolMethod,  // Event
     {
         R"FIDL(library x; protocol X { «-> Event()»; };)FIDL",
         R"FIDL(library x; protocol X { «@attr -> Event(struct { res bool; res2 int32; })»; };)FIDL",
     }},
    {ElementType::CompoundIdentifier,
     {
         R"FIDL(library «foo.bar.baz»;)FIDL",
     }},
    {ElementType::StringLiteral,
     {
         R"FIDL(library x; const x string = «"hello"»;)FIDL",
         R"FIDL(library x; @attr(«"foo"») const x string = «"goodbye"»;)FIDL",
     }},
    {ElementType::NumericLiteral,
     {
         R"FIDL(library x; const x uint8 = «42»;)FIDL",
         R"FIDL(library x; @attr(«42») const x uint8 = «42»;)FIDL",
     }},
    {ElementType::BoolLiteral,
     {
         R"FIDL(library x; const x bool = «true»;)FIDL",
         R"FIDL(library x; @attr(«true») const x bool = «true»;)FIDL",
         R"FIDL(library x; const x bool = «false»;)FIDL",
         R"FIDL(library x; @attr(«false») const x bool = «false»;)FIDL",
     }},
    {ElementType::Ordinal64,
     {
         R"FIDL(library x; type U = union { «1:» one uint8; };)FIDL",
     }},
    {ElementType::IdentifierConstant,
     {
         R"FIDL(library x; const x bool = true; const y bool = «x»;)FIDL",
     }},
    {ElementType::LiteralConstant,
     {
         R"FIDL(library x; const x bool = «true»;)FIDL",
         R"FIDL(library x; const x uint8 = «42»;)FIDL",
         R"FIDL(library x; const x string = «"hi"»;)FIDL",
     }},
    {ElementType::BinaryOperatorConstant,
     {
         R"FIDL(library x;
const one uint8 = 0x0001;
const two_fifty_six uint16 = 0x0100;
const two_fifty_seven uint16 = «one | two_fifty_six»;
         )FIDL",
         R"FIDL(library x; const two_fifty_seven uint16 = «0x0001 | 0x0100»;)FIDL",
     }},
    {ElementType::ConstDeclaration,
     {
         R"FIDL(library example;
«const C_SIMPLE uint32   = 11259375»;
«const C_HEX_S uint32    = 0xABCDEF»;
«const C_HEX_L uint32    = 0XABCDEF»;
«const C_BINARY_S uint32 = 0b101010111100110111101111»;
«const C_BINARY_L uint32 = 0B101010111100110111101111»;
      )FIDL"}},
};
// --- end new syntax ---

constexpr std::string_view kPassedMsg = "\x1B[32mPassed\033[0m";
constexpr std::string_view kFailedMsg = "\x1B[31mFailed\033[0m";
constexpr std::string_view kErrorMsg = "\x1B[31mERROR:\033[0m";

void RunParseTests(const std::vector<TestCase>& cases, const std::string& insert_left_padding,
                   const std::string& insert_right_padding) {
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
                          kMarkerRight.data() + insert_right_padding);
      auto clean_source = remove_markers(marked_source);

      // Parse the source with markers removed
      TestLibrary library(clean_source);
      std::unique_ptr<fidl::raw::File> ast;
      if (!library.Parse(&ast)) {
        errors.push_back("failed to parse");
        break;
      }

      // Get the expected spans from the marked source
      std::multiset<std::string> expected_spans = extract_expected_spans(marked_source, &errors);
      // Returns an empty set when there are errors
      if (expected_spans.empty()) {
        break;
      }

      // Get the actual spans by walking the AST
      SourceSpanVisitor visitor(test_case.type);
      visitor.OnFile(ast);
      std::multiset<std::string> actual_spans = visitor.spans();

      // Report errors where the checker found unexpected spans
      // (spans in actual but not expected)
      std::multiset<std::string> actual_minus_expected;
      std::set_difference(actual_spans.begin(), actual_spans.end(), expected_spans.begin(),
                          expected_spans.end(),
                          std::inserter(actual_minus_expected, actual_minus_expected.begin()));
      for (const auto& span : actual_minus_expected) {
        std::stringstream error_msg;
        error_msg << "unexpected occurrence of type " << element_type_str(test_case.type) << ": "
                  << kMarkerLeft << span << kMarkerRight;
        errors.push_back(error_msg.str());
      }

      // Report errors if the checker failed to find expected spans
      // (spans in expected but not actual)
      std::multiset<std::string> expected_minus_actual;
      std::set_difference(expected_spans.begin(), expected_spans.end(), actual_spans.begin(),
                          actual_spans.end(),
                          std::inserter(expected_minus_actual, expected_minus_actual.begin()));
      for (const auto& span : expected_minus_actual) {
        std::stringstream error_msg;
        error_msg << "expected (but didn't find) span of type " << element_type_str(test_case.type)
                  << ": " << kMarkerLeft << span << kMarkerRight;
        errors.push_back(error_msg.str());
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
  RunParseTests(test_cases, "", "");
  RunParseTests(test_cases, " ", "");
  RunParseTests(test_cases, "", " ");
  RunParseTests(test_cases, " ", " ");
}

}  // namespace
