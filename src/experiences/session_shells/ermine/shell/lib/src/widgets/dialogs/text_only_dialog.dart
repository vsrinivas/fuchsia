// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:ermine/src/widgets/dialogs/dialog.dart' as ermine;
import 'package:flutter/material.dart';

/// A dialog consists of only text and action buttons.
class TextOnlyDialog extends ermine.Dialog {
  final String? title;
  final String? body;
  final Map<String, VoidCallback> buttons;

  const TextOnlyDialog({required this.buttons, this.title, this.body, Key? key})
      : assert(title != null || body != null),
        super(key: key);

  @override
  Widget build(BuildContext context) => AlertDialog(
        title: (title != null) ? Text(title!) : null,
        content: (body != null) ? Text(body!) : null,
        actions: [
          for (final label in buttons.keys)
            TextButton(
              onPressed: buttons[label],
              child: Text(label.toUpperCase()),
            ),
        ],
        insetPadding: EdgeInsets.symmetric(horizontal: 240),
        titlePadding: EdgeInsets.fromLTRB(40, 40, 40, 24),
        contentPadding: EdgeInsets.fromLTRB(40, 0, 40, 24),
        actionsPadding: EdgeInsets.only(right: 40, bottom: 24),
        titleTextStyle: Theme.of(context).textTheme.headline5,
      );
}
