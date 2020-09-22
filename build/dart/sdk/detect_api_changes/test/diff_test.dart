// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';
import 'dart:convert';

import 'package:detect_api_changes/src/analyze.dart';
import 'package:detect_api_changes/src/diff.dart';
import 'package:path/path.dart' as p;
import 'package:test/test.dart';

// TODO(fxbug.dev/6541): Currently, API changes are detected using simple string comparison. In
// the future we want to be able to provide detailed error messages. i.e.
//   "Method 'foo' used to have return type 'void', now has return type 'int'"
// These tests are skipped until that functionality is implemented.

void main() {
  test('New API is Empty', () async {
    var sourceapi = await setup("""""");
    var goldenapi = await setup("""
      class exampleclass {
        string examplepublicfield = "testing";
      }
      """);
    String result = await diffTwoFiles(sourceapi, goldenapi);
    expect("TODO", result, skip: "Re-enable when full diffing is supported");
  });

  test('Golden API is Empty', () async {
    var sourceAPI = await setup("""
      class ExampleClass {
        String examplePublicField = "testing";
      }
      """);
    var goldenAPI = await setup("""""");
    String result = await diffTwoFiles(sourceAPI, goldenAPI);
    expect("TODO", result, skip: "Re-enable when full diffing is supported");
  });

  test('Extends statement added to existing public class', () async {
    var sourceAPI = await setup("""
      class ExampleClass1 {
        String examplePublicField = "testing";
      }
      class ExampleClass2 {
        String examplePublicField2 = "testing";
      }
      """);
    var goldenAPI = await setup("""
      class ExampleClass1 {
        String examplePublicField = "testing";
      }
      class ExampleClass2 extends ExampleClass1{
        String examplePublicField2 = "testing";
      }
        """);
    String result = await diffTwoFiles(sourceAPI, goldenAPI);
    expect("TODO", result, skip: "Re-enable when full diffing is supported");
  });

  test('Implements statement added to existing public class', () async {
    var sourceAPI = await setup("""
      class ExampleClass1 {
        String examplePublicField = "testing";
      }
      class ExampleClass2 {
        String examplePublicField2 = "testing";
      }
      """);
    var goldenAPI = await setup("""
      class ExampleClass1 {
        String examplePublicField = "testing";
      }
      class ExampleClass2 implements ExampleClass1{
        String examplePublicField2 = "testing";
      }
        """);
    String result = await diffTwoFiles(sourceAPI, goldenAPI);
    expect("TODO", result, skip: "Re-enable when full diffing is supported");
  });

  test('Mixin added to existing public class', () async {
    var sourceAPI = await setup("""
      class ExampleClass1 {
        String examplePublicField = "testing";
      }
      class ExampleClass2 {
        String examplePublicField2 = "testing";
      }
      """);
    var goldenAPI = await setup("""
      class ExampleClass1 {
        String examplePublicField = "testing";
      }
      class ExampleClass2 with ExampleClass1{
        String examplePublicField2 = "testing";
      }
        """);
    String result = await diffTwoFiles(sourceAPI, goldenAPI);
    expect("TODO", result, skip: "Re-enable when full diffing is supported");
  });

  test('Variable count in parameter list changes for public method', () async {
    var sourceAPI = await setup("""
        int publicMethod(String one, String two){};
      """);
    var goldenAPI = await setup("""
        int publicMethod(String one, String two, String three){};
      """);
    String result = await diffTwoFiles(sourceAPI, goldenAPI);
    expect("TODO", result, skip: "Re-enable when full diffing is supported");
  });

  test('Variable type in parameter list changes for public method', () async {
    var sourceAPI = await setup("""
        int publicMethod(String one, String two){return 1;};
      """);
    var goldenAPI = await setup("""
        int publicMethod(String one, int two){return 1;};
      """);
    String result = await diffTwoFiles(sourceAPI, goldenAPI);
    expect("TODO", result, skip: "Re-enable when full diffing is supported");
  });

  test('Type changes for public var', () async {
    var sourceAPI = await setup("""
        int publicVar = 1234;
      """);
    var goldenAPI = await setup("""
        String publicVar = "asdf";
      """);
    String result = await diffTwoFiles(sourceAPI, goldenAPI);
    expect("TODO", result, skip: "Re-enable when full diffing is supported");
  });
}

Future<String> setup(String source, [String packages = ""]) async {
  JsonEncoder encoder = new JsonEncoder.withIndent('  ');
  String path = (await Directory.systemTemp.createTemp()).path;
  File file = new File(p.join(path, "example.dart"));
  await file.writeAsString(source);
  File packageFile = new File(p.join(path, ".packages"));
  await packageFile.writeAsString(packages);
  String result = await analyzeAPI("test.dart", [path]);

  String outputPath = (await Directory.systemTemp.createTemp()).path;
  File outputFile = new File(p.join(outputPath, "example.dart"));
  await outputFile.writeAsString(result);
  return outputFile.path;
}
