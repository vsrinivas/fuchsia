// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// The asset types.
enum AssetType {
  /// Describes single assets containing both audio and video.
  movie,

  /// Describes single assets containing only audio.
  music,

  /// Describes assets that consist of a list of other assets.
  playlist
}

/// Describes an asset.
class Asset {
  /// Uri of the asset. Must be null for playlists.
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

  /// Constructs an asset.
  Asset({
    this.uri,
    this.type,
    this.title,
    this.artist,
    this.album,
    this.children,
  }) {
    if (type == AssetType.playlist) {
      assert(uri == null);
      assert(children != null);
    } else {
      assert(uri != null);
      assert(children == null);
    }
  }
}
