// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:io';
import 'dart:convert';

import 'package:appengine/appengine.dart';
import 'package:gcloud/storage.dart';

// TODO(victorkwan): Swap this out once it is exposed by a 'public' API,
import 'package:_discoveryapis_commons/_discoveryapis_commons.dart';

import 'package:indexer_pipeline/index.dart';
import 'package:indexer_pipeline/render_html.dart';
import 'package:indexer_pipeline/render_json.dart';
import 'package:parser/manifest.dart';
import 'package:parser/parse_error.dart';
import 'package:path/path.dart' as path;

Future<dynamic> readJsonStream(Stream<List<int>> stream) =>
    stream.transform(UTF8.decoder).transform(JSON.decoder).single;

/// Returns a manifest with URIs replaced with versioned URIs.
Manifest updateManifestUri(
    Manifest manifest, String bucketName, String arch, String revision) {
  final String tqPath = 'https://tq.mojoapps.io';
  final String versionedUrl = 'https://storage.googleapis.com/$bucketName/'
      'services/$arch/$revision/';

  // We assume that all URIs are specified relative to https://tq.mojoapps.io/.
  String relativeUrl = path.relative(manifest.url.toString(), from: tqPath);
  Uri uri = Uri.parse(path.join(versionedUrl, relativeUrl));

  Uri icon;
  if (manifest.icon != null) {
    String relativeIcon = path.relative(manifest.icon.toString(), from: tqPath);
    icon = Uri.parse(path.join(versionedUrl, relativeIcon));
  }

  return new Manifest(manifest.title, uri, manifest.use, manifest.verb,
      manifest.input, manifest.output, manifest.display, manifest.compose,
      icon: icon, themeColor: manifest.themeColor, schemas: manifest.schemas);
}

/// Updates the appropriate index given a bucket and a new (or updated)
/// manifest.
Future<Null> updateIndex(HttpRequest request, Bucket bucket, Logging logging,
    String manifestPath, String manifestArch, String manifestRevision) async {
  const String jsonIndexFilename = 'index.json';
  const String htmlIndexFilename = 'index.html';

  final Index index = new Index();

  // We normalize in the case that the manifest is located at the root, in which
  // case the dirname is returned as '.'.
  final String jsonIndexPath =
      path.normalize(path.join(path.dirname(manifestPath), jsonIndexFilename));
  final String htmlIndexPath =
      path.normalize(path.join(path.dirname(manifestPath), htmlIndexFilename));

  try {
    logging.info('Fetching index: ${jsonIndexPath}');
    String jsonIndex = await UTF8.decodeStream(bucket.read(jsonIndexPath));
    index.addJsonIndex(jsonIndex);
  } on DetailedApiRequestError catch (e) {
    if (e.status != HttpStatus.NOT_FOUND) {
      // Only when the index does not exist, i.e. NOT_FOUND returned, should we
      // create a new index and continue with populating the index. In all other
      // cases, we return to the task queue.
      logging.error('Fetching index failed (${e.status}). '
          'Returning to task queue.');
      request.response
        ..statusCode = HttpStatus.INTERNAL_SERVER_ERROR
        ..close();
      return;
    }

    logging.info('Fetching index failed (${e.status}). Creating new index.');
  }

  try {
    logging.info('Fetching manifest: ${manifestPath}');
    String yamlManifest = await UTF8.decodeStream(bucket.read(manifestPath));
    Manifest updatedManifest = updateManifestUri(
        new Manifest.parseYamlString(yamlManifest),
        bucket.bucketName,
        manifestArch,
        manifestRevision);
    logging.info('Adding manifest to the index.');
    index.addParsedManifest(updatedManifest);
  } on DetailedApiRequestError catch (e) {
    // If the manifest cannot be found, we assume that it has been removed some
    // time since the notification was dispatched. In which case, we do not
    // attempt to update the index again.
    int statusCode;
    if (e.status == HttpStatus.NOT_FOUND) {
      logging.error('Fetching manifest failed (${e.status}). Bailing out.');
      statusCode = HttpStatus.OK;
    } else {
      logging.error('Fetching manifest failed (${e.status}). '
          'Returning to task queue.');
      statusCode = HttpStatus.INTERNAL_SERVER_ERROR;
    }

    request.response
      ..statusCode = statusCode
      ..close();
    return;
  } on ParseError {
    // TODO(victorkwan): Find some way to provide feedback to the developer.
    logging.error('Adding manifest failed: parsing error. Bailing out.');
    request.response
      ..statusCode = HttpStatus.OK
      ..close();
    return;
  }

  String updatedJsonIndex = renderJsonIndex(index);
  String updatedHtmlIndex = renderHtmlIndex(index);

  try {
    logging.info('Writing index files back to cloud storage.');
    await bucket.writeBytes(jsonIndexPath, updatedJsonIndex.codeUnits);
    await bucket.writeBytes(htmlIndexPath, updatedHtmlIndex.codeUnits);
  } on DetailedApiRequestError catch (e) {
    logging.error('Writing index files failed (${e.status}). '
        'Returning to task queue.');
    request.response
      ..statusCode = HttpStatus.INTERNAL_SERVER_ERROR
      ..close();
    return;
  }

  request.response
    ..statusCode = HttpStatus.OK
    ..close();
}

/// Handles a request from the 'indexing' task queue.
///
/// Currently, we assume that there is no notion of deleting a module: once a
/// manifest is removed, we have no means of determining the URL the module is
/// indexed against.
Future<Null> requestHandler(HttpRequest request) async {
  const String modularQueueName = 'indexing';

  // The logging object can only be retrieved within an App Engine context.
  final Logging logging = context.services.logging;

  if (request.method != 'POST') {
    request.response
      ..statusCode = HttpStatus.NOT_FOUND
      ..close();
    return;
  }

  String queueName = request.headers.value('X-AppEngine-QueueName');
  if (queueName != modularQueueName) {
    // If null, this is a malformed request. If the queue name is incorrect,
    // the request is not relevant to this handler. In either case, we don't
    // want to receive any more notifications.
    logging.error('Invalid queue name: $queueName');
    request.response
      ..statusCode = HttpStatus.OK
      ..close();
    return;
  }

  Map<String, String> manifestNotification = await readJsonStream(request);
  if (manifestNotification['resource_state'] == 'not_exists') {
    // We currently do not support the deletion of modules. Like before, we send
    // OK to stop receiving notifications.
    logging.info('Unsupported resource state: \'not_exists\'');
    request.response
      ..statusCode = HttpStatus.OK
      ..close();
    return;
  }

  // Parsing manifest attributes.
  final String manifestPath = manifestNotification['name'];
  final String manifestArch = manifestNotification['arch'];
  final String manifestRevision = manifestNotification['revision'];

  // Retrieving the appropriate bucket.
  final Storage storage = context.services.storage;
  final Bucket bucket = storage.bucket(Platform.environment['BUCKET_NAME']);

  await updateIndex(
      request, bucket, logging, manifestPath, manifestArch, manifestRevision);
}
