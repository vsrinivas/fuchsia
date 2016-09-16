// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:googleapis/pubsub/v1.dart' as pubsub_api;
import 'package:googleapis/storage/v1.dart' as storage_api;
import 'package:http/http.dart' as http;

export 'package:_discoveryapis_commons/_discoveryapis_commons.dart'
    show DetailedApiRequestError;
export 'package:googleapis/pubsub/v1.dart' show PubsubMessage, ReceivedMessage;

class PubSubTopicWrapper {
  /// Auth scopes required to use all methods in [PubSubTopicWrapper].
  static const List<String> scopes = const [pubsub_api.PubsubApi.PubsubScope];

  final pubsub_api.PubsubApi _api;
  final String topicName;

  PubSubTopicWrapper(http.Client client, this.topicName)
      : _api = new pubsub_api.PubsubApi(client);

  PubSubTopicWrapper.fromApi(this._api, this.topicName);

  Future<Null> createSubscription(String subscriptionName,
      {String pushEndpoint}) {
    final pubsub_api.PushConfig pushConfig =
        pushEndpoint != null ? new pubsub_api.PushConfig() : null;
    pushConfig?.pushEndpoint = pushEndpoint;
    return _api.projects.subscriptions
        .create(
            new pubsub_api.Subscription()
              ..topic = topicName
              ..pushConfig = pushConfig,
            subscriptionName)
        .then((_) => null);
  }

  Future<List<pubsub_api.ReceivedMessage>> pull(
      String subscriptionName, int maxMessages) {
    final pubsub_api.PullRequest request = new pubsub_api.PullRequest()
      ..maxMessages = maxMessages;
    return _api.projects.subscriptions.pull(request, subscriptionName).then(
        (pubsub_api.PullResponse response) => response.receivedMessages ?? []);
  }

  Future<Null> acknowledge(List<String> ackIds, String subscriptionName) =>
      _api.projects.subscriptions
          .acknowledge(new pubsub_api.AcknowledgeRequest()..ackIds = ackIds,
              subscriptionName)
          .then((_) => null);

  Future<String> publish(String data) => _api.projects.topics
      .publish(
          new pubsub_api.PublishRequest()
            ..messages = [
              new pubsub_api.PubsubMessage()..dataAsBytes = data.codeUnits
            ],
          topicName)
      .then((pubsub_api.PublishResponse response) => response.messageIds.first);
}

/// A [StreamSink] for bytes that provides piping behavior depending on length.
///
/// For example, the [ThresholdSink] can be used for file uploads. If the length
/// is shorter than a given threshold, a normal upload is preferable to a
/// resumable upload, which would require additional requests.
class ThresholdSink implements StreamSink<List<int>> {
  // The backing StreamController that implements the StreamSink interface.
  final StreamController<List<int>> _inputController =
      new StreamController<List<int>>(sync: true);

  // The StreamController that provides a stream to upload.
  final StreamController<List<int>> _uploadController =
      new StreamController<List<int>>();

  final Completer _doneCompleter = new Completer();
  bool _hasActivatedThreshold = false;
  int _currentLength = 0;

  ThresholdSink(
      int threshold,
      Future onThreshold(Stream<List<int>> stream, int currentLength),
      Future onBelowThreshold(Stream<List<int>> stream, int currentLength)) {
    _inputController.stream.listen((List<int> data) {
      _uploadController.add(data);
      _currentLength += data.length;
      if (!_hasActivatedThreshold && _currentLength > threshold) {
        _hasActivatedThreshold = true;
        onThreshold(_uploadController.stream, _currentLength).then((value) {
          _doneCompleter.complete(value);
        }, onError: (error, StackTrace stackTrace) {
          _doneCompleter.completeError(error, stackTrace);
        });
      }
    }, onError: (error, StackTrace stackTrace) {
      _uploadController.addError(error, stackTrace);
    }, onDone: () {
      if (!_hasActivatedThreshold) {
        onBelowThreshold(_uploadController.stream, _currentLength).then(
            (value) {
          _doneCompleter.complete(value);
        }, onError: (error, StackTrace stackTrace) {
          _doneCompleter.completeError(error, stackTrace);
        });
      }
    });
  }

  void add(List<int> event) {
    _inputController.add(event);
  }

  void addError(errorEvent, [StackTrace stackTrace]) {
    _inputController.addError(errorEvent, stackTrace);
  }

  Future addStream(Stream<List<int>> stream) {
    return _inputController.addStream(stream);
  }

  Future get done => _doneCompleter.future;

  Future close() async {
    await _inputController.close();
    await _uploadController.close();
    return await done;
  }
}

class StorageBucketWrapper {
  /// Auth scopes required to use all methods in [StorageBucketWrapper].
  static const List<String> scopes = const [
    storage_api.StorageApi.DevstorageReadWriteScope
  ];

  static const int resumableThreshold = 5 * 1024 * 1024;

  final storage_api.StorageApi _api;
  final String bucketName;

  StorageBucketWrapper(http.Client client, this.bucketName)
      : _api = new storage_api.StorageApi(client);

  StorageBucketWrapper.fromApi(this._api, this.bucketName);

  Future<String> getObjectGeneration(String objectName) => _api.objects
      .get(bucketName, objectName, projection: 'full')
      .then((storage_api.Object object) => object.generation);

  Stream<List<int>> readObject(String objectName, {String generation}) async* {
    final storage_api.Media media = await _api.objects.get(
        bucketName, objectName,
        ifGenerationMatch: generation,
        downloadOptions: storage_api.DownloadOptions.FullMedia);
    yield* media.stream;
  }

  Future<String> writeObjectAsBytes(String objectName, List<int> bytes,
          {String generation}) =>
      new Stream.fromIterable([bytes])
          .pipe(writeObject(objectName, generation: generation));

  /// Returns a [StreamSink] that writes back to [objectName].
  StreamSink<List<int>> writeObject(String objectName, {String generation}) {
    Future<String> normalUpload(Stream<List<int>> stream, int currentLength) =>
        _api.objects
            .insert(null, bucketName,
                name: objectName,
                ifGenerationMatch: generation,
                uploadMedia: new storage_api.Media(stream, currentLength))
            .then((storage_api.Object object) => object.generation);
    Future<String> resumableUpload(
            Stream<List<int>> stream, int currentLength) =>
        _api.objects
            .insert(null, bucketName,
                name: objectName,
                ifGenerationMatch: generation,
                uploadMedia: new storage_api.Media(stream, null),
                uploadOptions: storage_api.UploadOptions.Resumable)
            .then((storage_api.Object object) => object.generation);
    return new ThresholdSink(resumableThreshold, resumableUpload, normalUpload);
  }
}
