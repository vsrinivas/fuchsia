// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:ermine/src/states/app_state.dart';
import 'package:flutter/material.dart';
import 'package:flutter_mobx/flutter_mobx.dart';

class AlertDialogs extends StatelessWidget {
  final AppState _app;

  const AlertDialogs(this._app);

  @override
  Widget build(BuildContext context) => RepaintBoundary(child: Observer(
        builder: (context) {
          return Stack(
            children: [
              for (final alert in _app.alerts)
                AlertDialog(
                  title: (alert.title != null) ? Text(alert.title!) : null,
                  content:
                      (alert.content != null) ? Text(alert.content!) : null,
                  actions: [
                    for (final label in alert.buttons.keys)
                      TextButton(
                        onPressed: alert.buttons[label],
                        child: Text(label.toUpperCase()),
                      ),
                  ],
                  insetPadding: EdgeInsets.symmetric(horizontal: 240),
                  titlePadding: EdgeInsets.fromLTRB(40, 40, 40, 24),
                  contentPadding: EdgeInsets.fromLTRB(40, 0, 40, 24),
                  actionsPadding: EdgeInsets.only(right: 40, bottom: 24),
                  titleTextStyle: Theme.of(context).textTheme.headline5,
                )
            ],
          );
        },
      ));
}
