// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:fuchsia_inspect/inspect.dart';

import '../utils/utils.dart';
import 'app_model.dart';

/// Model that manages the state of the Topbar UX.
class TopbarModel implements Inspectable {
  /// Provides access to [AppModel].
  final AppModel appModel;

  /// Constructor.
  TopbarModel({@required this.appModel});

  /// The [GlobalKey] used by ask button to derive its [RelativeRect].
  GlobalKey askButtonKey = GlobalKey(debugLabel: 'askButton');

  /// The [GlobalKey] used by status button to derive its [RelativeRect].
  GlobalKey statusButtonKey = GlobalKey(debugLabel: 'statusButton');

  /// The [GlobalKey] used by keyboard button to derive its [RelativeRect].
  GlobalKey keyboardButtonKey = GlobalKey(debugLabel: 'keyboardButton');

  /// Get the [RelativeRect] for the Ask button.
  RelativeRect get askButtonRect => relativeRectFromGlobalKey(askButtonKey);

  /// Get the [RelativeRect] for the Keyboard button.
  RelativeRect get keyboardButtonRect =>
      relativeRectFromGlobalKey(keyboardButtonKey);

  /// Get the [RelativeRect]for the status button.
  RelativeRect get statusButtonRect =>
      relativeRectFromGlobalKey(statusButtonKey);

  /// Display the Overview.
  void showOverview() => appModel.onOverview();

  /// Display Recents.
  void showRecents() => appModel.onRecents();

  /// Display the Ask bar. Called by Ask Button.
  void showAsk() => appModel.onMeta();

  /// Display the keyboard help panel.
  void showKeyboardHelp() => appModel.onKeyboard();

  /// Display the status panel.
  void showStatus() => appModel.onStatus();

  @override
  void onInspect(Node node) {
    Rect rect = rectFromGlobalKey(askButtonKey);
    if (rect == null) {
      return;
    }
    node
        .stringProperty('askButtonRect')
        .setValue('${rect.left},${rect.top},${rect.width},${rect.height}');

    rect = rectFromGlobalKey(statusButtonKey);
    node
        .stringProperty('statusButtonRect')
        .setValue('${rect.left},${rect.top},${rect.width},${rect.height}');

    rect = rectFromGlobalKey(keyboardButtonKey);
    node
        .stringProperty('keyboardButtonRect')
        .setValue('${rect.left},${rect.top},${rect.width},${rect.height}');
  }
}
