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
    throw new ArgumentError.value(fileName, 'fileName',
      'File does not exist');
  }

  return _convertAssetList(JSON.decode(await file.readAsString()));
}

String _convertString(dynamic json) {
  if (json is! String) {
    throw new FormatException('Config file is malformed: string expected');
  }

  return json;
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
          case 'music':
            type = AssetType.music;
            break;
          case 'playlist':
            type = AssetType.playlist;
            break;
          case 'remote':
            type = AssetType.remote;
            break;
          default:
            throw new FormatException(
              'Config file is malformed: $value is not a valid type'
            );
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
    }
  });

  if (type == null) {
    if (uri == null) {
      if (children == null) {
        throw new FormatException(
          'Config file is malformed: an asset must have a URI or children'
        );
      }

      type = AssetType.playlist;
    } else if (children != null) {
      throw new FormatException(
        'Config file is malformed: an asset must have a URI or children, not'
        ' both'
      );
    } else if (_isRemoteUri(uri)) {
      type = AssetType.remote;
    } else if (_isMovieUri(uri)) {
      type = AssetType.movie;
    } else if (_isMusicUri(uri)) {
      type = AssetType.music;
    } else {
      throw new FormatException(
        'Config file is malformed: asset type was not specified and cannot be'
        ' inferred'
      );
    }
  } else if (type == AssetType.playlist) {
    if (children == null) {
      throw new FormatException(
        'Config file is malformed: playlists must have children'
      );
    }

    if (uri != null) {
      throw new FormatException(
        'Config file is malformed: playlists cannot have URIs'
      );
    }
  } else {
    if (uri == null) {
      throw new FormatException(
        'Config file is malformed: non-playlists must have URIs'
      );
    }

    if (children != null) {
      throw new FormatException(
        'Config file is malformed: non-playlists cannot have children'
      );
    }
  }

  return new Asset(
    uri: uri,
    type: type,
    title: title,
    artist: artist,
    album: album,
    children: children,
  );
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

bool _isRemoteUri(Uri uri) {
  return uri.scheme == 'remoteplayer';
}

String _extension(Uri uri) {
  String lastSegment = uri.pathSegments.last;
  int index = lastSegment.lastIndexOf('.');
  return index == -1 ? null : lastSegment.substring(index + 1);
}
