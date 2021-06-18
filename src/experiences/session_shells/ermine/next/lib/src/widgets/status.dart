// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:flutter_mobx/flutter_mobx.dart';

import 'package:next/src/states/app_state.dart';

/// Defines a widget to display glanceable information like build verison, ip
/// addresses, battery charge or cpu metrics.
class Status extends StatelessWidget {
  final AppState appState;

  const Status(this.appState);

  @override
  Widget build(BuildContext context) {
    return Observer(builder: (_) {
      return Center(
        child: Text(appState.buildVersion),
      );
    });
  }
}
