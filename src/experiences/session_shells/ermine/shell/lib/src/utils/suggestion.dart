// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Defines a class to hold attributes of a Suggestion.
class Suggestion {
  final String id;
  final String url;
  final String title;

  const Suggestion({this.id, this.url, this.title});
}
