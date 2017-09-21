// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/services.dart';

import 'photo.dart';
import 'photo_storage.dart';

// An in memory cache for photos.
class DummyPhotoStorage extends PhotoStorage {
  final List<Photo> _photos = <Photo>[];

  @override
  List<Photo> get photos => new List.unmodifiable(_photos);

  @override
  void add(Photo photo) {
    _photos.add(photo);
  }

  @override
  ImageProvider getImageProvider(Photo photo) => new NetworkImage(photo.url);
}
