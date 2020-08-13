// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:test/test.dart';
import 'package:tools.dart-strict-deps.dart_strict_deps_proto/protos/models.pb.dart';
import 'package:dart_strict_deps_lib/dependency_check.dart';
import 'package:dart_strict_deps_lib/exceptions.dart';
import 'package:mockito/mockito.dart';
import 'package:analyzer/dart/analysis/utilities.dart' as analyzer;
import 'package:analyzer/dart/ast/ast.dart';
import 'package:analyzer/source/line_info.dart';
import 'package:quiver/core.dart';

import 'package:package_config/package_config.dart';

class MockResolver extends Mock implements PackageConfig {}

class MockImportDirective extends Mock implements ImportDirective {}

class MockCompUnit extends Mock implements CompilationUnit {}

class MockLineInfo extends Mock implements LineInfo {}

void main() {
  group('collectKnownLibsFromDirectDeps', () {
    test('Collect Known Libs works on own target', () {
      const String testTarget = 'testName';
      BuildTarget selfTarget = BuildTarget()
        ..packageName = testTarget
        ..isCurrentTarget = true
        ..rebasedSources.add('a');
      BuildInfo buildInfo = BuildInfo();
      buildInfo.buildTargets.add(selfTarget);
      final knownLibFiles =
          collectKnownLibsFromDirectDeps(buildInfo: buildInfo);
      expect(knownLibFiles, {'a'});
    });

    test('Collect Known Libs works on other target', () {
      const String otherTargetName = 'otherName';
      BuildTarget otherTarget = BuildTarget()
        ..packageName = otherTargetName
        ..isCurrentTarget = false
        ..rebasedSources.add('a');
      BuildInfo buildInfo = BuildInfo();
      buildInfo.buildTargets.add(otherTarget);
      final knownLibFiles =
          collectKnownLibsFromDirectDeps(buildInfo: buildInfo);
      expect(knownLibFiles, {'a'});
    });
  });

  group('Import path handling', () {
    MockResolver mockResolver = MockResolver();
    const currentFileDirectory = '/somepath/subfolder/';
    final resolverContext = ResolverContext(mockResolver, {}, {},
        currentFileDirectory: currentFileDirectory);

    test('Import Directive ignores dart and dart-ext imports', () {
      expect(isDartSDKImport(uri: Uri.parse('dart:io')), isTrue);
      expect(isDartSDKImport(uri: Uri.parse('dart-ext:io')), isTrue);
      expect(isDartSDKImport(uri: Uri.parse('package:io')), isFalse);
      expect(isDartSDKImport(uri: Uri.parse('../somefile.txt')), isFalse);
    });

    test('Import path resolves file to uri', () {
      Uri fileUri = Uri.parse('somefile.txt');
      expect(
          resolveImportPath(
              uri: fileUri,
              resolverContext: resolverContext,
              type: ImportResult_Type.RELATIVE),
          Optional.of('/somepath/subfolder/somefile.txt'));
    });

    test('Import path fails on invalid scheme', () {
      Uri invalidUri = Uri.parse('nonValidScheme:something');
      expect(
          resolveImportPath(
              uri: invalidUri,
              resolverContext: resolverContext,
              type: ImportResult_Type.ERROR),
          Optional.absent());
    });

    test('Import path resolves relative path', () {
      Uri relativePathUri = Uri.parse('../somefile.txt');
      // tests for path normalization
      expect(
          resolveImportPath(
              uri: relativePathUri,
              resolverContext: resolverContext,
              type: ImportResult_Type.RELATIVE),
          Optional.of('/somepath/somefile.txt'));
    });

    test('Import path resolves correctUri', () {
      Uri resolveablePackageUri = Uri.parse('package:resolveableUri');
      Uri resolvedPackageUri = Uri.parse('resolvedUri');
      when(mockResolver.resolve(resolveablePackageUri))
          .thenReturn(resolvedPackageUri);
      expect(
          resolveImportPath(
              uri: resolveablePackageUri,
              resolverContext: resolverContext,
              type: ImportResult_Type.PACKAGE),
          Optional.of(resolvedPackageUri.path));
    });
  });

  group('getImportType', () {
    test('dart: import', () {
      expect(getImportType(Uri.parse('dart:io')), ImportResult_Type.DART_SDK);
    });
    test('dart-ext: import', () {
      expect(
          getImportType(Uri.parse('dart-ext:io')), ImportResult_Type.DART_SDK);
    });
    test('Invalid scheme', () {
      expect(getImportType(Uri.parse('ptato:io')), ImportResult_Type.ERROR);
    });
    test('package import', () {
      expect(getImportType(Uri.parse('package:io')), ImportResult_Type.PACKAGE);
    });
    test('relative import', () {
      expect(getImportType(Uri.parse('./io.txt')), ImportResult_Type.RELATIVE);
    });
  });

  group('compilationUnit', () {
    MockResolver resolver = MockResolver();
    ResolverContext resolverContext = ResolverContext(resolver, {}, {});

    const file = '/somedir/somefile.txt';

    test('checkCompilationUnit dart imports found', () {
      String sourceText = '''import 'dart:io';''';
      final compUnit = analyzer.parseString(content: sourceText).unit;

      FileCheckResult res = checkCompilationUnit(compUnit,
          file: file, resolverContext: resolverContext);
      // dart imports are found even with empty known lib files as we assume SDK imports are always valid
      expect(res.imports, hasLength(1));
      expect(res.imports[0].importUri, 'dart:io');
      expect(res.imports[0].state, ImportResult_State.FOUND);
    });

    test('checkCompilationUnit package imports found', () {
      String sourceText = '''import 'package:resolveableUri';''';
      final compUnit = analyzer.parseString(content: sourceText).unit;
      final knownLibFiles = {'resolvedUri'};
      ResolverContext resolverContext =
          ResolverContext(resolver, knownLibFiles, {'resolveableUri'});
      when(resolver.resolve(any)).thenReturn(Uri.parse('resolvedUri'));

      FileCheckResult res = checkCompilationUnit(compUnit,
          resolverContext: resolverContext, file: file);
      expect(res.imports, hasLength(1));
      expect(res.imports[0].importUri, 'package:resolveableUri');
      expect(res.imports[0].resolvedLocation, 'resolvedUri');
      expect(res.imports[0].state, ImportResult_State.FOUND);
    });

    test('checkCompilationUnit package import not dependency', () {
      String sourceText = '''import 'package:resolveableUri';''';
      final compUnit = analyzer.parseString(content: sourceText).unit;
      when(resolver.resolve(any)).thenReturn(Uri.parse('resolvedUri'));

      FileCheckResult res = checkCompilationUnit(compUnit,
          file: file, resolverContext: resolverContext);
      expect(res.imports, hasLength(1));
      expect(res.imports[0].importUri, 'package:resolveableUri');
      expect(res.imports[0].resolvedLocation, 'resolvedUri');
      expect(res.imports[0].state, ImportResult_State.NOT_FOUND);
    });

    test('checkCompilationUnit invalid scheme', () {
      String sourceText = '''import 'invalidScheme:resolveableUri';''';
      final compUnit = analyzer.parseString(content: sourceText).unit;

      FileCheckResult res = checkCompilationUnit(compUnit,
          file: file, resolverContext: resolverContext);
      expect(res.imports, hasLength(1));
      expect(res.imports[0].state, ImportResult_State.NOT_FOUND);
    });

    test('checkCompilationUnit relative import', () {
      String sourceText = '''import 'file.txt';''';
      final compUnit = analyzer.parseString(content: sourceText).unit;
      final knownLibFiles = {'/somedir/file.txt'};
      ResolverContext resolverContext =
          ResolverContext(resolver, knownLibFiles, {'mockPackage'})
            ..currentPackage = 'mockPackage';
      FileCheckResult res = checkCompilationUnit(compUnit,
          file: file, resolverContext: resolverContext);
      expect(res.imports, hasLength(1));
      expect(res.imports[0].importUri, 'file.txt');
      expect(res.imports[0].resolvedLocation, '/somedir/file.txt');
      expect(res.imports[0].state, ImportResult_State.FOUND);
    });

    test('checkCompilationUnit multiple imports', () {
      String sourceText = '''
      import 'dart:io';
      import 'package:resolveableUri';
      ''';
      final compUnit = analyzer.parseString(content: sourceText).unit;
      final knownLibFiles = {'resolvedUri'};
      when(resolver.resolve(any)).thenReturn(Uri.parse('resolvedUri'));
      final knownPackages = {'resolveableUri'};
      ResolverContext resolverContext =
          ResolverContext(resolver, knownLibFiles, knownPackages);

      FileCheckResult res = checkCompilationUnit(compUnit,
          file: file, resolverContext: resolverContext);
      expect(res.imports, hasLength(2));
      expect(res.imports[0].importUri, 'dart:io');
      expect(res.imports[0].state, ImportResult_State.FOUND);
      expect(res.imports[1].importUri, 'package:resolveableUri');
      expect(res.imports[1].resolvedLocation, 'resolvedUri');
      expect(res.imports[1].state, ImportResult_State.FOUND);
    });

    test('checkTarget null compUnit', () {
      final target = BuildTarget()..rebasedSources.add('a');
      const compUnit = null;
      final result = checkTarget(
          target: target,
          resolverContext: resolverContext,
          parseCompilationUnitFunc: (f) => Optional.fromNullable(compUnit));
      expect(result.files, isEmpty);
    });

    test('checkTarget goes through all targets', () {
      final target = BuildTarget()..rebasedSources.addAll(['a', 'b', 'c']);
      final compUnit = analyzer.parseString(content: '').unit;
      final result = checkTarget(
          target: target,
          resolverContext: resolverContext,
          parseCompilationUnitFunc: (f) => Optional.of(compUnit));
      expect(result.files, hasLength(3));
    });
  });

  group('checkBuildInfo', () {
    MockResolver resolver = MockResolver();
    test('missing current target', () {
      final buildInfo = BuildInfo();
      expect(() => checkBuildInfo(buildInfo: buildInfo, resolver: resolver),
          throwsA(TypeMatcher<StrictDepsFatalException>()));
    });

    test('multiple current target throws error', () {
      final buildTarget1 = BuildTarget()..isCurrentTarget = true;
      final buildTarget2 = BuildTarget()..isCurrentTarget = true;
      final buildInfo = BuildInfo()
        ..buildTargets.addAll([buildTarget1, buildTarget2]);
      expect(() => checkBuildInfo(buildInfo: buildInfo, resolver: resolver),
          throwsStateError);
    });

    test('e2e test', () {
      final buildTarget1 = BuildTarget()
        ..isCurrentTarget = true
        ..rebasedSources.addAll(['./a.dart'])
        ..packageName = 'curPack';
      final buildTarget2 = BuildTarget()
        ..isCurrentTarget = false
        ..rebasedSources.addAll(['/c.dart', '/subdir/d.dart'])
        ..packageName = 'otherPack';
      final buildTarget3 = BuildTarget()
        ..isCurrentTarget = false
        ..rebasedSources.addAll(['/e.dart', '/subdir/f.dart'])
        ..packageName = 'otherPack2';
      String sourceText = '''
      import 'package:otherPack/c.dart';
      import 'package:otherPack2/subdir/f.dart';
      ''';
      final compUnit = analyzer.parseString(content: sourceText).unit;
      MockResolver resolver = MockResolver();
      when(resolver.resolve(Uri.parse('package:otherPack/c.dart')))
          .thenReturn(Uri.parse('/c.dart'));
      when(resolver.resolve(Uri.parse('package:otherPack/subdir/d.dart')))
          .thenReturn(Uri.parse('/subdir/d.dart'));
      when(resolver.resolve(Uri.parse('package:otherPack2/e.dart')))
          .thenReturn(Uri.parse('/e.dart'));
      when(resolver.resolve(Uri.parse('package:otherPack2/subdir/f.dart')))
          .thenReturn(Uri.parse('/subdir/f.dart'));
      final buildInfo = BuildInfo()
        ..buildTargets.addAll([buildTarget1, buildTarget2, buildTarget3]);
      final targetCheckResult = checkBuildInfo(
          buildInfo: buildInfo,
          resolver: resolver,
          parseCompilationUnitFunc: (f) => Optional.of(compUnit));
      expect(targetCheckResult.files, hasLength(1));
      expect(
          targetCheckResult.files[0].imports.where(
              (importResult) => importResult.state != ImportResult_State.FOUND),
          hasLength(0));
    });

    test('e2e test fail', () {
      final buildTarget1 = BuildTarget()
        ..isCurrentTarget = true
        ..rebasedSources.addAll(['./a.dart'])
        ..packageName = 'curPack';
      final buildTarget2 = BuildTarget()
        ..isCurrentTarget = false
        ..rebasedSources.addAll(['/c.dart', '/subdir/d.dart'])
        ..packageName = 'otherPack';
      final buildTarget3 = BuildTarget()
        ..isCurrentTarget = false
        ..rebasedSources.addAll(['/e.dart', '/subdir/f.dart'])
        ..packageName = 'otherPack2';
      String sourceText = '''
      import 'package:otherPack/z.dart';
      import 'package:otherPack2/subdir/f.dart';
      ''';
      final compUnit = analyzer.parseString(content: sourceText).unit;
      MockResolver resolver = MockResolver();
      when(resolver.resolve(Uri.parse('package:otherPack/c.dart')))
          .thenReturn(Uri.parse('/c.dart'));
      when(resolver.resolve(Uri.parse('package:otherPack/subdir/d.dart')))
          .thenReturn(Uri.parse('/subdir/d.dart'));
      when(resolver.resolve(Uri.parse('package:otherPack2/e.dart')))
          .thenReturn(Uri.parse('/e.dart'));
      when(resolver.resolve(Uri.parse('package:otherPack2/subdir/f.dart')))
          .thenReturn(Uri.parse('/subdir/f.dart'));
      final buildInfo = BuildInfo()
        ..buildTargets.addAll([buildTarget1, buildTarget2, buildTarget3]);
      final targetCheckResult = checkBuildInfo(
          buildInfo: buildInfo,
          resolver: resolver,
          parseCompilationUnitFunc: (f) => Optional.of(compUnit));
      expect(targetCheckResult.files, hasLength(1));
    });
  });
}
