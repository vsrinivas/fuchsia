// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';
import 'dart:convert';

import 'package:detect_api_changes/analyze.dart';
import 'package:detect_api_changes/diff.dart';
import 'package:path/path.dart' as p;
import 'package:test/test.dart';

// TODO(fxbug.dev/6541): Parse the JSON file into an intermediate format, and check a few
// pieces of the data instead of verifying a large block of text.

void main() async {
  test('Public elements are included in the tree', () async {
    var sourceAPI = await analyze("""
      int examplePublicFileVariable = 3;
      void examplePublicFileFunction() {};
      enum PublicEnum {
        apple, banana
      }

      class ExampleClass implements Cat, Bird, Horse {
        enum AnotherPublicEnum {
          peach, orange
        }
        String examplePublicField = "testing";
        void examplePublicMethod() {}
      }
      """);

    var expectedAPI = '{\n'
        '  "files": {\n'
        '    "example.dart": {\n'
        '      "ClassDeclarationImpl": {\n'
        '        "ExampleClass": {\n'
        '          "ImplementsClause": {\n'
        '            "Bird": {\n'
        '              "name": "Bird",\n'
        '              "type": "interface"\n'
        '            },\n'
        '            "Cat": {\n'
        '              "name": "Cat",\n'
        '              "type": "interface"\n'
        '            },\n'
        '            "Horse": {\n'
        '              "name": "Horse",\n'
        '              "type": "interface"\n'
        '            }\n'
        '          },\n'
        '          "MethodDeclarationImpl": {\n'
        '            "examplePublicMethod": {\n'
        '              "isAbstract": false,\n'
        '              "isGetter": false,\n'
        '              "isOperator": false,\n'
        '              "isSetter": false,\n'
        '              "isStatic": false,\n'
        '              "name": "examplePublicMethod",\n'
        '              "returnType": "void",\n'
        '              "type": "MethodDeclarationImpl"\n'
        '            }\n'
        '          },\n'
        '          "VariableDeclarationImpl": {\n'
        '            "examplePublicField": {\n'
        '              "isConst": false,\n'
        '              "isFinal": false,\n'
        '              "isLate": false,\n'
        '              "name": "examplePublicField",\n'
        '              "type": "VariableDeclarationImpl",\n'
        '              "varType": "String"\n'
        '            }\n'
        '          },\n'
        '          "name": "ExampleClass",\n'
        '          "type": "ClassDeclarationImpl"\n'
        '        }\n'
        '      },\n'
        '      "EnumDeclarationImpl": {\n'
        '        "PublicEnum": {\n'
        '          "EnumConstantDeclarationImpl": {\n'
        '            "apple": {\n'
        '              "name": "apple",\n'
        '              "type": "EnumConstantDeclarationImpl"\n'
        '            },\n'
        '            "banana": {\n'
        '              "name": "banana",\n'
        '              "type": "EnumConstantDeclarationImpl"\n'
        '            }\n'
        '          },\n'
        '          "name": "PublicEnum",\n'
        '          "type": "EnumDeclarationImpl"\n'
        '        }\n'
        '      },\n'
        '      "FunctionDeclarationImpl": {\n'
        '        "examplePublicFileFunction": {\n'
        '          "isGetter": false,\n'
        '          "isSetter": false,\n'
        '          "name": "examplePublicFileFunction",\n'
        '          "returnType": "void",\n'
        '          "type": "FunctionDeclarationImpl"\n'
        '        }\n'
        '      },\n'
        '      "VariableDeclarationImpl": {\n'
        '        "examplePublicFileVariable": {\n'
        '          "isConst": false,\n'
        '          "isFinal": false,\n'
        '          "isLate": false,\n'
        '          "name": "examplePublicFileVariable",\n'
        '          "type": "VariableDeclarationImpl",\n'
        '          "varType": "int"\n'
        '        }\n'
        '      },\n'
        '      "name": "example.dart",\n'
        '      "type": "file"\n'
        '    }\n'
        '  },\n'
        '  "name": "test.dart",\n'
        '  "type": "package"\n'
        '}';

    expect(sourceAPI, expectedAPI);
  });

  test('Private elements are not included in the tree', () async {
    var sourceAPI = await analyze("""
      int _examplePrivateFileVariable = 3;
      void _examplePrivateFileFunction() {};
      enum _PrivateEnum {
        apple, banana
      }

      class ExampleClass {
        enum _AnotherPrivateEnum {
          peach, orange
        }
        String _examplePrivateField = "testing";
        void _examplePrivateMethod() {}
      }

      class _ExamplePrivateClass {
        enum PublicEnum {
          pear, pineapple
        }
        String examplePublicField = "testing";
        void examplePublicMethod() {}
      }
      """);

    var expectedAPI = '{\n'
        '  "files": {\n'
        '    "example.dart": {\n'
        '      "ClassDeclarationImpl": {\n'
        '        "ExampleClass": {\n'
        '          "name": "ExampleClass",\n'
        '          "type": "ClassDeclarationImpl"\n'
        '        }\n'
        '      },\n'
        '      "name": "example.dart",\n'
        '      "type": "file"\n'
        '    }\n'
        '  },\n'
        '  "name": "test.dart",\n'
        '  "type": "package"\n'
        '}';

    expect(sourceAPI, expectedAPI);
  });
}

Future<String> analyze(String source) async {
  JsonEncoder encoder = new JsonEncoder.withIndent('  ');
  String path = (await Directory.systemTemp.createTemp()).path;
  File file = new File(p.join(path, "example.dart"));
  await file.writeAsString(source);
  return await analyzeAPI("test.dart", [path]);
}
