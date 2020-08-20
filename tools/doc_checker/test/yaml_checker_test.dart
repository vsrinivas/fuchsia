// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';

import 'package:test/test.dart';
import 'package:doc_checker/yaml_checker.dart';

void main() {
  // Clean up the yaml files after every test
  tearDown(() {
    List<File> files = [
      File('${Directory.systemTemp.path}/contents.yaml'),
      File('${Directory.systemTemp.path}/docs/contents.yaml'),
      File('${Directory.systemTemp.path}/docs/_included.yaml'),
      File('${Directory.systemTemp.path}/docs/hello.md'),
      File('${Directory.systemTemp.path}/docs/world.md'),
      File('${Directory.systemTemp.path}/docs/zircon/good.md'),
      File('${Directory.systemTemp.path}/docs/objects/hello.md'),
      File('${Directory.systemTemp.path}/docs/objects/README.md'),
      File('${Directory.systemTemp.path}/docs/objects.md'),
    ];

    for (var f in files) {
      if (f.existsSync()) {
        f.deleteSync();
      }
    }
  });

  group('doc_checker yaml_checker tests', () {
    test('empty list of toc files', () async {
      YamlChecker checker =
          YamlChecker(Directory.systemTemp.path, null, [], []);
      bool ret = await checker.check();
      expect(ret, equals(true));
    });

    test('filter hidden directory', () {
      YamlChecker checker = YamlChecker('/usr/docs', null, [], []);
      final files = [
        '/usr/docs/_hidden/a.md',
        '/usr/docs/_hidden/b.md',
        '/usr/docs/not_hidden/c.md',
      ];
      expect(
          checker.filterHidden(files), equals(['/usr/docs/not_hidden/c.md']));
    });

    test('filter hidden file', () {
      YamlChecker checker = YamlChecker('/usr/_foo/docs', null, [], []);
      final files = [
        '/usr/_foo/docs/_a.md',
        '/usr/_foo/docs/b.md',
      ];
      expect(checker.filterHidden(files), equals(['/usr/_foo/docs/b.md']));
    });

    test('Happy path single yaml file', () async {
      const String contents = '''toc:
- title: Hello
  path: /docs/hello.md
- title: "World"
  path: /docs/world.md
''';
      File file = File('${Directory.systemTemp.path}/docs/contents.yaml')
        ..createSync(recursive: true)
        ..writeAsStringSync(contents);
      File('${Directory.systemTemp.path}/docs/hello.md')
        ..createSync(recursive: true)
        ..writeAsStringSync('hello');
      File('${Directory.systemTemp.path}/docs/world.md')
        ..createSync(recursive: true)
        ..writeAsStringSync('world');
      YamlChecker checker = YamlChecker(Directory.systemTemp.path,
          file.absolute.path, [file.absolute.path], []);
      bool ret = await checker.check();
      if (!ret) {
        print('Unexpected errors: ${checker.errors}');
      }
      expect(ret, equals(true));
      expect(checker.errors.isEmpty, equals(true));
    });

    test('Misspelled keywords', () async {
      const String contents = '''toc:
- tile: Hello
  path: /docs/hello.md
- title: "World"
  filepath: /docs/world.md
''';

      File file = File('${Directory.systemTemp.path}/contents.yaml')
        ..writeAsStringSync(contents);
      File('${Directory.systemTemp.path}/docs/hello.md')
        ..createSync(recursive: true)
        ..writeAsStringSync('hello');
      File('${Directory.systemTemp.path}/docs/world.md')
        ..createSync(recursive: true)
        ..writeAsStringSync('world');
      YamlChecker checker = YamlChecker(Directory.systemTemp.path,
          file.absolute.path, [file.absolute.path], []);
      bool ret = await checker.check();

      expect(ret, equals(false));
      expect(checker.errors[0].content, equals('Unknown Content Key tile'));
      expect(checker.errors[1].content, equals('Unknown Content Key filepath'));
      expect(checker.errors.length, equals(2));
    });

    test('Bad paths ', () async {
      const String content = '''toc:
- title: Hello
  path: /docs/hello.md
- title: "World"
  path: /docs/zircon/good.md
- title: "Source"
  path: /src/ref/1.cc
- title: Good
  path: /docs/hello.md
- title: Google
  path: https://google.com
''';

      File file = File('${Directory.systemTemp.path}/contents.yaml')
        ..writeAsStringSync(content);
      File('${Directory.systemTemp.path}/docs/hello.md')
        ..createSync(recursive: true)
        ..writeAsStringSync('hello');
      File('${Directory.systemTemp.path}/docs/zircon/good.md')
        ..createSync(recursive: true)
        ..writeAsStringSync('good');
      YamlChecker checker = YamlChecker(Directory.systemTemp.path,
          file.absolute.path, [file.absolute.path], []);
      bool ret = await checker.check();
      expect(ret, equals(false));
      expect(checker.errors[0].content,
          equals('Path needs to start with \'/docs\', got /src/ref/1.cc'));
      expect(checker.errors.length, equals(1));
    });

    test('include happy path', () async {
      const String content = '''toc:
- title: Hello
  section:
  - include: /docs/_included.yaml
''';

      const String includedContent = '''toc:
- title: World
  path: /docs/world.md
''';

      File file = File('${Directory.systemTemp.path}/contents.yaml')
        ..writeAsStringSync(content);

      File('${Directory.systemTemp.path}/docs/_included.yaml')
        ..createSync(recursive: true)
        ..writeAsStringSync(includedContent);
      File('${Directory.systemTemp.path}/docs/world.md')
        ..createSync(recursive: true)
        ..writeAsStringSync('world');
      YamlChecker checker = YamlChecker(Directory.systemTemp.path,
          file.absolute.path, [file.absolute.path], []);
      bool ret = await checker.check();
      expect(ret, equals(true));
    });

    test('include invalid path', () async {
      const String content = '''toc:
- title: Hello
  section:
  - include: /docs/_included.yaml
''';

      const String includedContent = '''toc:
- title: World
  path: /src/sample/1.c
''';

      File file = File('${Directory.systemTemp.path}/contents.yaml')
        ..writeAsStringSync(content);
      File('${Directory.systemTemp.path}/docs/_included.yaml')
        ..createSync(recursive: true)
        ..writeAsStringSync(includedContent);
      YamlChecker checker = YamlChecker(Directory.systemTemp.path,
          file.absolute.path, [file.absolute.path], []);
      bool ret = await checker.check();
      expect(ret, equals(false));
      expect(checker.errors[0].content,
          equals('Path needs to start with \'/docs\', got /src/sample/1.c'));
    });

    test('include file not found', () async {
      const String content = '''toc:
- title: Hello
  section:
  - include: /docs/_included_notfound.yaml
''';

      File file = File('${Directory.systemTemp.path}/contents.yaml')
        ..writeAsStringSync(content);
      YamlChecker checker = YamlChecker(Directory.systemTemp.path,
          file.absolute.path, [file.absolute.path], []);
      bool ret = await checker.check();
      expect(ret, equals(false));
      expect(checker.errors.length, equals(1));
      expect(
          checker.errors[0].content,
          startsWith(
              'FileSystemException: Cannot open file, path = \'${Directory.systemTemp.path}/docs/_included_notfound.yaml\''));
    });

    // Test when there is a directory with the same name and there is a README.md in that
    // directory, and there is a markdown file with the name of the directory.
    // Ideally, there should not be this type of ambiguity, but it happens.
    test('file and dir with same name', () async {
      const String content = '''toc:
- title: Objects readme
  path: /docs/objects
- title: readme
  path: /docs/objects/README.md
- title: Objects
  path: /docs/objects.md
- title: "Hello objects"
  path: /docs/objects/hello.md
''';

      File file = File('${Directory.systemTemp.path}/contents.yaml')
        ..writeAsStringSync(content);
      File('${Directory.systemTemp.path}/docs/objects/README.md')
        ..createSync(recursive: true)
        ..writeAsStringSync('readme objects');
      File('${Directory.systemTemp.path}/docs/objects/hello.md')
        ..createSync(recursive: true)
        ..writeAsStringSync('hello objects');
      File('${Directory.systemTemp.path}/docs/objects.md')
        ..createSync(recursive: true)
        ..writeAsStringSync('objects');
      YamlChecker checker = YamlChecker(Directory.systemTemp.path,
          file.absolute.path, [file.absolute.path], []);
      bool ret = await checker.check();
      if (!ret) {
        print('Unexpected failure: ${checker.errors}');
      }
      expect(ret, equals(true));
    });
  }); // group
}
