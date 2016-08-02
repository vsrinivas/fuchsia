// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:io';

import 'package:file_utils/file_utils.dart';
import 'package:path/path.dart' as path;
import 'package:yaml/yaml.dart';

import 'base/file_system.dart';

enum ProjectType { dart, flutter, }

enum HostPlatform { mac, linux, }
const Map<HostPlatform, String> hostPlatformToArchString = const {
  HostPlatform.linux: 'linux-x64',
  HostPlatform.mac: 'mac-x64',
};

enum TargetPlatform { android, linux, }
const Map<TargetPlatform, String> targetPlatformToArchString = const {
  TargetPlatform.linux: 'linux-x64',
  TargetPlatform.android: 'android-arm',
};

// Configuration information that is set up by `modular configure` command.
class EnvironmentConfiguration {
  final String base;
  final String modularRoot;
  final bool isModularRepo;
  final String mojoRevision;

  EnvironmentConfiguration._(
      this.base, this.modularRoot, this.isModularRepo, this.mojoRevision);

  factory EnvironmentConfiguration() {
    final String currentDirectory = FileUtils.getcwd();

    // Use the location of the script to find the root of the modular repo
    final String scriptPath = Platform.script.toFilePath();
    final String modularRoot =
        path.dirname(path.dirname(path.dirname(scriptPath)));

    final bool isModularRepo = (currentDirectory == modularRoot) ||
        path.isWithin(modularRoot, currentDirectory);

    final File mojoVersionFile =
        new File(path.join(modularRoot, 'MOJO_VERSION'));
    final String mojoRevision = mojoVersionFile.readAsStringSync().trim();

    return new EnvironmentConfiguration._(
        isModularRepo ? modularRoot : currentDirectory,
        modularRoot,
        isModularRepo,
        mojoRevision);
  }

  String get flutterRoot => path.join(modularRoot, 'third_party', 'flutter');
  String get dartSdkPath => path.join(flutterRoot, 'bin', 'cache', 'dart-sdk');
  String get devtoolsPath =>
      path.join(modularRoot, 'third_party', 'mojo_devtools');
  String get buildDir => path.join(base, 'build');

  Stream<File> _pubspecFiles;
  List<ProjectConfiguration> _projectConfigurations = [];
  Stream<ProjectConfiguration> get projectConfigurations async* {
    for (final ProjectConfiguration projectConfiguration
        in _projectConfigurations) {
      yield projectConfiguration;
    }
    if (_pubspecFiles == null) {
      _pubspecFiles = findFilesAndFilter(base, 'pubspec.yaml',
              const ['/.pub', '/build', '/out', '/third_party'])
          .asBroadcastStream();
    }
    await for (final File pubspecFile in _pubspecFiles) {
      _projectConfigurations.add(await ProjectConfiguration._fromProjectRoot(
          path.dirname(pubspecFile.path), path.absolute(buildDir)));
      yield _projectConfigurations.last;
    }
  }

  HostPlatform get hostPlatform =>
      Platform.isMacOS ? HostPlatform.mac : HostPlatform.linux;
}

class ProjectConfiguration {
  static const String _projectRootValidationErrorMessage =
      'Error: pubspec.yaml file not found.\n'
      'This command should be run from the root of your Modular project.';

  final YamlMap _modularConfig;
  final String projectRoot;

  const ProjectConfiguration._(this.projectRoot, this._modularConfig);

  static Future<ProjectConfiguration> _fromProjectRoot(
      String projectRoot, String buildDir) async {
    final String pubspecPath = path.join(projectRoot, 'pubspec.yaml');
    if (!(await FileSystemEntity.isFile(pubspecPath))) {
      stderr.writeln(_projectRootValidationErrorMessage);
      return null;
    }
    final pubspecContent = await (new File(pubspecPath)).readAsString();
    final pubspec = loadYaml(pubspecContent);
    return new ProjectConfiguration._(projectRoot, pubspec['modular']);
  }

  ProjectType get projectType {
    if (_modularConfig != null &&
        _modularConfig.containsKey('project_type') &&
        _modularConfig['project_type'] == 'flutter') {
      return ProjectType.flutter;
    }
    return ProjectType.dart;
  }

  String get packageRoot => path.join(projectRoot, 'packages');

  // A list of YAML maps where each entry of the list has information for
  // building a single dart / flutter module target.
  List<YamlMap> get targets {
    if (_modularConfig == null) return null;
    return _modularConfig['targets'];
  }

  // A list of project-relative paths that point to assets that should be
  // deployed to the CDN.
  Iterable<String> get deployedAssets {
    if (_modularConfig == null) return null;
    return _modularConfig['deployed_assets']?.value;
  }
}
