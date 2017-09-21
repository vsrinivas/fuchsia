// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/services.dart';

import 'photo.dart';

/// Storage used for photos.
abstract class PhotoStorage {
  /// The list of photos added in the storage.
  List<Photo> get photos;

  /// Adds the given photo in the storage.
  void add(Photo photo);

  /// Returns the ImageProvider representing the given photo.
  ImageProvider getImageProvider(Photo photo);
}
