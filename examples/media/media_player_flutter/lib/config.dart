// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'asset.dart';

/// Reads the configuration from [fileName].
Future<List<Asset>> readConfig(String fileName) async {
  File file = new File(fileName);

  if (!(await file.exists())) {
    throw new ArgumentError.value(fileName, 'fileName', 'File does not exist');
  }

  return _convertAssetList(JSON.decode(await file.readAsString()));
}

String _convertString(dynamic json) {
  if (json is! String) {
    throw new FormatException('Config file is malformed: string expected');
  }

  return json;
}

bool _convertBool(dynamic json) {
  if (json is! String || (json != 'true' && json != 'false')) {
    throw new FormatException(
        'Config file is malformed: "true" or "false" expected');
  }

  return json == 'true';
}

Asset _convertAsset(dynamic json) {
  if (json is! Map) {
    throw new FormatException('Config file is malformed: object expected');
  }

  Uri uri;
  AssetType type;
  String title;
  String artist;
  String album;
  List<Asset> children;
  bool loop = false;
  String device;
  String service;

  json.forEach((String key, dynamic value) {
    switch (key) {
      case 'uri':
      case 'url':
        try {
          uri = Uri.parse(_convertString(value));
        } on FormatException {
          throw new FormatException('Config file is malformed: bad URI $value');
        }
        break;
      case 'type':
        switch (_convertString(value)) {
          case 'movie':
            type = AssetType.movie;
            break;
          case 'song':
            type = AssetType.song;
            break;
          case 'playlist':
            type = AssetType.playlist;
            break;
          case 'remote':
            type = AssetType.remote;
            break;
          default:
            throw new FormatException(
                'Config file is malformed: $value is not a valid type');
        }
        break;
      case 'title':
        title = _convertString(value);
        break;
      case 'artist':
        artist = _convertString(value);
        break;
      case 'album':
        album = _convertString(value);
        break;
      case 'children':
        children = _convertAssetList(value);
        break;
      case 'loop':
        loop = _convertBool(value);
        break;
      case 'device':
        device = _convertString(value);
        break;
      case 'service':
        service = _convertString(value);
        break;
    }
  });

  if (type == null) {
    // Try to infer the type.
    if (uri == null) {
      if (children != null) {
        type = AssetType.playlist;
      } else if (device != null || service != null) {
        type = AssetType.remote;
      }
    } else if (_isMovieUri(uri)) {
      type = AssetType.movie;
    } else if (_isMusicUri(uri)) {
      type = AssetType.song;
    }
  }

  if (type == null) {
    throw new FormatException(
        'Config file is malformed: asset type was not specified and cannot be'
        ' inferred');
  }

  switch (type) {
    case AssetType.movie:
      _checkNotNull(type, uri, "a URI");
      _checkNull(type, children, "children");
      _checkNull(type, device, "device name");
      _checkNull(type, service, "service name");
      return new Asset.movie(
        uri: uri,
        title: title,
        artist: artist,
        album: album,
        loop: loop,
      );

    case AssetType.song:
      _checkNotNull(type, uri, "a URI");
      _checkNull(type, children, "children");
      _checkNull(type, device, "device name");
      _checkNull(type, service, "service name");
      return new Asset.song(
        uri: uri,
        title: title,
        artist: artist,
        album: album,
        loop: loop,
      );

    case AssetType.playlist:
      _checkNull(type, uri, "a URI");
      _checkNotNull(type, children, "children");
      _checkNull(type, artist, "artist name");
      _checkNull(type, album, "album name");
      _checkNull(type, device, "device name");
      _checkNull(type, service, "service name");
      if (children.isEmpty) {
        throw new FormatException(
            'Config file is malformed: a playlist must have at least one child');
      }
      if (!children.every(
          (Asset c) => c.type == AssetType.movie || c.type == AssetType.song)) {
        throw new FormatException(
            'Config file is malformed: playlist children must be songs or movies');
      }
      return new Asset.playlist(
        title: title,
        children: children,
        loop: loop,
      );

    case AssetType.remote:
      _checkNull(type, uri, "a URI");
      _checkNull(type, children, "children");
      _checkNull(type, artist, "artist name");
      _checkNull(type, album, "album name");
      _checkNotNull(type, device, "device name");
      _checkNotNull(type, service, "service name");
      return new Asset.remote(
        title: title,
        device: device,
        service: service,
      );

    default:
      throw new FormatException('Unknown asset type: $type');
  }
}

void _checkNotNull(AssetType type, Object value, String name) {
  if (value == null) {
    throw new FormatException(
        'Config file is malformed: a $type must have $name');
  }
}

void _checkNull(AssetType type, Object value, String name) {
  if (value != null) {
    throw new FormatException(
        'Config file is malformed: a $type must not have $name');
  }
}

List<Asset> _convertAssetList(dynamic json) {
  if (json is! List) {
    throw new FormatException('Config file is malformed: array expected');
  }

  List<Asset> list = new List<Asset>();

  json.forEach((dynamic item) {
    Asset asset = _convertAsset(item);
    if (asset != null) {
      list.add(asset);
    }
  });

  return list;
}

bool _isMovieUri(Uri uri) {
  switch (_extension(uri)) {
    case 'ogv':
    case 'mp4':
    case 'vp8':
    case 'vp9':
    case 'mkv':
    case 'mov':
    case 'webm':
      return true;
  }

  return false;
}

bool _isMusicUri(Uri uri) {
  switch (_extension(uri)) {
    case 'ogg':
    case 'wav':
    case 'mp3':
    case 'flac':
      return true;
  }

  return false;
}

String _extension(Uri uri) {
  String lastSegment = uri.pathSegments.last;
  int index = lastSegment.lastIndexOf('.');
  return index == -1 ? null : lastSegment.substring(index + 1);
}
