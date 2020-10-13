// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';
import 'package:analyzer/dart/analysis/utilities.dart' as analyzer;
import 'package:analyzer/file_system/file_system.dart';
import 'package:analyzer/dart/analysis/features.dart';
import 'package:analyzer/dart/ast/ast.dart';
import 'package:analyzer/source/line_info.dart';
import 'package:dart_strict_deps_lib/exceptions.dart';
import 'package:meta/meta.dart';
import 'package:package_config/package_config.dart';
import 'package:path/path.dart' as p;
import 'package:quiver/check.dart';
import 'package:quiver/core.dart';
import 'package:tools.dart-strict-deps.dart_strict_deps_proto/protos/models.pb.dart';

p.Context context = p.Context(style: p.Style.posix);

// Collects list of source files from direct dependencies.
Set<String> collectKnownLibsFromDirectDeps({BuildInfo buildInfo}) =>
    Set.from(buildInfo.buildTargets.expand((target) => target.rebasedSources));

// Collects list of known packages from direct dependencies
Set<String> collectKnownPackagesFromDirectDeps({BuildInfo buildInfo}) =>
    Set.from(buildInfo.buildTargets.map((target) => target.packageName));

/// Returns compilation unit read with dart analyzer from filepath.
Optional<CompilationUnit> parseCompilationUnit(String filePath) {
  try {
    return Optional.of(analyzer
        .parseFile(
            path: filePath,
            featureSet: FeatureSet.fromEnableFlags(["non-nullable"]))
        .unit);
  } on FileSystemException {
    return Optional.absent();
  }
}

/// Returns true if the [uri] scheme is considered a Dart SDK import.
bool isDartSDKImport({@required Uri uri}) =>
    (uri?.scheme == 'dart' || uri?.scheme == 'dart-ext');

/// Resolves import paths (package imports and relative imports) to absolute file path.
///
/// Returns an absent Optional if the import does not have a valid scheme.
Optional<String> resolveImportPath(
    {@required Uri uri,
    @required ResolverContext resolverContext,
    @required ImportResult_Type type}) {
  if (type == ImportResult_Type.PACKAGE) {
    Uri resolved = resolverContext.resolver.resolve(uri);
    if (resolved == null) return Optional.absent();
    return Optional.of(resolved.path);
  } else if (type == ImportResult_Type.RELATIVE) {
    //local path
    return Optional.of(context.normalize(
        context.join(resolverContext.currentFileDirectory, uri.path)));
  }
  return Optional.absent();
}

/// Returns true if the [path] exists on file system.
bool pathExists(String path) =>
    FileSystemEntity.typeSync(path) != FileSystemEntityType.notFound;

/// Returns the type of import [uri] represents.
ImportResult_Type getImportType(Uri uri) {
  if (uri == null) {
    return ImportResult_Type.ERROR;
  }
  if (isDartSDKImport(uri: uri)) {
    return ImportResult_Type.DART_SDK;
  } else if (uri.hasScheme) {
    // package is only valid scheme after dart and dart_ext
    return uri.isScheme('package')
        ? ImportResult_Type.PACKAGE
        : ImportResult_Type.ERROR;
  }
  return ImportResult_Type.RELATIVE;
}

// Returns a ImportResult with context info filled out, specifically lineInfo and importURI.
ImportResult getBaseImportResultFromDirective(ImportDirective directive,
    {LineInfo lineInfo, Uri uri}) {
  final lineNumber =
      lineInfo.getLocation(directive.offset).lineNumber.toString();
  return ImportResult()
    ..lineInfo = lineNumber
    ..importUri = uri.toString();
}

/// Wrapper class for path resolution helpers.
class ResolverContext {
  final PackageConfig resolver;
  Set<String> knownLibFiles;
  Set<String> knownPackages;
  String currentFileDirectory;
  String currentPackage;
  ResolverContext(this.resolver, this.knownLibFiles, this.knownPackages,
      {this.currentFileDirectory});
}

/// Returns dart package being imported
///
/// Returns Optional.absent if uri scheme could not be found.
Optional<String> getDartPackage(Uri uri, {ResolverContext resolverContext}) {
  if (uri.isScheme('package') && uri.pathSegments.isNotEmpty) {
    return Optional.of(uri.pathSegments[0]);
  } else if (!uri.hasScheme) {
    return Optional.fromNullable(resolverContext?.currentPackage);
  }
  return Optional.absent();
}

/// Returns a ImportResult representing the import defined by [directive] after resolution.
ImportResult directiveToImportResult(
    {LineInfo lineInfo,
    ImportDirective directive,
    ResolverContext resolverContext}) {
  final uri = Uri.parse(Uri.encodeFull(directive.uri.stringValue));
  final importResult =
      getBaseImportResultFromDirective(directive, lineInfo: lineInfo, uri: uri)
        ..type = getImportType(uri);
  if (importResult.type == ImportResult_Type.DART_SDK) {
    importResult.state = ImportResult_State.FOUND;
    return importResult;
  }
  final path = resolveImportPath(
      uri: uri, type: importResult.type, resolverContext: resolverContext);
  final dartpkg = getDartPackage(uri, resolverContext: resolverContext);
  if (dartpkg.isPresent) {
    importResult
      ..dartPackage = dartpkg.value
      ..isDartPackageImported =
          resolverContext.knownPackages.contains(dartpkg.value);
  }
  if (importResult.isDartPackageImported) {
    importResult.state = ImportResult_State.FOUND;
  } else {
    importResult.state = ImportResult_State.NOT_FOUND;
  }
  if (path.isPresent) {
    importResult..resolvedLocation = path.value;
  }
  return importResult;
}

/// Goes through compilationUnit and handles import directives.
FileCheckResult checkCompilationUnit(CompilationUnit compUnit,
    {@required String file, @required ResolverContext resolverContext}) {
  checkArgument(compUnit != null);
  final lineInfo = compUnit.lineInfo;
  final result = FileCheckResult()..filePath = file;
  resolverContext.currentFileDirectory = context.dirname(file);
  result.imports.addAll(compUnit.directives
      .whereType<ImportDirective>()
      .map((directive) => directiveToImportResult(
          lineInfo: lineInfo,
          directive: directive,
          resolverContext: resolverContext))
      .where((importResult) => importResult != null)
      .toList());
  return result;
}

/// Processes and analyzes specific target from GN.
///
/// parseCompilationUnitFunc is needed for unit testing
TargetCheckResult checkTarget(
    {@required BuildTarget target,
    @required ResolverContext resolverContext,
    Optional<CompilationUnit> Function(String) parseCompilationUnitFunc =
        parseCompilationUnit}) {
  TargetCheckResult targetCheckResult = TargetCheckResult();
  for (var file in target.rebasedSources) {
    final compUnit = parseCompilationUnitFunc(file);
    if (compUnit.isNotPresent) continue;
    FileCheckResult parsedFile = checkCompilationUnit(compUnit.value,
        file: file, resolverContext: resolverContext);
    targetCheckResult.files.add(parsedFile);
  }
  return targetCheckResult;
}

/// Returns result of strict dependency check run against the target the metadata collection was run on.
///
/// (metadata collection is run against a specific target)
/// parseCompilationUnitFunc is needed for unit testing
TargetCheckResult checkBuildInfo(
    {@required BuildInfo buildInfo,
    @required PackageConfig resolver,
    Optional<CompilationUnit> Function(String) parseCompilationUnitFunc =
        parseCompilationUnit}) {
  final knownLibs = collectKnownLibsFromDirectDeps(buildInfo: buildInfo);
  final knownPackages =
      collectKnownPackagesFromDirectDeps(buildInfo: buildInfo);
  // find the target the metadata collection was run on
  final target = Optional.fromNullable(buildInfo.buildTargets
      .singleWhere((i) => i.isCurrentTarget, orElse: () => null));
  if (target.isNotPresent) {
    throw StrictDepsFatalException(
        'Could not find current target in buildinfo');
  }
  ResolverContext resolverContext =
      ResolverContext(resolver, knownLibs, knownPackages)
        ..currentPackage = target.value.packageName;
  return checkTarget(
      target: target.value,
      resolverContext: resolverContext,
      parseCompilationUnitFunc: parseCompilationUnitFunc);
}
