// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:meta/meta.dart';

/// The asset types.
enum AssetType {
  /// Individual assets containing both audio and video.
  movie,

  /// Individual assets containing only audio.
  song,

  /// Composite assets that consist of a list of other assets.
  playlist,

  /// Remote player
  remote,
}

/// Describes an asset.
class Asset {
  /// Uri of the asset. Must be null for playlists and remotes, required for
  /// all other asset types.
  final Uri uri;

  /// Type of the asset.
  final AssetType type;

  /// Title of the asset. May be null.
  final String title;

  /// Artist to which the asset is attributed. May be null.
  final String artist;

  /// Album name for the asset. May be null.
  final String album;

  /// Children of the playlist asset. Must be null for other asset types.
  final List<Asset> children;

  /// Device on which remote player is running. Required for remotes, must be
  /// null for other asset types.
  final String device;

  /// Service number under which remote player is published. Required for
  /// remotes, must be null for other asset types.
  final String service;

  /// Constructs an asset describing a movie.
  Asset.movie({
    @required this.uri,
    this.title,
    this.artist,
    this.album,
  })
      : type = AssetType.movie,
        children = null,
        device = null,
        service = null;

  /// Constructs an asset describing a song.
  Asset.song({
    @required this.uri,
    this.title,
    this.artist,
    this.album,
  })
      : type = AssetType.song,
        children = null,
        device = null,
        service = null;

  /// Constructs an asset describing a playlist.
  Asset.playlist({
    @required this.children,
    this.title,
  })
      : type = AssetType.playlist,
        uri = null,
        artist = null,
        album = null,
        device = null,
        service = null {
    assert(children.isNotEmpty);
    assert(children.every(
        (Asset c) => c.type == AssetType.movie || c.type == AssetType.song));
  }

  /// Constructs an asset describing a remote player.
  Asset.remote({
    @required this.device,
    @required this.service,
    this.title,
  })
      : type = AssetType.remote,
        uri = null,
        artist = null,
        album = null,
        children = null;
}
