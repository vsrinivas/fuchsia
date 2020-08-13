// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:test/test.dart';
import 'package:tools.dart-strict-deps.dart_strict_deps_proto/protos/models.pb.dart';
import 'package:dart_strict_deps_lib/file_processor.dart';

void main() {
  test('Read empty json to BuildInfo succeeds', () {
    var json = '[]';
    BuildInfo buildInfo = buildInfoFromJson(json);
    expect(buildInfo.buildTargets, isEmpty);
  });

  test('Read invalid json fails', () {
    var json = 'invalidJson';
    expect(() => buildInfoFromJson(json), throwsFormatException);
  });

  test('Read valid json to BuildInfo same as expected', () {
    var json = '''
      [
        {
          "__package_name": "test1",
          "__public_deps": ["a", "b"],
          "__deps": ["c"],
          "__rebased_sources": ["d"],
          "__is_current_target": true
        },
        {
          "__package_name": "test2",
          "__public_deps": ["e"],
          "__deps": ["f"],
          "__rebased_sources": ["g"],
          "__is_current_target": false
        }
      ]
      ''';

    BuildTarget test1 = BuildTarget()
      ..packageName = 'test1'
      ..publicDeps.addAll(['a', 'b'])
      ..deps.add('c')
      ..rebasedSources.add('d')
      ..isCurrentTarget = true;
    BuildTarget test2 = BuildTarget()
      ..packageName = 'test2'
      ..publicDeps.add('e')
      ..deps.add('f')
      ..rebasedSources.add('g')
      ..isCurrentTarget = false;
    BuildInfo expectedBuildInfo = BuildInfo()
      ..buildTargets.addAll([test1, test2]);
    BuildInfo buildInfo = buildInfoFromJson(json);
    expect(buildInfo, expectedBuildInfo);
  });
}
