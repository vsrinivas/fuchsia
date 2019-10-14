// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_fuchsia_modular/fidl_async.dart';
import 'package:fidl_fuchsia_ui_policy/fidl_async.dart';
import 'package:lib.widgets/model.dart';
import 'package:meta/meta.dart';

export 'package:lib.widgets/model.dart' show ScopedModel, ScopedModelDescendant;

/// The [Model] that provides a [BaseShellContext] and [UserProvider].
class BaseShellModel extends Model {
  BaseShellContext _baseShellContext;
  UserProvider _userProvider;
  Presentation _presentation;

  /// The [BaseShellContext] given to this app's [BaseShell].
  BaseShellContext get baseShellContext => _baseShellContext;

  /// The [UserProvider] given to this app's [BaseShell].
  UserProvider get userProvider => _userProvider;

  /// The [Presentation] given to this app's [BaseShell].
  Presentation get presentation => _presentation;

  /// Called when this app's [BaseShell] is given its [BaseShellContext],
  /// and [UserProvider], and (optionally) its [Presentation].
  @mustCallSuper
  void onReady(
    UserProvider userProvider,
    BaseShellContext baseShellContext,
    Presentation presentation,
  ) {
    _userProvider = userProvider;
    _baseShellContext = baseShellContext;
    _presentation = presentation;
    notifyListeners();
  }

  /// Called when the app's [BaseShell] stops.
  void onStop() => null;
}
