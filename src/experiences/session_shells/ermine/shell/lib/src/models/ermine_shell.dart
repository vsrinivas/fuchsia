// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import '../utils/suggestion.dart';
import 'ermine_story.dart';

/// Defines an interface for a session shell that primarily handles
/// story management for Ermine.
///
/// It handles all interactions with stories:
/// - directly through touch/mouse gestures
/// - indirectly through keyboard actions
/// - programmatically through scripting
abstract class ErmineShell {
  /// Creates a new [ErmineStory] with a given [Suggestion].
  void storyStarted(Suggestion suggestion);

  /// Handles a story removed from the shell.
  void storyDeleted(ErmineStory story);

  /// Handles story attributes changing and updating its UX in the shell.
  void storyChanged(ErmineStory story);
}
