// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:ermine/src/states/app_state.dart';
import 'package:ermine/src/widgets/dialogs/dialog.dart';
import 'package:flutter/material.dart';
import 'package:mobx/mobx.dart';

/// Displays dialogs sequentially.
class Dialogs extends StatelessWidget {
  final AppState app;

  const Dialogs(this.app);

  @override
  Widget build(BuildContext context) {
    // Display queued up dialogs if none are being displayed currently.
    if (!Navigator.canPop(context)) {
      _showAllDialogs(context);
    }
    return Offstage();
  }

  // Shows all dialogs sequentially.
  void _showAllDialogs(BuildContext context, [int index = 0]) async {
    if (index >= app.dialogs.length) {
      runInAction(() {
        app.dialogs.clear();
        // Hiding overlays should restore focus to child views.
        app.hideOverlay();
      });
      return;
    }

    final dialog = app.dialogs[index] as AlertDialogInfo;
    final result = await showDialog<String?>(
        context: context,
        builder: (context) {
          return AlertDialog(
            title: Text(dialog.title),
            content: (dialog.body != null) ? Text(dialog.body!) : null,
            actions: [
              for (final label in dialog.actions)
                if (label == dialog.defaultAction)
                  ElevatedButton(
                    autofocus: true,
                    child: Text(label.toUpperCase()),
                    onPressed: () => Navigator.pop(context, label),
                  )
                else
                  OutlinedButton(
                    child: Text(label.toUpperCase()),
                    onPressed: () => Navigator.pop(context, label),
                  )
            ],
            insetPadding: EdgeInsets.symmetric(horizontal: 240),
            titlePadding: EdgeInsets.fromLTRB(40, 40, 40, 24),
            contentPadding: EdgeInsets.fromLTRB(40, 0, 40, 24),
            actionsPadding: EdgeInsets.only(right: 40, bottom: 24),
            titleTextStyle: Theme.of(context).textTheme.headline5,
          );
        });
    if (result != null) {
      dialog.onAction?.call(result);
    }
    dialog.onClose?.call();

    _showAllDialogs(context, index + 1);
  }
}
