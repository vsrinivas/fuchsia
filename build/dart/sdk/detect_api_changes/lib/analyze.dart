// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:collection';
import 'dart:convert';
import 'dart:io';

import 'package:analyzer/dart/analysis/analysis_context_collection.dart';
import 'package:analyzer/dart/analysis/analysis_context.dart';
import 'package:analyzer/dart/analysis/session.dart';
import 'package:analyzer/dart/ast/ast.dart';
import 'package:analyzer/file_system/physical_file_system.dart';
import 'package:path/path.dart' as p;

import 'src/visitor.dart';

// Analyze the given library source files and returns a map structure
// representing all publicly available information in the library.
Future<String> analyzeAPI(String apiName, List<String> sources) async {
  SplayTreeMap<String, dynamic> package = SplayTreeMap();
  package['name'] = apiName;
  package['type'] = 'package';
  package['files'] = SplayTreeMap<String, dynamic>();

  AnalysisContextCollection collection = AnalysisContextCollection(
      includedPaths: sources,
      resourceProvider: PhysicalResourceProvider.INSTANCE);

  for (AnalysisContext context in collection.contexts) {
    for (String path in context.contextRoot.analyzedFiles()) {
      var myFile = File(path);
      String filename = p.basename(myFile.path);

      // The .packages file should be included in sources,
      // but we don't want to directly parse that file here.
      if (filename.endsWith('.dart') && !Identifier.isPrivateName(filename)) {
        DetectChangesVisitor d = DetectChangesVisitor(filename);

        AnalysisSession session = context.currentSession;
        var result = await session.getResolvedUnit(path);
        result.unit.accept(d);

        package['files'][filename] = d.file;
      }
    }
  }

  JsonEncoder encoder = JsonEncoder.withIndent('  ');
  return encoder.convert(package);
}
