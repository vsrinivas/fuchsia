// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert' show JSON;

import 'index.dart';

// TODO(thatguy): Prefer to merge these two json files into one, once
// Manifests can be serialized in an exportable way.
String renderJsonIndex(final Index index) {
  return JSON.encode(index.manifests);
}

String renderJsonTypes(final Index index) {
  return JSON.encode({
    'verb': index.verbRanking,
    'semantic': index.semanticRanking,
    'representation': index.representationRanking,
    'embodiment': index.embodimentRanking,
  });
}
