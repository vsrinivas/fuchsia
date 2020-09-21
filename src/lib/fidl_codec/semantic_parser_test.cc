// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fidl_codec/semantic_parser.h"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "src/lib/fidl_codec/library_loader.h"
#include "src/lib/fidl_codec/list_test_data.h"
#include "src/lib/fidl_codec/semantic_parser_test.h"

namespace fidl_codec {
namespace semantic {

// Checks the semantic parser.
// Checks that we detects errors.
// Checks that we do a good recovery on errors (only a few tests display more than one error).

SemanticParserTest::SemanticParserTest() {
  fidl_codec_test::SdkExamples sdk_examples;

  // Loads all the files in sdk/core.fidl_json.txt.
  for (const auto& element : sdk_examples.map()) {
    library_loader_.AddContent(element.second, &err_);
  }
}

void SemanticParserTest::SetUp() {}

TEST_F(SemanticParserTest, GlobalExample) {
  // Checks that Directory::Open exists in fuchsia.io.
  Library* library = library_loader_.GetLibraryFromName("fuchsia.io");
  ASSERT_NE(library, nullptr);
  library->DecodeTypes();
  Interface* interface = nullptr;
  library->GetInterfaceByName("fuchsia.io/Directory", &interface);
  ASSERT_NE(interface, nullptr);
  InterfaceMethod* method = interface->GetMethodByName("Open");
  ASSERT_NE(method, nullptr);
  // Checks that we currently don't have any semantic for Open.
  ASSERT_EQ(method->semantic(), nullptr);

  std::string text =
      "library fuchsia.io {\n"
      "  Directory::Open {\n"
      "    request.object = handle / request.path;\n"
      "  }\n"
      "}\n"
      "library fuchsia.sys {\n"
      "  Launcher::CreateComponent {\n"
      "   request.controller = HandleDescription('server-control', request.launch_info.url);\n"
      "  }\n"
      "}\n";
  ParserErrors parser_errors;
  SemanticParser parser(&library_loader_, text, &parser_errors);
  parser.ParseSemantic();

  // Checks that we now have the right semantic.
  ASSERT_NE(method->semantic(), nullptr);
  std::stringstream ss;
  method->semantic()->Dump(ss);
  std::string result = ss.str();
  ASSERT_EQ(result, "request.object = handle / request.path\n");
}

TEST_F(SemanticParserTest, CheckAssignments) {
  std::string text =
      "request.object = handle / request.path;\n"
      "request.foo = handle;\n"
      "request.bar = handle / request.other_path;\n"
      "request.bar2 = handle : 'cloned';\n";
  ParserErrors parser_errors;
  SemanticParser parser(&library_loader_, text, &parser_errors);
  MethodSemantic semantic;
  while (!parser.IsEof()) {
    parser.ParseAssignment(&semantic);
  }

  std::stringstream ss;
  semantic.Dump(ss);
  std::string result = ss.str();
  ASSERT_EQ(result,
            "request.object = handle / request.path\n"
            "request.foo = handle\n"
            "request.bar = handle / request.other_path\n"
            "request.bar2 = handle : 'cloned'\n");
}

TEST_F(SemanticParserTest, CheckDisplay) {
  std::string text =
      "  input_field: request.path;\n"
      "  result: request.object;\n"
      "  input_field: request.data.size ' bytes';\n"
      "  input_field: 'buffer of ' request.data.size ' bytes';\n"
      "  input_field: 'size = ' request.data.size;\n"
      "}\n";
  InterfaceMethod method;
  ParserErrors parser_errors;
  SemanticParser parser(&library_loader_, text, &parser_errors);
  while (!parser.IsEof()) {
    parser.ParseMethod(&method);
  }

  ASSERT_NE(method.short_display(), nullptr);

  std::stringstream ss;
  method.short_display()->Dump(ss);
  std::string result = ss.str();
  ASSERT_EQ(result,
            "input_field: request.path;\n"
            "input_field: request.data.size \" bytes\";\n"
            "input_field: \"buffer of \" request.data.size \" bytes\";\n"
            "input_field: \"size = \" request.data.size;\n"
            "result: request.object;\n");
}

TEST_F(SemanticParserTest, EmptyText) {
  std::string text = "";
  std::stringstream error_stream;
  ParserErrors parser_errors(error_stream);
  SemanticParser parser(&library_loader_, text, &parser_errors);
  MethodSemantic semantic;
  parser.ParseSemantic();

  std::string result = error_stream.str();
  ASSERT_EQ(result, "");
}

TEST_F(SemanticParserTest, LibraryExpected) {
  std::string text =
      "xxx fuchsia.io {\n"
      "  Directory::Open {\n"
      "    request.object = handle / request.path;\n"
      "  }\n"
      "}\n";
  std::stringstream error_stream;
  ParserErrors parser_errors(error_stream);
  SemanticParser parser(&library_loader_, text, &parser_errors);
  parser.ParseSemantic();

  std::string result = error_stream.str();
  ASSERT_EQ(result,
            "xxx fuchsia.io {\n"
            "^\n"
            "1:1: Keyword 'library' expected.\n");
}

TEST_F(SemanticParserTest, LibraryNameExpected) {
  std::string text =
      "library {\n"
      "  Directory::Open {\n"
      "    request.object = handle / request.path;\n"
      "  }\n"
      "}\n";
  std::stringstream error_stream;
  ParserErrors parser_errors(error_stream);
  SemanticParser parser(&library_loader_, text, &parser_errors);
  parser.ParseSemantic();

  std::string result = error_stream.str();
  ASSERT_EQ(result,
            "library {\n"
            "        ^\n"
            "1:9: Library name expected.\n");
}

TEST_F(SemanticParserTest, LibraryNotFound) {
  std::string text =
      "library fuchsia.xxx {\n"
      "  Directory::Open {\n"
      "    request.object = handle / request.path;\n"
      "  }\n"
      "}\n";
  std::stringstream error_stream;
  ParserErrors parser_errors(error_stream);
  SemanticParser parser(&library_loader_, text, &parser_errors);
  parser.ParseSemantic();

  std::string result = error_stream.str();
  ASSERT_EQ(result,
            "library fuchsia.xxx {\n"
            "        ^\n"
            "1:9: Library fuchsia.xxx not found.\n");
}

TEST_F(SemanticParserTest, MissingLeftBrace1) {
  std::string text =
      "library fuchsia.io\n"
      "  Directory::Open {\n"
      "    request.object = handle / request.path;\n"
      "  }\n"
      "}\n";
  std::stringstream error_stream;
  ParserErrors parser_errors(error_stream);
  SemanticParser parser(&library_loader_, text, &parser_errors);
  parser.ParseSemantic();

  std::string result = error_stream.str();
  ASSERT_EQ(result,
            "  Directory::Open {\n"
            "  ^\n"
            "2:3: Symbol '{' expected.\n");
}

TEST_F(SemanticParserTest, ProtocolNameExpected) {
  std::string text =
      "library fuchsia.io {\n"
      "  ::Open {\n"
      "    request.object = handle / request.path;\n"
      "  }\n"
      "}\n";
  std::stringstream error_stream;
  ParserErrors parser_errors(error_stream);
  SemanticParser parser(&library_loader_, text, &parser_errors);
  parser.ParseSemantic();

  std::string result = error_stream.str();
  ASSERT_EQ(result,
            "  ::Open {\n"
            "  ^\n"
            "2:3: Protocol name expected.\n");
}

TEST_F(SemanticParserTest, ProtocolNotFound) {
  std::string text =
      "library fuchsia.io {\n"
      "  Xxx::Open {\n"
      "    request.object = handle / request.path;\n"
      "  }\n"
      "}\n";
  std::stringstream error_stream;
  ParserErrors parser_errors(error_stream);
  SemanticParser parser(&library_loader_, text, &parser_errors);
  parser.ParseSemantic();

  std::string result = error_stream.str();
  ASSERT_EQ(result,
            "  Xxx::Open {\n"
            "  ^\n"
            "2:3: Protocol Xxx not found in library fuchsia.io\n");
}

TEST_F(SemanticParserTest, DoubleColonExpected) {
  std::string text =
      "library fuchsia.io {\n"
      "  Directory Open {\n"
      "    request.object = handle / request.path;\n"
      "  }\n"
      "}\n";
  std::stringstream error_stream;
  ParserErrors parser_errors(error_stream);
  SemanticParser parser(&library_loader_, text, &parser_errors);
  parser.ParseSemantic();

  std::string result = error_stream.str();
  ASSERT_EQ(result,
            "  Directory Open {\n"
            "            ^\n"
            "2:13: Symbol '::' expected.\n");
}

TEST_F(SemanticParserTest, MethodNameExpected) {
  std::string text =
      "library fuchsia.io {\n"
      "  Directory:: {\n"
      "    request.object = handle / request.path;\n"
      "  }\n"
      "}\n";
  std::stringstream error_stream;
  ParserErrors parser_errors(error_stream);
  SemanticParser parser(&library_loader_, text, &parser_errors);
  parser.ParseSemantic();

  std::string result = error_stream.str();
  ASSERT_EQ(result,
            "  Directory:: {\n"
            "              ^\n"
            "2:15: Method name expected.\n");
}

TEST_F(SemanticParserTest, MethodNotFound) {
  std::string text =
      "library fuchsia.io {\n"
      "  Directory::Xxx {\n"
      "    request.object = handle / request.path;\n"
      "  }\n"
      "}\n";
  std::stringstream error_stream;
  ParserErrors parser_errors(error_stream);
  SemanticParser parser(&library_loader_, text, &parser_errors);
  parser.ParseSemantic();

  std::string result = error_stream.str();
  ASSERT_EQ(result,
            "  Directory::Xxx {\n"
            "             ^\n"
            "2:14: Method Xxx not found in protocol fuchsia.io/Directory\n");
}

TEST_F(SemanticParserTest, MissingLeftBrace2) {
  std::string text =
      "library fuchsia.io {\n"
      "  Directory::Open\n"
      "    request.object = handle / request.path;\n"
      "  }\n"
      "}\n";
  std::stringstream error_stream;
  ParserErrors parser_errors(error_stream);
  SemanticParser parser(&library_loader_, text, &parser_errors);
  parser.ParseSemantic();

  std::string result = error_stream.str();
  ASSERT_EQ(result,
            "    request.object = handle / request.path;\n"
            "    ^\n"
            "3:5: Symbol '{' expected.\n"
            "}\n"
            "^\n"
            "5:1: Keyword 'library' expected.\n");
}

TEST_F(SemanticParserTest, InputFieldColonExpected) {
  std::string text =
      "library fuchsia.io {\n"
      "  Directory::Open {\n"
      "    input_field request.path;\n"
      "    result: request.object;\n"
      "  }\n"
      "}\n";
  std::stringstream error_stream;
  ParserErrors parser_errors(error_stream);
  SemanticParser parser(&library_loader_, text, &parser_errors);
  parser.ParseSemantic();

  std::string result = error_stream.str();
  ASSERT_EQ(result,
            "    input_field request.path;\n"
            "                ^\n"
            "3:17: Symbol ':' expected.\n");
}

TEST_F(SemanticParserTest, ResultColonExpected) {
  std::string text =
      "library fuchsia.io {\n"
      "  Directory::Open {\n"
      "    input_field: request.path;\n"
      "    result request.object;\n"
      "  }\n"
      "}\n";
  std::stringstream error_stream;
  ParserErrors parser_errors(error_stream);
  SemanticParser parser(&library_loader_, text, &parser_errors);
  parser.ParseSemantic();

  std::string result = error_stream.str();
  ASSERT_EQ(result,
            "    result request.object;\n"
            "           ^\n"
            "4:12: Symbol ':' expected.\n");
}

TEST_F(SemanticParserTest, InputFieldSemiColonExpected) {
  std::string text =
      "library fuchsia.io {\n"
      "  Directory::Open {\n"
      "    input_field: request.path\n"
      "    result: request.object;\n"
      "  }\n"
      "}\n";
  std::stringstream error_stream;
  ParserErrors parser_errors(error_stream);
  SemanticParser parser(&library_loader_, text, &parser_errors);
  parser.ParseSemantic();

  std::string result = error_stream.str();
  ASSERT_EQ(result,
            "    result: request.object;\n"
            "    ^\n"
            "4:5: Symbol ';' expected.\n");
}

TEST_F(SemanticParserTest, ResultSemiColonExpected) {
  std::string text =
      "library fuchsia.io {\n"
      "  Directory::Open {\n"
      "    input_field: request.path;\n"
      "    result: request.object\n"
      "  }\n"
      "}\n";
  std::stringstream error_stream;
  ParserErrors parser_errors(error_stream);
  SemanticParser parser(&library_loader_, text, &parser_errors);
  parser.ParseSemantic();

  std::string result = error_stream.str();
  ASSERT_EQ(result,
            "  }\n"
            "  ^\n"
            "5:3: Symbol ';' expected.\n");
}

TEST_F(SemanticParserTest, AssignmentExpected) {
  std::string text =
      "library fuchsia.io {\n"
      "  Directory::Open {\n"
      "    = handle / request.path;\n"
      "  }\n"
      "}\n";
  std::stringstream error_stream;
  ParserErrors parser_errors(error_stream);
  SemanticParser parser(&library_loader_, text, &parser_errors);
  parser.ParseSemantic();

  std::string result = error_stream.str();
  ASSERT_EQ(result,
            "    = handle / request.path;\n"
            "    ^\n"
            "3:5: Assignment expected.\n");
}

TEST_F(SemanticParserTest, FieldNameExpected) {
  std::string text =
      "library fuchsia.io {\n"
      "  Directory::Open {\n"
      "    request. = handle / request.path;\n"
      "  }\n"
      "}\n";
  std::stringstream error_stream;
  ParserErrors parser_errors(error_stream);
  SemanticParser parser(&library_loader_, text, &parser_errors);
  parser.ParseSemantic();

  std::string result = error_stream.str();
  ASSERT_EQ(result,
            "    request. = handle / request.path;\n"
            "             ^\n"
            "3:14: Field name expected.\n");
}

TEST_F(SemanticParserTest, EqualExpected) {
  std::string text =
      "library fuchsia.io {\n"
      "  Directory::Open {\n"
      "    request.object handle / request.path;\n"
      "  }\n"
      "}\n";
  std::stringstream error_stream;
  ParserErrors parser_errors(error_stream);
  SemanticParser parser(&library_loader_, text, &parser_errors);
  parser.ParseSemantic();

  std::string result = error_stream.str();
  ASSERT_EQ(result,
            "    request.object handle / request.path;\n"
            "                   ^\n"
            "3:20: Symbol '=' expected.\n");
}

TEST_F(SemanticParserTest, ExpressionExpected1) {
  std::string text =
      "library fuchsia.io {\n"
      "  Directory::Open {\n"
      "    request.object =;\n"
      "  }\n"
      "}\n";
  std::stringstream error_stream;
  ParserErrors parser_errors(error_stream);
  SemanticParser parser(&library_loader_, text, &parser_errors);
  parser.ParseSemantic();

  std::string result = error_stream.str();
  ASSERT_EQ(result,
            "    request.object =;\n"
            "                    ^\n"
            "3:21: Expression expected.\n");
}

TEST_F(SemanticParserTest, ExpressionExpected2) {
  std::string text =
      "library fuchsia.io {\n"
      "  Directory::Open {\n"
      "    request.object = handle /;\n"
      "  }\n"
      "}\n";
  std::stringstream error_stream;
  ParserErrors parser_errors(error_stream);
  SemanticParser parser(&library_loader_, text, &parser_errors);
  parser.ParseSemantic();

  std::string result = error_stream.str();
  ASSERT_EQ(result,
            "    request.object = handle /;\n"
            "                             ^\n"
            "3:30: Expression expected.\n");
}

TEST_F(SemanticParserTest, ExpressionExpected3) {
  std::string text =
      "library fuchsia.io {\n"
      "  Directory::Open {\n"
      "    request.object = xxx;\n"
      "  }\n"
      "}\n";
  std::stringstream error_stream;
  ParserErrors parser_errors(error_stream);
  SemanticParser parser(&library_loader_, text, &parser_errors);
  parser.ParseSemantic();

  std::string result = error_stream.str();
  ASSERT_EQ(result,
            "    request.object = xxx;\n"
            "                     ^\n"
            "3:22: Expression expected.\n");
}

TEST_F(SemanticParserTest, SemicolonExpected) {
  std::string text =
      "library fuchsia.io {\n"
      "  Directory::Open {\n"
      "    request.object = handle / request.path\n"
      "  }\n"
      "}\n";
  std::stringstream error_stream;
  ParserErrors parser_errors(error_stream);
  SemanticParser parser(&library_loader_, text, &parser_errors);
  parser.ParseSemantic();

  std::string result = error_stream.str();
  ASSERT_EQ(result,
            "  }\n"
            "  ^\n"
            "4:3: Symbol ';' expected.\n");
}

TEST_F(SemanticParserTest, HandleDescriptionTypo) {
  std::string text =
      "library fuchsia.sys {\n"
      "  Launcher::CreateComponent {\n"
      "   request.controller = HandleDescriptions('server-control', request.launch_info.url);\n"
      "  }\n"
      "}\n";
  std::stringstream error_stream;
  ParserErrors parser_errors(error_stream);
  SemanticParser parser(&library_loader_, text, &parser_errors);
  parser.ParseSemantic();

  std::string result = error_stream.str();
  ASSERT_EQ(
      result,
      "   request.controller = HandleDescriptions('server-control', request.launch_info.url);\n"
      "                        ^\n"
      "3:25: Expression expected.\n");
}

TEST_F(SemanticParserTest, UnterminatedString) {
  std::string text =
      "library fuchsia.sys {\n"
      "  Launcher::CreateComponent {\n"
      "   request.controller = HandleDescription('server-control, request.launch_info.url);\n"
      "  }\n"
      "}\n";
  std::stringstream error_stream;
  ParserErrors parser_errors(error_stream);
  SemanticParser parser(&library_loader_, text, &parser_errors);
  parser.ParseSemantic();

  std::string result = error_stream.str();
  ASSERT_EQ(result,
            "   request.controller = HandleDescription('server-control, request.launch_info.url);\n"
            "                                          ^\n3:43: Unterminated string.\n"
            "   request.controller = HandleDescription('server-control, request.launch_info.url);\n"
            "                                           ^\n3:44: Symbol ',' expected.\n");
}

TEST_F(SemanticParserTest, LeftParenthesisExpected) {
  std::string text =
      "library fuchsia.sys {\n"
      "  Launcher::CreateComponent {\n"
      "   request.controller = HandleDescription 'server-control', request.launch_info.url);\n"
      "  }\n"
      "}\n";
  std::stringstream error_stream;
  ParserErrors parser_errors(error_stream);
  SemanticParser parser(&library_loader_, text, &parser_errors);
  parser.ParseSemantic();

  std::string result = error_stream.str();
  ASSERT_EQ(
      result,
      "   request.controller = HandleDescription 'server-control', request.launch_info.url);\n"
      "                                          ^\n"
      "3:43: Symbol '(' expected.\n");
}

TEST_F(SemanticParserTest, CommaExpected) {
  std::string text =
      "library fuchsia.sys {\n"
      "  Launcher::CreateComponent {\n"
      "   request.controller = HandleDescription('server-control' request.launch_info.url);\n"
      "  }\n"
      "}\n";
  std::stringstream error_stream;
  ParserErrors parser_errors(error_stream);
  SemanticParser parser(&library_loader_, text, &parser_errors);
  parser.ParseSemantic();

  std::string result = error_stream.str();
  ASSERT_EQ(result,
            "   request.controller = HandleDescription('server-control' request.launch_info.url);\n"
            "                                                           ^\n"
            "3:60: Symbol ',' expected.\n");
}

TEST_F(SemanticParserTest, RightParenthesisExpected) {
  std::string text =
      "library fuchsia.sys {\n"
      "  Launcher::CreateComponent {\n"
      "   request.controller = HandleDescription('server-control', request.launch_info.url;\n"
      "  }\n"
      "}\n";
  std::stringstream error_stream;
  ParserErrors parser_errors(error_stream);
  SemanticParser parser(&library_loader_, text, &parser_errors);
  parser.ParseSemantic();

  std::string result = error_stream.str();
  ASSERT_EQ(result,
            "   request.controller = HandleDescription('server-control', request.launch_info.url;\n"
            "                                                                                   ^\n"
            "3:84: Symbol ')' expected.\n");
}

}  // namespace semantic
}  // namespace fidl_codec
