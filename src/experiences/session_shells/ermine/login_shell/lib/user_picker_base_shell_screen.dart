// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
import 'package:fidl_fuchsia_sys/fidl_async.dart';
import 'package:flutter/material.dart';
import 'package:meta/meta.dart';

import 'clock.dart';
import 'user_picker_base_shell_model.dart';
import 'user_picker_screen.dart';

/// The root widget which displays all the other windows of this app.
class UserPickerBaseShellScreen extends StatelessWidget {
  /// Launcher to launch the kernel panic module if needed.
  final Launcher launcher;

  /// Constructor.
  const UserPickerBaseShellScreen({
    @required this.launcher,
    Key key,
  }) : super(key: key);
  @override
  Widget build(BuildContext context) {
    return ScopedModelDescendant<UserPickerBaseShellModel>(
      builder: (
        BuildContext context,
        Widget child,
        UserPickerBaseShellModel model,
      ) {
        List<Widget> stackChildren = <Widget>[
          UserPickerScreen(),
          Align(
            alignment: FractionalOffset.center,
            child: Offstage(
              offstage: !model.showingClock,
              child: Clock(),
            ),
          ),
        ];

        return Stack(fit: StackFit.expand, children: stackChildren);
      },
    );
  }
}
