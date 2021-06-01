// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_fuchsia_ui_focus/fidl_async.dart';
import 'package:fidl_fuchsia_ui_views/fidl_async.dart';
import 'package:flutter/foundation.dart';

/// Defines a [FocusChainListener] that calls [onChange] when the system
/// notifies of change in focus between views.
class FocusChangeListener extends FocusChainListener {
  final ValueChanged<List<ViewRef>> onChange;

  FocusChangeListener(this.onChange);
  @override
  Future<void> onFocusChange(FocusChain focusChain) async =>
      onChange(focusChain.focusChain);
}
