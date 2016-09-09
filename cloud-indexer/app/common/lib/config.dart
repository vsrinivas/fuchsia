// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:gcloud/service_scope.dart' as ss;
import 'package:googleapis_auth/auth_io.dart' as auth_io;
import 'package:path/path.dart' as path;
import 'package:http/http.dart' as http;

const Symbol _configServiceKey = #configService;

Config get configService => ss.lookup(_configServiceKey);

void registerConfigService(Config config) {
  ss.register(_configServiceKey, config);
}

Future<Map<dynamic, dynamic>> jsonFromStream(Stream<List<int>> data) =>
    data.transform(UTF8.decoder).transform(JSON.decoder).single;

abstract class Config {
  /// A [Client] representing a service account for the Google Cloud project.
  final http.Client cloudPlatformClient;

  /// Factory method that produces a [Config], depending on the environment.
  ///
  /// We assume that in the development environment, the `SERVER_SOFTWARE`
  /// environment variable is not set.
  static Future<Config> create() =>
      Platform.environment['SERVER_SOFTWARE'] != null
          ? CloudPlatformConfig.createFromMetadataServer()
          : ShelfConfig.createFromConfigFile();

  Config._(this.cloudPlatformClient);

  /// The bucket containing configuration files pertinent to the cloud indexer.
  ///
  /// We currently expect that the bucket contains an auth key with READ
  /// permissions for the group members of mojodeveloppers@mojoapps.io. This
  /// key is to be stored in the file `auth/key.json`.
  String get indexerBucketName;

  String get moduleBucketName;
  String get topicName;
}

class CloudPlatformConfig extends Config {
  /// Creates a [CloudPlatformConfig] from the metadata server [Client].
  static Future<CloudPlatformConfig> createFromMetadataServer() async =>
      new CloudPlatformConfig(await auth_io.clientViaMetadataServer());

  CloudPlatformConfig(http.Client cloudPlatformClient)
      : super._(cloudPlatformClient);

  String get indexerBucketName => Platform.environment['INDEXER_BUCKET_NAME'];
  String get moduleBucketName => Platform.environment['MODULE_BUCKET_NAME'];
  String get topicName => Platform.environment['TOPIC_NAME'];
}

class ShelfConfig extends Config {
  static final RegExp _configRegExp = new RegExp(r'^(.*cloud-indexer).*$');
  static String get configPath {
    final String configDir =
        _configRegExp.matchAsPrefix(Platform.script.toFilePath()).group(1);
    return path.join(configDir, 'config.json');
  }
  static String get keyPath =>
      path.join(Platform.environment['HOME'], '.modular_cloud_indexer_key');

  final String indexerBucketName;
  final String moduleBucketName;
  final String topicName;

  /// Creates a [ShelfConfig] from a configuration file.
  ///
  /// The configuration file should be located at the root of the cloud-indexer
  /// directory. It should contain the relevant environment variables, as well
  /// as the path to the key file.
  static Future<ShelfConfig> createFromConfigFile() async {
    final Map<String, dynamic> config =
        await jsonFromStream(new File(configPath).openRead());

    // We retrieve the key file from the file system.
    final auth_io.ServiceAccountCredentials credentials =
        new auth_io.ServiceAccountCredentials.fromJson(
            await jsonFromStream(new File(keyPath).openRead()));
    final auth_io.AuthClient client = await auth_io.clientViaServiceAccount(
        credentials, const ['https://www.googleapis.com/auth/cloud-platform']);

    return new ShelfConfig(client, config['testing']['indexerBucketName'],
        config['testing']['moduleBucketName'], config['testing']['topicName']);
  }

  ShelfConfig(http.Client cloudPlatformClient, this.indexerBucketName,
      this.moduleBucketName, this.topicName)
      : super._(cloudPlatformClient);
}
