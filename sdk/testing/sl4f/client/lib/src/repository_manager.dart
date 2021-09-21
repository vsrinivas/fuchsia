// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:meta/meta.dart';
import 'sl4f_client.dart';

class RepositoryConfig {
  final String repoUrl;
  final List<KeyConfig> rootKeys;
  final List<MirrorConfig> mirrors;
  String updatePackageUrl;
  int rootVersion;
  int rootThreshold;

  RepositoryConfig(this.repoUrl, this.rootKeys, this.mirrors);

  factory RepositoryConfig.fromJson(Map<String, dynamic> json) {
    final List<KeyConfig> rootKeys = <KeyConfig>[];
    for (final k in json['root_keys']) {
      rootKeys.add(KeyConfig.fromJson(k));
    }
    final List<MirrorConfig> mirrorConfigs = <MirrorConfig>[];
    for (final m in json['mirrors']) {
      mirrorConfigs.add(MirrorConfig.fromJson(m));
    }
    return RepositoryConfig(json['repo_url'], rootKeys, mirrorConfigs)
      ..updatePackageUrl = json['update_package_url']
      ..rootVersion = json['root_verion']
      ..rootThreshold = json['root_threshold'];
  }

  dynamic toJson() {
    final Map<String, dynamic> resp = {'repo_url': repoUrl};
    resp['root_keys'] = rootKeys.map((k) => k.toJson()).toList();
    resp['mirrors'] = mirrors.map((m) => m.toJson()).toList();

    if (updatePackageUrl != null) {
      resp['update_package_url'] = updatePackageUrl;
    }
    if (rootVersion != null) {
      resp['root_version'] = rootVersion;
    }
    if (rootThreshold != null) {
      resp['root_threshold'] = rootThreshold;
    }
    return resp;
  }
}

class KeyConfig {
  final String type;
  final String value;

  KeyConfig(this.type, this.value);

  factory KeyConfig.fromJson(Map<String, dynamic> json) =>
      KeyConfig(json['type'], json['value']);

  dynamic toJson() => {'type': type, 'value': value};
}

class MirrorConfig {
  final String mirrorUrl;
  final bool subscribe;
  String blobMirrorUrl;

  MirrorConfig(this.mirrorUrl, {@required this.subscribe});

  factory MirrorConfig.fromJson(Map<String, dynamic> json) {
    return MirrorConfig(json['mirror_url'], subscribe: json['subscribe'])
      ..blobMirrorUrl = json['blob_mirror_url'];
  }

  dynamic toJson() {
    final resp = {
      'mirror_url': mirrorUrl,
      'subscribe': subscribe,
    };

    if (blobMirrorUrl != null) {
      resp['blob_mirror_url'] = blobMirrorUrl;
    }
    return resp;
  }
}

class RepositoryManager {
  final Sl4f _sl4f;

  RepositoryManager(this._sl4f);

  Future<void> add(RepositoryConfig config) async =>
      await _sl4f.request('repo_facade.Add', config.toJson());

  Future<List<dynamic>> list() async =>
      await _sl4f.request('repo_facade.List', null);
}
