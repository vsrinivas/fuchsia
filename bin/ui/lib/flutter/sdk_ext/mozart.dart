// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

library dart_mozart;

// Should be set to a |mozart::NativesDelegate*| by the embedder.
int _context;

int _viewContainer;

class MozartStartupInfo {
  static int takeViewContainer() {
    final int handle = _viewContainer;
    _viewContainer = null;
    return handle;
  }
}

class Mozart {
  static void offerServiceProvider(int handle) {
    _offerServiceProvider(_context, handle);
  }

  static void _offerServiceProvider(int context, int handle)
      native 'Mozart_offerServiceProvider';
}
