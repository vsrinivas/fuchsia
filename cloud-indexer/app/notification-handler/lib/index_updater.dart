// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:io';
import 'dart:convert';

import 'package:cloud_indexer_common/config.dart';
import 'package:cloud_indexer_common/wrappers.dart';
import 'package:gcloud/service_scope.dart' as ss;
import 'package:indexer_pipeline/index.dart';
import 'package:indexer_pipeline/render_html.dart';
import 'package:indexer_pipeline/render_json.dart';
import 'package:logging/logging.dart';
import 'package:parser/manifest.dart';
import 'package:parser/parse_error.dart';
import 'package:path/path.dart' as path;

final Logger _logger = new Logger('notification_handler.index_updater');

// Set-up for exposing the IndexUpdater to the service scope.
const Symbol _indexUpdaterKey = #indexUpdater;

IndexUpdater get indexUpdaterService => ss.lookup(_indexUpdaterKey);

void registerIndexUpdaterService(IndexUpdater indexUpdater) {
  ss.register(_indexUpdaterKey, indexUpdater);
}

class AtomicUpdateFailureException implements Exception {
  final String message;

  AtomicUpdateFailureException(this.message);

  String toString() => 'AtomicUpdateFailureException: $message';
}

class CloudStorageFailureException implements Exception {
  final String message;
  final int statusCode;

  CloudStorageFailureException(this.message, this.statusCode);

  String toString() => 'CloudStorageFailureException ($statusCode): $message';
}

class ManifestException implements Exception {
  final String message;

  ManifestException(this.message);

  String toString() => 'ManifestException: $message';
}

AtomicUpdateFailureException _atomicUpdateFailureException(String message) {
  AtomicUpdateFailureException e = new AtomicUpdateFailureException(message);
  _logger.warning(e.toString());
  return e;
}

CloudStorageFailureException _cloudStorageFailureException(
    String message, int statusCode) {
  CloudStorageFailureException e =
      new CloudStorageFailureException(message, statusCode);
  _logger.warning(e.toString());
  return e;
}

ManifestException _manifestException(String message) {
  ManifestException e = new ManifestException(message);
  _logger.warning(e.toString());
  return e;
}

/// Takes a [manifest] and updates its URLs to point to cloud storage objects.
///
/// We assume that all URLs are prefixed with 'https://tq.mojoapps.io' and
/// should be replaced with a URL in
/// 'https://storage.googleapis.com/$bucketName/services/$arch/$revision/'.
Manifest updateManifestUri(Manifest manifest, String bucketName) {
  final String tqPath = 'https://tq.mojoapps.io';
  final String versionedUrl = 'https://storage.googleapis.com/$bucketName/'
      'services/${manifest.arch}/${manifest.modularRevision}/';

  String relativeUrl = path.relative(manifest.url.toString(), from: tqPath);
  Uri uri = Uri.parse(path.join(versionedUrl, relativeUrl));

  Uri icon;
  if (manifest.icon != null) {
    String relativeIcon = path.relative(manifest.icon.toString(), from: tqPath);
    icon = Uri.parse(path.join(versionedUrl, relativeIcon));
  }

  return new Manifest(manifest.title, uri, manifest.use, manifest.verb,
      manifest.input, manifest.output, manifest.display, manifest.compose,
      icon: icon,
      themeColor: manifest.themeColor,
      schemas: manifest.schemas,
      arch: manifest.arch,
      modularRevision: manifest.modularRevision);
}

class IndexUpdater {
  final StorageBucketWrapper _storageBucketWrapper;

  IndexUpdater(this._storageBucketWrapper);

  IndexUpdater.fromServiceScope()
      : _storageBucketWrapper = new StorageBucketWrapper(
            configService.cloudPlatformClient, configService.moduleBucketName);

  static String storageDestinationPath(
          String arch, String modularRevision, String file) =>
      path.join('services', arch, modularRevision, file);

  /// Updates the index given some [jsonManifest] to be indexed.
  Future<Null> update(String jsonManifest) async {
    Manifest manifest;
    try {
      manifest = new Manifest.fromJsonString(jsonManifest);
      if (manifest.arch == null || manifest.modularRevision == null) {
        throw _manifestException(
            'Manifest did not have `arch` or `modularRevision` fields.');
      }
    } on ParseError {
      throw _manifestException('Error parsing manifest.');
    } on FormatException {
      // Currently, some JSON-related exceptions are not handled by
      // the Manifest.fromJsonString method.
      throw _manifestException('Error parsing manifest.');
    }

    final Index index = new Index();
    final String jsonIndexName = storageDestinationPath(
        manifest.arch, manifest.modularRevision, 'index.json');
    final String htmlIndexName = storageDestinationPath(
        manifest.arch, manifest.modularRevision, 'index.html');

    String indexGeneration;
    try {
      _logger.info('Fetching index from cloud storage.');
      indexGeneration =
          await _storageBucketWrapper.getObjectGeneration(jsonIndexName);
      index.addJsonIndex(await UTF8.decodeStream(_storageBucketWrapper
          .readObject(jsonIndexName, generation: indexGeneration)));
    } on DetailedApiRequestError catch (e) {
      switch (e.status) {
        case HttpStatus.NOT_FOUND:
          // If the index cannot be found, it's likely it didn't exist. In which
          // case, we can create a new one.
          break;
        case HttpStatus.PRECONDITION_FAILED:
          throw _atomicUpdateFailureException('Index changed during fetching.');
        default:
          throw _cloudStorageFailureException(
              'Index could not be fetched.', e.status);
      }
    }

    // We add the manifest *after* the older index. This way, the manifest
    // will overwrite its entry in the index.
    Manifest updatedManifest =
        updateManifestUri(manifest, _storageBucketWrapper.bucketName);
    index.addParsedManifest(updatedManifest);

    List<int> updatedJsonIndex = renderJsonIndex(index).codeUnits;
    try {
      // If the indexGeneration is null, the index did not exist. In which case,
      // we need to guarantee that the file does not exist when we write back to
      // cloud storage - this is made possible with '0'.
      _logger.info('Writing JSON index back to cloud storage.');
      await _storageBucketWrapper.writeObjectAsBytes(
          jsonIndexName, updatedJsonIndex,
          generation: indexGeneration ?? '0');
    } on DetailedApiRequestError catch (e) {
      if (e.status == HttpStatus.PRECONDITION_FAILED) {
        throw _atomicUpdateFailureException('Index changed during updating.');
      }

      throw _cloudStorageFailureException(
          'Index could not be uploaded', e.status);
    }

    List<int> updatedHtmlIndex = renderHtmlIndex(index).codeUnits;
    try {
      // TODO(victorkwan): In the future, we would want to expose the HTML
      // index by generating it on a separate endpoint. For now, we
      // optimistically update the HTML index in cloud storage.
      _logger.info('Writing HTML index back to cloud storage.');
      await _storageBucketWrapper.writeObjectAsBytes(
          htmlIndexName, updatedHtmlIndex);
    } on DetailedApiRequestError catch (e) {
      _logger.warning(
          'HTML index could not be updated, but JSON index is up to date '
          '(${e.status}).');
    }
  }
}
