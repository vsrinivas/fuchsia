// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';

import 'package:doc_checker/errors.dart';
import 'package:doc_checker/link_checker.dart';

import 'package:path/path.dart' as path;
import 'package:test/test.dart';

void main() {
  LinkChecker checker;
  final String docsDir = '${Directory.systemTemp.path}/docs';
  final String srcDir = '${Directory.systemTemp.path}/src';
  const String docsProject = 'fuchsia';

  setUp(() async {
    checker = LinkChecker(Directory.systemTemp.path, docsDir, docsProject)
      ..checkLocalLinksOnly = true;
  });

  tearDown(() async {
    Directory docsDirectory = Directory(docsDir);
    Directory srcDirectory = Directory(srcDir);
    if (docsDirectory.existsSync()) {
      await docsDirectory.delete(recursive: true);
    }
    if (srcDirectory.existsSync()) {
      await srcDirectory.delete(recursive: true);
    }
  });

  group('doc_checker link_checker tests', () {
    test('invalid uri', () async {
      const String invalid = 'h!:\\some_crazy//String';

      List<String> links = [invalid];

      List<DocContext> docList = [
        DocContext(docsDir, 'some_label', null, links)
      ];

      bool sawError = await checker.check(docList, [], null);
      expect(sawError, isTrue);
      expect(checker.errors, hasLength(1));

      Error error = checker.errors.first;
      expect(error.type, equals(ErrorType.invalidUri));
    });

    test('non_http_link', () async {
      // check that non http, non-file schemes are ignored.
      const String maillink = 'mailto://fuchsia@fuchsia.dev';

      // check that an explicit file scheme is checked.
      String filelink = 'file:///tmp/docs/does-not-exist.md';

      List<String> links = [maillink, filelink];

      List<DocContext> docList = [DocContext(docsDir, 'nothing', null, links)];

      bool sawError = await checker.check(docList, [], null);
      // mailto should be ignored, no error.
      // filelink should be broken link error.
      expect(sawError, isTrue);

      expect(checker.errors, hasLength(1));
      Error error = checker.errors.first;
      expect(error.type, equals(ErrorType.brokenLink));
    });

    test('link_to_code', () async {
      const String codelink =
          'https://fuchsia.googlesource.com/fuchsia/+/master/src/testing/BUILD.gn';

      List<String> links = [codelink];

      List<DocContext> docList = [DocContext(docsDir, 'BUILD.gn', null, links)];

      bool sawError = await checker.check(docList, [], null);
      expect(sawError, isTrue);

      expect(checker.errors, hasLength(1));
      Error error = checker.errors.first;
      expect(error.type, equals(ErrorType.convertHttpToPath));
    });
    test('link_to_project', () async {
      const String codelink =
          'https://fuchsia.googlesource.com/topaz/+/master/tools/doc_checker/';

      List<String> links = [codelink];

      List<DocContext> docList = [
        DocContext(docsDir, 'doc_checker', null, links)
      ];

      bool sawError = await checker.check(docList, [], null);
      expect(sawError, isFalse);
    });

    test('link_to_unrelated project', () async {
      const String codelink = 'https://fuchsia.googlesource.com/peridot/';

      List<String> links = [codelink];

      List<DocContext> docList = [DocContext(docsDir, 'peridot', null, links)];

      bool sawError = await checker.check(docList, [], null);
      expect(sawError, isTrue);

      expect(checker.errors, hasLength(1));
      Error error = checker.errors.first;
      expect(error.type, equals(ErrorType.obsoleteProject));
    });

    test('link_to_devsite', () async {
      const String codelink =
          'https://fuchsia.dev/fuchsia-src/development/monitor/fidlcat';

      // These are exceptions to the rule.
      // Links to the home page are OK
      const String rootlink = 'https://fuchsia.dev';
      // Links to the reference section are OK.
      const String referencelink = 'https://fuchsia.dev/reference/something';

      // navbar.md can link to anywhere
      const navbar = 'navbar.md';

      List<String> links = [codelink, rootlink, referencelink];

      List<DocContext> docList = [
        DocContext(docsDir, 'randompage', null, links),
        DocContext(docsDir, navbar, null, links)
      ];

      bool sawError = await checker.check(docList, [], null);
      expect(sawError, isTrue);

      expect(checker.errors, hasLength(1));
      Error error = checker.errors.first;
      expect(error.type, equals(ErrorType.convertHttpToPath));
    });
    test('link_to_external sites', () async {
      const String codelink = 'https://github.com/google/fonts';
      const String codelinkWithAnchor =
          'https://github.com/google/fonts#licensing';

      List<String> links = [codelink, codelinkWithAnchor];

      List<DocContext> docList = [
        DocContext(docsDir, 'randompage', null, links)
      ];

      bool sawError = await checker.check(docList, [], null);
      expect(sawError, isFalse);
    });
    test('relative file link', () async {
      List<String> links = [
        '/docs/page1.md',
        'page1.md#topic',
        'relative/page2.md',
        'missing_page.md',
        '/src/project/main.cc',
        '#Heading2'
      ];

      List<DocContext> docList = [
        DocContext(docsDir, 'randompage', null, links)
      ];

      File('$docsDir/page1.md')
        ..createSync(recursive: true)
        ..writeAsStringSync('# Page 1\nSome text\n\n ##Topic\n\nTopic info\n');

      File('$docsDir/relative/page2.md')
        ..createSync(recursive: true)
        ..writeAsStringSync('# Page 1');

      File('$srcDir/project/main.cc')
        ..createSync(recursive: true)
        ..writeAsStringSync('print "hello world";\n');

      bool sawError = await checker.check(docList, [], null);
      expect(sawError, isTrue);

      expect(checker.errors, hasLength(1));
      Error error = checker.errors.first;
      expect(error.type, equals(ErrorType.brokenLink));
    });

    test('relative paths', () async {
      String pageLabel = '//docs/somewhere/index.md';
      List<String> links = [
        '../page1.md',
        '../../src/project/main.cc',
        '/one/two/three/../../../../../../'
      ];

      File('$docsDir/page1.md')
        ..createSync(recursive: true)
        ..writeAsStringSync('# Page 1\nSome text\n\n ##Topic\n\nTopic info\n');

      File('$srcDir/project/main.cc')
        ..createSync(recursive: true)
        ..writeAsStringSync('print "hello world";\n');

      List<DocContext> docList = [
        DocContext(path.join(docsDir, 'somewhere'), pageLabel, null, links)
      ];

      bool sawError = await checker.check(docList, [], null);
      expect(sawError, isTrue);

      expect(checker.errors, hasLength(2));
      Error error = checker.errors.first;
      expect(error.type, equals(ErrorType.invalidRelativePath));
      error = checker.errors.last;
      expect(error.type, equals(ErrorType.invalidRelativePath));
    });

    test('uri to docs', () async {
      List<String> links = [
        // This should be an error, use /docs/...
        'https://fuchsia.googlesource.com/fuchsia/+/refs/heads/master/docs/README.md',
        // This should be OK, since it goes to owners
        'https://fuchsia.googlesource.com/fuchsia/+/refs/heads/master/docs/some/path/OWNERS'
      ];

      String pageLabel = '//docs/somewhere/index.md';
      List<DocContext> docList = [
        DocContext(path.join(docsDir, 'somewhere'), pageLabel, null, links)
      ];

      bool sawError = await checker.check(docList, [], null);
      expect(sawError, isTrue);

      expect(checker.errors, hasLength(1));
      for (Error error in checker.errors) {
        expect(error.type, equals(ErrorType.convertHttpToPath));
      }
    });

    test('link to directory', () async {
      String pageLabel = '//docs/somewhere/index.md';
      List<String> links = [
        // This is OK - since there is /docs/README.md
        '/docs/',
        // This is OK - there is a README.md
        '/docs/folder',
        // This is OK - it is to a directory that is not under /docs/
        '/src/project',
        // This is an error - there is not a README.md file.
        '/docs/noreadme_folder/'
      ];

      File('$docsDir/README.md')
        ..createSync(recursive: true)
        ..writeAsStringSync('# Readme file\n');

      File('$docsDir/folder/README.md')
        ..createSync(recursive: true)
        ..writeAsStringSync('# index file in a directory.\n');

      File('$docsDir/noreadme_folder/anypage.md')
        ..createSync(recursive: true)
        ..writeAsStringSync('# No readme here.\n');

      File('$srcDir/project/main.cc')
        ..createSync(recursive: true)
        ..writeAsStringSync('print "hello world";\n');

      List<DocContext> docList = [
        DocContext(path.join(docsDir, 'somewhere'), pageLabel, null, links)
      ];

      bool sawError = await checker.check(docList, [], null);
      expect(sawError, isTrue);

      expect(checker.errors, hasLength(1));
      for (Error error in checker.errors) {
        expect(error.type, equals(ErrorType.invalidLinkToDirectory));
      }
    });
  });
}
