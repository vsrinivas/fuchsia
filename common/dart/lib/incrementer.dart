// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Provides read-only access to a unique counter.
class Incrementer {
  int _next = 0;
  int get next => _next++;
}
