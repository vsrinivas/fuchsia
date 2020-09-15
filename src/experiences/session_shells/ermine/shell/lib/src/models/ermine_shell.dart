// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_fuchsia_ui_views/fidl_async.dart';
import 'package:fuchsia_scenic_flutter/child_view_connection.dart';

import '../utils/presenter.dart';
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
  void storySuggested(Suggestion suggestion);

  /// Handles a story removed from the shell.
  void storyDeleted(ErmineStory story);

  /// Handles story attributes changing and updating its UX in the shell.
  void storyChanged(ErmineStory story);

  /// Called when a story should be presented. A story is presented after
  /// it has been launched. The story may have been proposed by ermine or
  /// an external source.
  void presentStory(
    /// The [ChildViewConnection] used to connect the view to the process.
    ChildViewConnection connection,

    /// The [ViewRef] of the view used as a handle for focusing.
    ViewRef viewRef,

    /// A view controller which can be used to communicate with the running process
    /// This value may be null
    ViewControllerImpl viewController,

    /// A string identifying the launched story. This id is only valid if the
    /// story was launched from ermine.
    String id,
  );

  /// Called when a story is dismissed by the session instead of the user.
  void dismissStory(ViewControllerImpl viewController);
}
