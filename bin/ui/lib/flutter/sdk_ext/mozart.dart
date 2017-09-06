// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

library dart_mozart;

import 'dart:zircon' show Handle;

// Should be set to a |mozart::NativesDelegate*| by the embedder.
int _context;

Handle _viewContainer;

class MozartStartupInfo {
  static Handle takeViewContainer() {
    final Handle handle = _viewContainer;
    _viewContainer = null;
    return handle;
  }
}

class Mozart {
  static void offerServiceProvider(Handle handle, List<String> services) {
    _offerServiceProvider(_context, handle, services);
  }

  static void _offerServiceProvider(int context,
                                    Handle handle,
                                    List<String> services) native
      'Mozart_offerServiceProvider';
}
