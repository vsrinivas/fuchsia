// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Error returned when no manifest was found for a step.
class NoMatchedManifestError extends StateError {
  NoMatchedManifestError([String message]) : super(message);
}

/// Error returned when no manifest was found at the provided URL.
class ManifestNotFoundError extends StateError {
  ManifestNotFoundError([String message]) : super(message);
}
