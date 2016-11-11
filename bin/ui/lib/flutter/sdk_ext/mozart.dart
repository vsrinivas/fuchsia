// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

library dart_mozart;

int _viewContainer;

class MozartStartupInfo {
  static int takeViewContainer() {
    final int handle = _viewContainer;
    _viewContainer = null;
    return handle;
  }
}
