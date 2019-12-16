// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'collector.dart';

/// Determines whether we consider two git pointers as equivalent.
///
/// Accepts two strings, which should be git pointers, and returns whether they
/// are equivalent. Useful to prevent other classes from returning awkward and
/// unstructured tuples of strings, and in case we ever add constraints to
/// "equivalency".
class GitJiriStatus {
  final String head;
  final String jiriHead;
  GitJiriStatus(this.head, this.jiriHead);

  bool get isSourceInJiriHead => head == jiriHead;
}

/// Receives a [ProcessResult] which contains information about Git hashes.
class GitStatusParser {
  GitJiriStatus parse({ProcessResult processResult}) {
    if (processResult.exitCode != 0) {
      return null;
    }
    LineSplitter ls = LineSplitter();
    List<String> lines = ls.convert(processResult.stdout);

    String head = lines.isNotEmpty ? lines[0] : 'BAD_GIT_HASH';
    String jiriHead = lines.length > 1 ? lines[1] : 'BAD_JIRI_GIT_HASH';
    return GitJiriStatus(head, jiriHead);
  }
}

/// Asks git for the HEAD and JIRI_HEAD hashes. Intended to be paired with the
/// [GitStatusParser] class, which knows how to make sense of this result.
class GitStatusChecker {
  Future<ProcessResult> checkStatus() async {
    var fuchsiaGitDir = '${Platform.environment['FUCHSIA_DIR']}/.git';
    return Process.run('git', [
      '--git-dir=$fuchsiaGitDir',
      'rev-parse',
      'HEAD',
      'JIRI_HEAD',
    ]);
  }
}

/// Confirms whether git's HEAD and JIRI_HEAD share the same hash.
///
/// Accepts optional [GitStatusChecker] and [GitStatusParser] helper
/// classes which respectively know how to make a git system call and
/// make sense of those results. Not passing values leads to default
/// behavior, which is usually what you want.
class GitCollector implements Collector {
  @override
  Future<List<Item>> collect({
    GitStatusChecker statusChecker,
    GitStatusParser statusParser,
  }) async {
    // Create default values
    statusChecker ??= GitStatusChecker();
    statusParser ??= GitStatusParser();

    ProcessResult pr = await statusChecker.checkStatus();
    GitJiriStatus status = statusParser.parse(processResult: pr);

    if (status == null) {
      return null;
    }

    return <Item>[
      Item(
        CategoryType.sourceInfo,
        'is_in_jiri_head',
        'Is fuchsia source project in JIRI_HEAD?',
        status.isSourceInJiriHead,
      ),
    ];
  }
}
