// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:io';
import 'dart:convert';

import 'package:gcloud/service_scope.dart' as ss;
import 'package:googleapis/storage/v1.dart' as storage_api;
import 'package:http/http.dart' as http;
import 'package:indexer_pipeline/index.dart';
import 'package:logging/logging.dart';
import 'package:indexer_pipeline/render_html.dart';
import 'package:indexer_pipeline/render_json.dart';
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
Manifest updateManifestUri(
    Manifest manifest, String bucketName, String arch, String revision) {
  final String tqPath = 'https://tq.mojoapps.io';
  final String versionedUrl = 'https://storage.googleapis.com/$bucketName/'
      'services/$arch/$revision/';

  String relativeUrl = path.relative(manifest.url.toString(), from: tqPath);
  Uri uri = Uri.parse(path.join(versionedUrl, relativeUrl));

  Uri icon;
  if (manifest.icon != null) {
    String relativeIcon =
        path.relative(manifest.icon.toString(), from: tqPath);
    icon = Uri.parse(path.join(versionedUrl, relativeIcon));
  }

  return new Manifest(manifest.title, uri, manifest.use, manifest.verb,
      manifest.input, manifest.output, manifest.display, manifest.compose,
      icon: icon, themeColor: manifest.themeColor, schemas: manifest.schemas);
}

class IndexUpdater {
  final String bucketName;
  final storage_api.StorageApi _api;

  IndexUpdater(http.Client client, this.bucketName)
      : _api = new storage_api.StorageApi(client);

  IndexUpdater.fromApi(this._api, this.bucketName);

  Future<storage_api.Object> _getObjectResource(String objectName) =>
      _api.objects.get(bucketName, objectName, projection: 'full');

  Future<storage_api.Media> _getObjectData(String objectName,
          {String generation}) =>
      _api.objects.get(bucketName, objectName,
          downloadOptions: storage_api.DownloadOptions.FullMedia,
          ifGenerationMatch: generation);

  Future<storage_api.Object> _insertObjectData(
          String objectName, storage_api.Media objectData,
          {String generation}) =>
      _api.objects.insert(null, bucketName,
          ifGenerationMatch: generation,
          name: objectName,
          uploadMedia: objectData,
          downloadOptions: storage_api.DownloadOptions.Metadata);

  /// Updates the index given some [manifestName] to be indexed.
  ///
  /// We assume that for some given manifest with name
  /// services/<arch>/<revision>/manifest.yaml, the index should be located in
  /// services/<arch>/<revision>/index.json.
  Future<Null> update(String manifestName, String arch, String revision) async {
    final Index index = new Index();
    final String jsonIndexName =
        path.normalize(path.join(path.dirname(manifestName), 'index.json'));
    final String htmlIndexName =
        path.normalize(path.join(path.dirname(manifestName), 'index.html'));

    storage_api.Object indexResource;
    try {
      _logger.info('Fetching index from cloud storage.');
      indexResource = await _getObjectResource(jsonIndexName);
      storage_api.Media indexData = await _getObjectData(jsonIndexName,
          generation: indexResource.generation);
      index.addJsonIndex(await UTF8.decodeStream(indexData.stream));
    } on storage_api.DetailedApiRequestError catch (e) {
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

    try {
      _logger.info('Fetching manifest from cloud storage.');
      storage_api.Media manifestData = await _getObjectData(manifestName);
      String yamlManifest = await UTF8.decodeStream(manifestData.stream);
      Manifest updatedManifest = updateManifestUri(
          new Manifest.parseYamlString(yamlManifest),
          bucketName,
          arch,
          revision);

      _logger.info('Updating index.');
      index.addParsedManifest(updatedManifest);
    } on storage_api.DetailedApiRequestError catch (e) {
      switch (e.status) {
        case HttpStatus.NOT_FOUND:
          throw _manifestException('Manifest $manifestName not found.');
        default:
          throw _cloudStorageFailureException(
              'Manifest $manifestName could not be fetched.', e.status);
      }
    } on ParseError {
      throw _manifestException('Manifest $manifestName was malformed');
    }

    List<int> updatedJsonIndex = renderJsonIndex(index).codeUnits;
    storage_api.Media updatedJsonIndexData = new storage_api.Media(
        new Stream.fromIterable([updatedJsonIndex]), updatedJsonIndex.length);
    try {
      // If the indexResource is null, this means that the index did not exist.
      // In which case, we need to guarantee that the file does not exist when
      // we write back to cloud storage - this is made possible with '0'.
      _logger.info('Writing JSON index back to cloud storage.');
      await _insertObjectData(jsonIndexName, updatedJsonIndexData,
          generation: indexResource?.generation ?? '0');
    } on storage_api.DetailedApiRequestError catch (e) {
      if (e.status == HttpStatus.PRECONDITION_FAILED) {
        throw _atomicUpdateFailureException('Index changed during updating.');
      }

      throw _cloudStorageFailureException(
          'Index could not be uploaded', e.status);
    }

    List<int> updatedHtmlIndex = renderHtmlIndex(index).codeUnits;
    storage_api.Media updatedHtmlIndexData = new storage_api.Media(
        new Stream.fromIterable([updatedHtmlIndex]), updatedHtmlIndex.length);
    try {
      // TODO(victorkwan): In the future, we would want to expose the HTML
      // index by generating it on a separate endpoint. For now, we
      // optimistically update the HTML index in cloud storage.
      _logger.info('Writing HTML index back to cloud storage.');
      await _insertObjectData(htmlIndexName, updatedHtmlIndexData);
    } on storage_api.DetailedApiRequestError catch (e) {
      _logger.warning(
          'HTML index could not be updated, but JSON index is up to date '
          '(${e.status}).');
    }
  }
}
