// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'package:fidl/fidl.dart';
import 'package:fidl_ermine_tools/fidl_async.dart';
import 'package:fuchsia_services/services.dart';

abstract class Automator {
  Future<void> launch(String appName);
  Future<void> closeAll();
}

class AutomatorService extends ShellAutomator {
  late final Automator automator;

  AutomatorService();

  void serve(ComponentContext componentContext) {
    componentContext.outgoing.addPublicService(
        (request) => ShellAutomatorBinding().bind(this, request),
        ShellAutomator.$serviceName);
  }

  @override
  Future<void> launch(ShellAutomatorLaunchRequest request) async {
    if (request.appName == null) {
      throw MethodException(AutomatorErrorCode.invalidArgs);
    }
    return automator.launch(request.appName!);
  }

  @override
  Future<void> closeAll() async {
    return automator.closeAll();
  }
}
