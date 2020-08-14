// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// A package that registers shortcuts and their mapping actions to the FIDL
/// shortcut service and binds a listener.
///
/// To use this package, import following packages in the file you are going
/// to create a KeyboardShortcuts class :
///
/// 'package:fidl_fuchsia_ui_shortcut/fidl_async.dart'
/// 'package:fuchsia_services/services.dart'
/// 'package:keyboard_shortcuts/keyboard_shortcuts.dart' (current package)
///
/// Also, include the following service in your .cmx file:
///
/// "fuchsia.ui.shortcut.Registry",
///
/// Parameters in KeyboardShortcuts :
///
/// registry: FIDL RegistryProxy object. (package: fidl_fuchsia_ui_shortcut)
///           Create one with `RegistryProxy()`
/// actions: Map of shortcut keys and callback functions.
///          The keys must be same to the keys(action names) in your JSON file.
/// bindings: String of your json file that contains shortcut key information.
///           You can get this with `your_json_file.readAsString()`
///
/// The examples of JSON file:
///
/// {
///   "cancel": [
///     {
///       "char": "z",
///       "modifier": "control",
///       "chord": "Control + z",
///       "exclusive": false,
///       "description": "Cancel the last task"
///     },
///   ],
///   "goBack": [
///     {
///       "char": "tab",
///       "modifier": "control + shift",
///       "chord": "Control + Shift + tab",
///       "description": "Go back to the previous open component"
///     },
///     {
///       "char": "left",
///       "modifier": "control",
///       "chord": "Control + left",
///       "description": "Go back to the previous open component"
///     },
///   ],
/// }
///
/// One action(e.g. "goBack") can have multiple shortcuts.
/// See [Key] class in fuchsia_ui_input2 package to find valid "char" values.
/// See [_modifierFromString] method in src/keyboard_shortcuts.dart to fine valid "modifier" values.

export 'src/keyboard_shortcuts.dart';
