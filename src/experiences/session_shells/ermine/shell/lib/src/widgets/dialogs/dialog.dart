// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// An abstract class for information for dialogs.
abstract class DialogInfo {}

/// A class for holding information for alert dialogs widgets that will be
/// carried by [AppState.dialogs].
class AlertDialogInfo implements DialogInfo {
  /// The title of the alert dialog box.
  final String title;

  /// Optional. The content body of the dialog box.
  final String? body;

  /// Optional. The default action to invoke if the user presses submit button.
  /// This action MUST be present in the list of [actions].
  final String? defaultAction;

  /// The list of actions that are presented as buttons. The default action is
  /// drawn using [ElevatedButton] to emphasize it's default nature.
  final List<String> actions;

  /// Optional. Callback when the dialog is closed.
  final void Function()? onClose;

  /// Optional. Callback when a specific action is invoked.
  final void Function(String action)? onAction;

  const AlertDialogInfo({
    required this.title,
    required this.actions,
    this.defaultAction,
    this.onAction,
    this.onClose,
    this.body,
  });
}
