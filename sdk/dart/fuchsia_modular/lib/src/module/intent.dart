// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_fuchsia_modular/fidl_async.dart' as fidl;

/// An [Intent] is a fundamental building block of module development.
/// Modules will either be started with an intent or will receive an
/// intent after they have been launched. It is up to the module author
/// to decide how to respond to the intents that they receive.
///
/// A module will only receive intents which have been registered in their
/// module manifest file. A special case is when they are launched by the
/// system launcher in which case the action will be an empty string.
///
/// An example manifest which handles multiple intents would look like:
/// ```
/// {
///   "@version": 2,
///   "binary": "my_binary",
///   "suggestion_headline": "My Suggesting Headline",
///   "intent_filters": [
///     {
///       "action": "com.my-pets-app.show_cats",
///       "parameters": [
///         {
///           "name": "cat",
///           "type": "cat-type"
///         }
///       ]
///     },
///     {
///       "action": "com.my-pets-app.show_dogs",
///       "parameters": [
///         {
///           "name": "dog",
///           "type": "dog-type"
///         },
///         {
///           "name": "owner",
///           "type": "person-type"
///         }
///       ]
///     }
///   ]
/// }
/// ```
class Intent extends fidl.Intent {
  /// Creates an [Intent] that is used to start
  /// a module which can handle the specified action.
  /// If an explicit handler is not set the modular framework
  /// will search for an appropriate handler for the given action.
  Intent({
    required String? action,
    String? handler,
  }) : super(
          action: action,
          handler: handler,
          parameters: [],
        );
}
