// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <iostream>
#include <sstream>
#include <stack>
#include <set>

#include <unittest/unittest.h>

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

#define FOR_ENUM_VARIANTS(DO) \
  DO(Identifier) \
  DO(CompoundIdentifier) \
  DO(StringLiteral) \
  DO(NumericLiteral) \
  DO(TrueLiteral) \
  DO(FalseLiteral) \
  DO(Ordinal32) \
  DO(Ordinal64) \
  DO(IdentifierConstant) \
  DO(LiteralConstant) \
  DO(BinaryOperatorConstant) \
  DO(Attribute) \
  DO(AttributeList) \
  DO(TypeConstructor) \
  DO(Using) \
  DO(ConstDeclaration) \
  DO(BitsMember) \
  DO(BitsDeclaration) \
  DO(EnumMember) \
  DO(EnumDeclaration) \
  DO(Parameter) \
  DO(ParameterList) \
  DO(ProtocolMethod) \
  DO(ComposeProtocol) \
  DO(ProtocolDeclaration) \
  DO(ServiceMember) \
  DO(ServiceDeclaration) \
  DO(StructMember) \
  DO(StructDeclaration) \
  DO(TableMember) \
  DO(TableDeclaration) \
  DO(UnionMember) \
  DO(UnionDeclaration) \
  DO(XUnionMember) \
  DO(XUnionDeclaration)

#define MAKE_ENUM_VARIANT(VAR) VAR,
enum ElementType {
  FOR_ENUM_VARIANTS(MAKE_ENUM_VARIANT)
};

#define MAKE_ENUM_NAME(VAR) #VAR,
const std::string kElementTypeNames[] = {
  FOR_ENUM_VARIANTS(MAKE_ENUM_NAME)
};

std::string element_type_str(ElementType type) {
  return kElementTypeNames[type];
}

// Used to delineate spans in source code. E.g.,
// const uint32 «three» = 3;
constexpr std::string_view kMarkerLeft  = "«";
constexpr std::string_view kMarkerRight = "»";

class SourceSpanVisitor : public fidl::raw::TreeVisitor {
 public:
  SourceSpanVisitor(ElementType test_case_type)
    : test_case_type_(test_case_type) {}

  const std::multiset<std::string>& spans() {
    return spans_;
  }

  void OnIdentifier(std::unique_ptr<fidl::raw::Identifier> const& element) override {
    CheckSpanOfType(ElementType::Identifier, *element);
  }
  void OnCompoundIdentifier(std::unique_ptr<fidl::raw::CompoundIdentifier> const& element) override {
    CheckSpanOfType(ElementType::CompoundIdentifier, *element);
  }
  void OnStringLiteral(fidl::raw::StringLiteral& element) override {
    CheckSpanOfType(ElementType::StringLiteral, element);
  }
  void OnNumericLiteral(fidl::raw::NumericLiteral& element) override {
    CheckSpanOfType(ElementType::NumericLiteral, element);
  }
  void OnTrueLiteral(fidl::raw::TrueLiteral& element) override {
    CheckSpanOfType(ElementType::TrueLiteral, element);
  }
  void OnFalseLiteral(fidl::raw::FalseLiteral& element) override {
    CheckSpanOfType(ElementType::FalseLiteral, element);
  }
  void OnOrdinal32(fidl::raw::Ordinal32& element) override {
    CheckSpanOfType(ElementType::Ordinal32, element);
  }
  void OnOrdinal64(fidl::raw::Ordinal64& element) override {
    CheckSpanOfType(ElementType::Ordinal64, element);
  }
  void OnIdentifierConstant(std::unique_ptr<fidl::raw::IdentifierConstant> const& element) override {
    CheckSpanOfType(ElementType::IdentifierConstant, *element);
  }
  void OnLiteralConstant(std::unique_ptr<fidl::raw::LiteralConstant> const& element) override {
    CheckSpanOfType(ElementType::LiteralConstant, *element);
  }
  void OnBinaryOperatorConstant(std::unique_ptr<fidl::raw::BinaryOperatorConstant> const& element) override {
    CheckSpanOfType(ElementType::BinaryOperatorConstant, *element);
  }
  void OnAttribute(const fidl::raw::Attribute& element) override {
    CheckSpanOfType(ElementType::Attribute, element);
  }
  void OnAttributeList(std::unique_ptr<fidl::raw::AttributeList> const& element) override {
    CheckSpanOfType(ElementType::AttributeList, *element);
  }
  void OnTypeConstructor(std::unique_ptr<fidl::raw::TypeConstructor> const& element) override {
    CheckSpanOfType(ElementType::TypeConstructor, *element);
  }
  void OnUsing(std::unique_ptr<fidl::raw::Using> const& element) override {
    CheckSpanOfType(ElementType::Using, *element);
  }
  void OnConstDeclaration(std::unique_ptr<fidl::raw::ConstDeclaration> const& element) override {
    CheckSpanOfType(ElementType::ConstDeclaration, *element);
  }
  void OnBitsMember(std::unique_ptr<fidl::raw::BitsMember> const& element) override {
    CheckSpanOfType(ElementType::BitsMember, *element);
  }
  void OnBitsDeclaration(std::unique_ptr<fidl::raw::BitsDeclaration> const& element) override {
    CheckSpanOfType(ElementType::BitsDeclaration, *element);
  }
  void OnEnumMember(std::unique_ptr<fidl::raw::EnumMember> const& element) override {
    CheckSpanOfType(ElementType::EnumMember, *element);
  }
  void OnEnumDeclaration(std::unique_ptr<fidl::raw::EnumDeclaration> const& element) override {
    CheckSpanOfType(ElementType::EnumDeclaration, *element);
  }
  void OnParameter(std::unique_ptr<fidl::raw::Parameter> const& element) override {
    CheckSpanOfType(ElementType::Parameter, *element);
  }
  void OnParameterList(std::unique_ptr<fidl::raw::ParameterList> const& element) override {
    CheckSpanOfType(ElementType::ParameterList, *element);
  }
  void OnProtocolMethod(std::unique_ptr<fidl::raw::ProtocolMethod> const& element) override {
    CheckSpanOfType(ElementType::ProtocolMethod, *element);
  }
  void OnComposeProtocol(std::unique_ptr<fidl::raw::ComposeProtocol> const& element) override {
    CheckSpanOfType(ElementType::ComposeProtocol, *element);
  }
  void OnProtocolDeclaration(std::unique_ptr<fidl::raw::ProtocolDeclaration> const& element) override {
    CheckSpanOfType(ElementType::ProtocolDeclaration, *element);
  }
  void OnServiceMember(std::unique_ptr<fidl::raw::ServiceMember> const& element) override {
    CheckSpanOfType(ElementType::ServiceMember, *element);
  }
  void OnServiceDeclaration(std::unique_ptr<fidl::raw::ServiceDeclaration> const& element) override {
    CheckSpanOfType(ElementType::ServiceDeclaration, *element);
  }
  void OnStructMember(std::unique_ptr<fidl::raw::StructMember> const& element) override {
    CheckSpanOfType(ElementType::StructMember, *element);
  }
  void OnStructDeclaration(std::unique_ptr<fidl::raw::StructDeclaration> const& element) override {
    CheckSpanOfType(ElementType::StructDeclaration, *element);
  }
  void OnTableMember(std::unique_ptr<fidl::raw::TableMember> const& element) override {
    CheckSpanOfType(ElementType::TableMember, *element);
  }
  void OnTableDeclaration(std::unique_ptr<fidl::raw::TableDeclaration> const& element) override {
    CheckSpanOfType(ElementType::TableDeclaration, *element);
  }
  void OnUnionMember(std::unique_ptr<fidl::raw::UnionMember> const& element) override {
    CheckSpanOfType(ElementType::UnionMember, *element);
  }
  void OnUnionDeclaration(std::unique_ptr<fidl::raw::UnionDeclaration> const& element) override {
    CheckSpanOfType(ElementType::UnionDeclaration, *element);
  }
  void OnXUnionMember(std::unique_ptr<fidl::raw::XUnionMember> const& element) override {
    CheckSpanOfType(ElementType::XUnionMember, *element);
  }
  void OnXUnionDeclaration(std::unique_ptr<fidl::raw::XUnionDeclaration> const& element) override {
    CheckSpanOfType(ElementType::XUnionDeclaration, *element);
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

std::string remove_markers(const std::string& source) {
  std::string result(source);

  const auto remove_all = [&](std::string_view pattern) {
    std::string::size_type i = result.find(pattern);
    while (i != std::string::npos) {
      result.erase(i, pattern.length());
      i = result.find(pattern, i);
    }
  };

  remove_all(kMarkerLeft);
  remove_all(kMarkerRight);
  return result;
}

// Extracts marked source spans from a given source string.
// If source spans are incorrectly marked (missing or extra markers), returns an
// empty set; otherwise, returns a multiset of expected spans.
std::multiset<std::string> extract_expected_spans(const std::string& source, std::vector<std::string>* errors) {
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
        error_msg << "unexpected closing marker '" << kMarkerRight
                  << "' at position " << i << " in source string";
        errors->push_back(error_msg.str());
        // Return an empty set if errors
        spans.clear();
        break;
      }

      const std::string span = source.substr(
        stack.top(), // index of left marker
        i - stack.top() // length of span
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
  {
    ElementType::ConstDeclaration,
    {
      R"FIDL(
library example;

«const uint32 C_SIMPLE   = 11259375»;
«const uint32 C_HEX_S    = 0xABCDEF»;
«const uint32 C_HEX_L    = 0XABCDEF»;
«const uint32 C_BINARY_S = 0b101010111100110111101111»;
«const uint32 C_BINARY_L = 0B101010111100110111101111»;
      )FIDL"
    }
  },
  {
    ElementType::EnumDeclaration,
    {
      R"FIDL(
library example;

«enum TestEnum {
  A = 1;
  B = 2;
}»;
      )FIDL"
    }
  },
  {
    ElementType::BitsDeclaration,
    {
      R"FIDL(
library example;

«bits TestBits {
  A = 1;
  B = 2;
}»;
      )FIDL"
    }
  },
};

constexpr std::string_view kPassedMsg = "\x1B[32mPassed\033[0m";
constexpr std::string_view kFailedMsg = "\x1B[31mFailed\033[0m";
constexpr std::string_view kErrorMsg = "\x1B[31mERROR:\033[0m";

bool parse_test() {
  BEGIN_TEST;
  std::cerr << '\n';

  bool all_passed = true;
  for (const auto& test_case : test_cases) {
    std::cerr << "\tTest case for type " << element_type_str(test_case.type) << "...";
    std::vector<std::string> errors;

    for (const auto& marked_source : test_case.marked_sources) {
      // Parse the source with markers removed
      TestLibrary library(remove_markers(marked_source));
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
      std::set_difference(actual_spans.begin(), actual_spans.end(),
                          expected_spans.begin(), expected_spans.end(),
                          std::inserter(actual_minus_expected, actual_minus_expected.begin()));
      for (const auto& span : actual_minus_expected) {
        std::stringstream error_msg;
        error_msg << "unexpected occurrence of type "
                  << element_type_str(test_case.type) << ": "
                  << kMarkerLeft << span << kMarkerRight;
        errors.push_back(error_msg.str());
      }

      // Report errors if the checker failed to find expected spans
      // (spans in expected but not actual)
      std::multiset<std::string> expected_minus_actual;
      std::set_difference(expected_spans.begin(), expected_spans.end(),
                          actual_spans.begin(), actual_spans.end(),
                          std::inserter(expected_minus_actual, expected_minus_actual.begin()));
      for (const auto& span : expected_minus_actual) {
        std::stringstream error_msg;
        error_msg << "expected (but didn't find) span of type "
                  << element_type_str(test_case.type) << ": "
                  << kMarkerLeft << span << kMarkerRight;
        errors.push_back(error_msg.str());
      }
    }

    if (errors.empty()) {
      std::cerr << '\t' << kPassedMsg << '\n';
    } else {
      std::cerr << '\t' << kFailedMsg << '\n';
      all_passed = false;
      for (const auto& error : errors) {
        std::cerr << "\t  " << kErrorMsg << ' ' << error << '\n';
      }
    }
  }

  // Assert after all tests are over so that we can get output for each test
  // case even if one of them fails.
  ASSERT_TRUE(all_passed, "At least one test case failed");

  END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(span_tests)

RUN_TEST(parse_test)

END_TEST_CASE(span_tests)
