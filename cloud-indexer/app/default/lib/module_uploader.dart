// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:io';

import 'package:cloud_indexer_common/wrappers.dart';
import 'package:gcloud/service_scope.dart' as ss;
import 'package:http/http.dart' as http;
import 'package:logging/logging.dart';
import 'package:parser/manifest.dart';
import 'package:parser/parse_error.dart';
import 'package:path/path.dart' as path;

import 'tarball.dart';

final Logger _logger = new Logger('cloud_indexer.module_uploader');

const Symbol _moduleUploaderKey = #moduleUploader;

ModuleUploader get moduleUploaderService => ss.lookup(_moduleUploaderKey);

void registerModuleUploaderService(ModuleUploader moduleUploader) {
  ss.register(_moduleUploaderKey, moduleUploader);
}

class CloudStorageException implements Exception {
  final int statusCode;
  final String message;
  CloudStorageException(this.statusCode, this.message);
  String toString() => 'CloudStorageException: $message';
}

class PubSubException implements Exception {
  final int statusCode;
  final String message;
  PubSubException(this.statusCode, this.message);
  String toString() => 'PubSubException: $message';
}

CloudStorageException _cloudStorageException(int statusCode, String message) {
  CloudStorageException e = new CloudStorageException(statusCode, message);
  _logger.warning(e.toString());
  return e;
}

PubSubException _pubSubException(int statusCode, String message) {
  PubSubException e = new PubSubException(statusCode, message);
  _logger.warning(e.toString());
  return e;
}

TarballException _tarballException(String message) {
  TarballException e = new TarballException(message);
  _logger.warning(e.toString());
  return e;
}

/// Handles the uploading of modules to cloud storage.
///
/// The [ModuleUploader] processes uploads in three steps.
/// 1. First, it ensures the validity of the tarball uploaded;
/// 2. Second, it copies files to cloud storage, preserving directory structure;
/// 3. Finally, it uses Pub/Sub to invoke an index update.
class ModuleUploader {
  static const String _manifestPath = 'manifest.yaml';
  static const String _defaultTopicName =
      'projects/google.com:modular-cloud-indexer/topics/indexing';

  final PubSubTopicWrapper _pubSubTopicWrapper;
  final StorageBucketWrapper _storageBucketWrapper;

  ModuleUploader(this._pubSubTopicWrapper, this._storageBucketWrapper);

  /// Creates an instance of a [ModuleUploader].
  ///
  /// Note that the [ModuleUploader] requires that it is instantiated within a
  /// gcloud service scope with access to the Pub/Sub and Storage services.
  factory ModuleUploader.fromClient(http.Client client,
      {String topicName: _defaultTopicName, String bucketName}) {
    // In the case these are not set, we use default values from the service
    // scope and environment.
    bucketName ??= Platform.environment['BUCKET_NAME'];
    return new ModuleUploader(new PubSubTopicWrapper(client, topicName),
        new StorageBucketWrapper(client, bucketName));
  }

  static String storageDestinationPath(
          String arch, String modularRevision, String file) =>
      path.join('services', arch, modularRevision, file);

  Future<Null> processUpload(Stream<List<int>> data) =>
      withTarball(data, processTarball);

  Future<Null> processTarball(Tarball tarball) async {
    Set<String> files = await tarball
        .list()
        .where((String file) => !file.endsWith('/'))
        .toSet();

    if (!files.contains(_manifestPath)) {
      throw _tarballException(
          'Manifest: $_manifestPath was missing in tarball.');
    }

    String yamlManifest = await tarball.readAsString(_manifestPath);
    Manifest manifest;
    try {
      manifest = new Manifest.parseYamlString(yamlManifest);
      if (manifest.arch == null || manifest.modularRevision == null) {
        throw _tarballException(
            'Manifest: $_manifestPath was missing its `arch` and/or '
            '`modularRevision` field(s).');
      }

      _logger.info('Parsed manifest with title: ${manifest.title}');
    } on ParseError {
      throw _tarballException(
          'Manifest: $_manifestPath was invalid in tarball.');
    }

    // Copy all files into cloud storage, preserving directory structure.
    for (String file in files) {
      if (file == _manifestPath) continue;
      String destinationPath =
          storageDestinationPath(manifest.arch, manifest.modularRevision, file);
      try {
        await tarball
            .openRead(file)
            .pipe(_storageBucketWrapper.writeObject(destinationPath));
      } on DetailedApiRequestError catch (e) {
        throw _cloudStorageException(e.status, 'Failed to write file: $file');
      }
    }

    try {
      // Finally, we attempt to publish to the indexing topic.
      await _pubSubTopicWrapper.publish(manifest.toJsonString());
    } on DetailedApiRequestError catch (e) {
      throw _pubSubException(e.status, 'Failed to publish indexing task');
    }
  }
}
