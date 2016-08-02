// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:modular_core/uuid.dart';
import 'package:parser/manifest.dart';

import 'session.dart';
import 'suggestion.dart';

typedef UpdateCallback(
    final Iterable<Suggestion> added, final Iterable<Uuid> removed);

/// A [SuggestionProvider] generates context suggestions. [Suggestinator] works
/// with multiple [SuggestionProvider]s that each create suggestions based on a
/// specific domain and/or algorithm.
abstract class SuggestionProvider {
  void initialize(
      final List<Manifest> manifests, final UpdateCallback callback);

  /// Starts generating suggestions for the session with [sessionId] and
  /// [graph].
  void addSession(final Session session);

  /// Stops generating suggestions for the session with [sessionId].
  void removeSession(final Session session);
}
