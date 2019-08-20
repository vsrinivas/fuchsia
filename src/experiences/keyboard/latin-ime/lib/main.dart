// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_fuchsia_ui_input/fidl_async.dart';
import 'package:flutter/material.dart';
import 'package:keyboard/keyboard.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_services/services.dart';

class ImeKeyboard extends StatelessWidget {
  final ImeServiceProxy _imeService;

  const ImeKeyboard({ImeServiceProxy imeService})
      : _imeService = imeService,
        super();

  void _onText(String text) {
    final kbEvent = KeyboardEvent(
      phase: KeyboardEventPhase.pressed,
      codePoint: text.codeUnitAt(0),
      hidUsage: 0,
      eventTime: 0,
      modifiers: 0,
      deviceId: 0,
    );
    var event = InputEvent.withKeyboard(kbEvent);
    _imeService.injectInput(event);
  }

  void _onDelete() {
    final kbEvent = KeyboardEvent(
      phase: KeyboardEventPhase.pressed,
      codePoint: 0,
      hidUsage: 0x2a,
      eventTime: 0,
      modifiers: 0,
      deviceId: 0,
    );
    var event = InputEvent.withKeyboard(kbEvent);
    _imeService.injectInput(event);
  }

  void _onGo() {
    final kbEvent = KeyboardEvent(
      phase: KeyboardEventPhase.pressed,
      codePoint: 0,
      hidUsage: 0x28,
      eventTime: 0,
      modifiers: 0,
      deviceId: 0,
    );
    var event = InputEvent.withKeyboard(kbEvent);
    _imeService.injectInput(event);
  }

  void _onHide() {
    _imeService.hideKeyboard();
  }

  @override
  Widget build(BuildContext context) {
    return Keyboard(
        onText: _onText, onDelete: _onDelete, onGo: _onGo, onHide: _onHide);
  }
}

void main() {
  setupLogger(name: 'latin_ime');

  final imeService = ImeServiceProxy();
  StartupContext.fromStartupInfo().incoming.connectToService(imeService);

  runApp(Theme(
    data: ThemeData.light(),
    child: ImeKeyboard(imeService: imeService),
  ));
}
