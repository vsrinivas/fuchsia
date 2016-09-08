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

  Future<String> createPushSubscription(
      String subscriptionName, String pushEndpoint) {
    final pubsub_api.PushConfig pushConfig = new pubsub_api.PushConfig()
      ..pushEndpoint = pushEndpoint;
    return _api.projects.subscriptions
        .create(
            new pubsub_api.Subscription()
              ..topic = topicName
              ..pushConfig = pushConfig,
            subscriptionName)
        .then((pubsub_api.Subscription subscription) =>
            subscription?.pushConfig?.pushEndpoint);
  }

  Future<String> publish(String data) => _api.projects.topics
      .publish(
          new pubsub_api.PublishRequest()
            ..messages = [
              new pubsub_api.PubsubMessage()..dataAsBytes = data.codeUnits
            ],
          topicName)
      .then((pubsub_api.PublishResponse response) => response.messageIds.first);
}

class _StorageObjectSink implements StreamSink<List<int>> {
  final StreamController<List<int>> _controller =
      new StreamController<List<int>>();
  final Completer _doneCompleter = new Completer();

  _StorageObjectSink(
      storage_api.StorageApi api, String bucketName, String objectName,
      {String generation}) {
    api.objects
        .insert(null, bucketName,
            name: objectName,
            ifGenerationMatch: generation,
            uploadMedia: new storage_api.Media(_controller.stream, null),
            uploadOptions: storage_api.UploadOptions.Resumable)
        .then((storage_api.Object object) {
      _doneCompleter.complete(object.generation);
    }, onError: (errorEvent, [StackTrace stackTrace]) {
      _doneCompleter.completeError(errorEvent, stackTrace);
    });
  }

  void add(List<int> event) {
    _controller.add(event);
  }

  void addError(errorEvent, [StackTrace stackTrace]) {
    _controller.addError(errorEvent, stackTrace);
  }

  Future addStream(Stream<List<int>> stream) {
    return _controller.addStream(stream);
  }

  Future get done => _doneCompleter.future;

  Future close() {
    _controller.close();
    return done;
  }
}

class StorageBucketWrapper {
  /// Auth scopes required to use all methods in [StorageBucketWrapper].
  static const List<String> scopes = const [
    storage_api.StorageApi.DevstorageReadWriteScope
  ];

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
      _api.objects
          .insert(null, bucketName,
              name: objectName,
              ifGenerationMatch: generation,
              uploadMedia: new storage_api.Media(
                  new Stream.fromIterable([bytes]), bytes.length))
          .then((storage_api.Object object) => object.generation);

  /// Returns a [StreamSink] that writes back to [objectName].
  ///
  /// This method is best used for files with unknown size or larger files with
  /// size greater than 5MB.
  StreamSink<List<int>> writeObject(String objectName, {String generation}) =>
      new _StorageObjectSink(_api, bucketName, objectName,
          generation: generation);
}
