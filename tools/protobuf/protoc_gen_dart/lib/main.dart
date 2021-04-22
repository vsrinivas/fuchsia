// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';

import 'package:path/path.dart' as path;
import 'package:protoc_plugin/protoc.dart';

/// This is the alternate plugin driver for dart protobuf bindings. This
/// plugin driver is suppled into the `protoc` (or protoc_wrapper.py). This
/// driver is used alongside GN, takes in a mapping of dart package name -> dart
/// package paths, and uses these mappings to transform strictly file-based
/// imports to package-based imports.
///
/// The plugin option passed to `protoc` end up here. The option format is:
/// A semicolon-separated list of package name -> package directory mappings of
/// the form `package_name|input_root`. `input_root` designates the directory
/// relative to which input protos are located -- typically the root of the GN
/// package, where the `BUILD.gn` file is located.
///
/// The options above are used in the following way:
/// * Given a file-based import path in the .proto file, the generated dart
///   import path is rebased to a package path if possible.
/// * See [DartImportMappingOutputConfiguration.resolveImport] below.
class DartImportMappingOptionParser extends SingleOptionParser {
  /// This is the option key we pass to protoc;
  static const String kImportOptionKey = 'PackagePaths';

  /// Output map of: package input root => package name
  final Map<String, String> packageMapping;

  DartImportMappingOptionParser(this.packageMapping);

  @override
  void parse(String name, String value, onError(String message)) {
    if (value == null) {
      onError('Invalid $kImportOptionKey option. Expected a non-empty value.');
      return;
    }

    for (var entry in value.split(';')) {
      var fields = entry.split('|');
      if (fields.length != 2) {
        onError('ERROR: expected package_name|input_root. Got: $entry');
        continue;
      }
      if (packageMapping.containsKey(fields[1])) {
        onError('ERROR: ${fields[1]} already exists.');
        continue;
      }
      packageMapping[fields[1]] = fields[0];
    }
  }
}

class DartImportMappingOutputConfiguration extends DefaultOutputConfiguration {
  /// Output map (package root) => package name
  final Map<String, String> packageMapping;

  DartImportMappingOutputConfiguration(this.packageMapping);

  /// Given a file-based import path ("a/b/c/d.proto"), this routine looks
  /// through the [packageMapping] to find a package that this import may fall
  /// under.  If it can find one, it rebases the import path to the package
  /// input root in the generated import string.
  @override
  Uri resolveImport(Uri target, Uri source, String extension) {
    for (int i = target.pathSegments.length; i > 0; i--) {
      var candidatePath = target.pathSegments.sublist(0, i).join('/');
      if (packageMapping.containsKey(candidatePath)) {
        var packageName = packageMapping[candidatePath];
        var importPath = target.pathSegments
            .sublist(i, target.pathSegments.length)
            .join('/');
        var genImportPath = '${path.withoutExtension(importPath)}.pb.dart';
        return Uri.parse('package:$packageName/$genImportPath');
      }
    }
    return super.resolveImport(target, source, extension);
  }
}

void main() {
  var packageMapping = <String, String>{};
  CodeGenerator(stdin, stdout).generate(
    optionParsers: {
      DartImportMappingOptionParser.kImportOptionKey:
          DartImportMappingOptionParser(packageMapping),
    },
    config: DartImportMappingOutputConfiguration(packageMapping),
  );
}
