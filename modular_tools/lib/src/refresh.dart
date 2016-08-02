// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:core';
import 'dart:convert' show JSON;
import 'dart:io';

import 'package:path/path.dart' as path;
import 'package:yaml/yaml.dart';

import 'base/process.dart';
import 'configuration.dart';
import 'build.dart';

class RefreshCommandRunner {
  final EnvironmentConfiguration _environment;
  final bool _release;

  RefreshCommandRunner(this._environment, this._release);

  Future<int> refresh(final String outputName) async {
    return pipeline([
      new BuildRunner(_environment, _release).runBuild,
      () => _startRefresh(outputName),
    ]);
  }

  Future<int> _startRefresh(final String outputName) async {
    final List<ProjectConfiguration> projects =
        await _environment.projectConfigurations.toList();
    String moduleToReload = outputName ?? projects[0].targets[0]['output_name'];
    String newOutputName = await _renameOutputInOurDir(moduleToReload);
    Map<String, String> reloadJson =
        await _generateJsonForReload(moduleToReload, newOutputName);
    return _reloadModuleThroughDebugServer(reloadJson);
  }

  List<String> _getOutputNameParts(String outputName) {
    final List<String> nameParts = outputName.split('.');
    // Check that name parts are less than or equal to 3.
    // One for the name, one for hash, one for the extension.
    assert(nameParts.length < 4);
    return nameParts;
  }

  String _getNewOutpuName(String currentName) {
    final List<String> nameParts = _getOutputNameParts(currentName);
    return [
      nameParts[0],
      new DateTime.now().hashCode,
      nameParts[nameParts.length - 1]
    ].join('.');
  }

  // Returns the file name pattern we generate in _getNewOutpuName.
  String _getOutputNamePattern(String outputName) {
    final List<String> nameParts = _getOutputNameParts(outputName);
    return [nameParts[0] + '(\\.\\d+)?', nameParts[nameParts.length - 1]]
        .join('.');
  }

  // We need to rename the output file name so that mojo can create a new mojo
  // application, instead of connecting to mojo app at the old url.
  Future<String> _renameOutputInOurDir(String outputName) async {
    String newOutputName = _getNewOutpuName(outputName);
    File outputFile = new File(path.join(_environment.buildDir, outputName));
    if (!await outputFile.exists()) {
      throw 'File $outputName not found in ${_environment.buildDir}';
    }
    outputFile.renameSync(path.join(_environment.buildDir, newOutputName));
    return newOutputName;
  }

  Future<Map<String, String>> _generateJsonForReload(
      String outputName, String newOutputName) async {
    Map<String, String> response = <String, String>{};

    Uri moduleUrl;
    await parallelStream(_environment.projectConfigurations,
        (final ProjectConfiguration projectConfiguration) async {
      if (moduleUrl != null) {
        return 0;
      }

      final File manifestFile = new File(
          path.join(projectConfiguration.projectRoot, 'manifest.yaml'));
      if (!manifestFile.existsSync()) {
        return 0;
      }
      final manifestContent = await manifestFile.readAsString();
      final manifest = loadYaml(manifestContent);
      if (manifest['url'] == null) {
        throw 'Manifest found without any url field.';
      }
      if (manifest['url'].contains(outputName)) {
        response['url_pattern'] = _getOutputNamePattern(outputName);
        final String newManifestContent =
            manifestContent.replaceFirst(outputName, newOutputName);
        response['manifest'] = newManifestContent;
      }
      return 0;
    });

    if (response.isEmpty) {
      throw 'No reload json generated.';
    }
    return response;
  }

  Future<int> _reloadModuleThroughDebugServer(
      Map<String, String> reloadJson) async {
    Uri uri = Uri.parse('http://localhost:1842/handler');

    HttpClient httpClient = new HttpClient();
    HttpClientRequest request = await httpClient.postUrl(uri);

    request.headers.contentType =
        new ContentType("application", "json", charset: "utf-8");
    request.write(JSON.encode(reloadJson));
    HttpClientResponse response = await request.close();
    if (response.statusCode != 200) {
      throw new Exception(response.reasonPhrase);
    }
    return 0;
  }
}
