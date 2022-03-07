// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// An base class for information for aler dialogs.
class DialogInfo {
  /// The title of the alert dialog box.
  final String? title;

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

  const DialogInfo({
    required this.actions,
    this.defaultAction,
    this.onAction,
    this.onClose,
    this.title,
  });
}

/// A class for holding information for alert dialogs widgets that will be
/// carried by [AppState.dialogs].
class AlertDialogInfo extends DialogInfo {
  /// Optional. The content body of the dialog box.
  final String? body;

  const AlertDialogInfo({
    required String title,
    required List<String> actions,
    String? defaultAction,
    void Function(String action)? onAction,
    void Function()? onClose,
    this.body,
  }) : super(
          title: title,
          actions: actions,
          defaultAction: defaultAction,
          onAction: onAction,
          onClose: onClose,
        );
}

/// A class for holding information for password capture dialogs widgets that
/// will be carried by [AppState.dialogs].
class PasswordDialogInfo extends DialogInfo {
  /// The password prompt to show above the password text field.
  final String prompt;

  /// The callback to receive the entered password.
  final void Function(String? password) onSubmit;

  /// Optional. Callback to validate the password. Returns an error text on
  /// validation fail or [null] for success.
  final String? Function(String? password)? validator;

  PasswordDialogInfo({
    required this.prompt,
    required this.onSubmit,
    required List<String> actions,
    String? defaultAction,
    void Function(String action)? onAction,
    void Function()? onClose,
    this.validator,
  }) : super(
          actions: actions,
          defaultAction: defaultAction,
          onAction: onAction,
          onClose: onClose,
        );
}
