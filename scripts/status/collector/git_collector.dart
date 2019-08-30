// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'collector.dart';

class GitCollector implements Collector {
  @override
  FutureOr<List<Item>> collect(bool includeSlow,
      {List<Category> restrictCategories}) {
    var fuchsiaGitDir = Platform.environment['FUCHSIA_DIR'] + '/.git';
    Future<List<Item>> gitRunner = Process.run('git', [
      '--git-dir=$fuchsiaGitDir',
      'rev-parse',
      'HEAD',
      'JIRI_HEAD'
    ]).then((ProcessResult pr) {
      List<Item> result = new List();
      if (pr.exitCode != 0) {
        // just ignore
        // throw('Unexpected error running fx gn: exit code ${pr.exitCode}\n---- stderr output:\n${pr.stderr}\n------');
      } else {
        LineSplitter ls = new LineSplitter();
        List<String> lines = ls.convert(pr.stdout);
        bool isInJiriHead = (lines.length == 2 && lines[0] == lines[1]);
        result.add(new Item(CategoryType.sourceInfo, 'is_in_jiri_head',
            'Is fuchsia source project in JIRI_HEAD?', isInJiriHead));
      }
      return result;
    });
    return gitRunner;
  }
}
