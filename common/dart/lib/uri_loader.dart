// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

/// Interface for loading an URI from the network.
abstract class UriLoader {
  /// Returns the string contents of [uri] or null upon error.
  Future<String> getString(Uri uri);
}
