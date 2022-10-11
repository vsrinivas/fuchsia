// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'package:fidl_ermine_tools/fidl_async.dart';
import 'package:fuchsia_services/services.dart';
import 'package:login/src/states/oobe_state.dart';

abstract class Automator {
  /// Returns the current oobe screen.
  Future<OobeScreen> getScreen();

  /// Returns true if account was successfully authenticated.
  bool get authenticated;

  Future<void> login(String password);
  Future<void> logout();
  Future<void> setPassword(String password);
  Future<void> skipScreen();
}

class AutomatorService extends OobeAutomator {
  late final Automator automator;

  AutomatorService();

  void serve(ComponentContext componentContext) {
    componentContext.outgoing.addPublicService(
        (request) => OobeAutomatorBinding().bind(this, request),
        OobeAutomator.$serviceName);
  }

  @override
  Future<OobePage> getOobePage() async {
    final screen = await automator.getScreen();
    if (automator.authenticated && screen == OobeScreen.loading) {
      return OobePage.shell;
    }

    switch (await automator.getScreen()) {
      case OobeScreen.password:
        return OobePage.setPassword;
      case OobeScreen.loading:
        return OobePage.login;
      default:
        return OobePage.unknown;
    }
  }

  @override
  Future<void> login(String password) async {
    return automator.login(password);
  }

  @override
  Future<void> setPassword(String password) async {
    return automator.setPassword(password);
  }

  @override
  Future<void> skipPage() async {
    return automator.skipScreen();
  }
}
