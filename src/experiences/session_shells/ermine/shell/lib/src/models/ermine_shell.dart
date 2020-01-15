// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fuchsia_scenic_flutter/child_view_connection.dart';

import 'ermine_story.dart';

/// Defines an interface for a session shell that primarily handles
/// story management for Ermine.
///
/// It handles all interactions with stories:
/// - directly through touch/mouse gestures
/// - indirectly through keyboard actions
/// - programmatically through scripting
abstract class ErmineShell {
  /// Creates a new [ErmineStory] with a given [id], [name] and
  /// [childViewConnection] and adds it to the shell.
  ErmineStory storyStarted(
    String id,
    String name,
    ChildViewConnection childViewConnection,
  );

  /// Handles a story removed from the shell.
  void storyDeleted(ErmineStory story);

  /// Handles story attributes changing and updating its UX in the shell.
  void storyChanged(ErmineStory story);
}
