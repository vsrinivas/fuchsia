// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:ermine_ui/ermine_ui.dart';
import 'package:flutter/material.dart';
import '../../utils/styles.dart';

class OobeButtons extends StatelessWidget {
  final List<OobeButtonModel> buttons;

  const OobeButtons(this.buttons);

  @override
  Widget build(BuildContext context) {
    return Row(
      mainAxisAlignment: MainAxisAlignment.center,
      children: <Widget>[
        for (final button in buttons)
          Container(
            margin:
                EdgeInsets.symmetric(horizontal: ErmineStyle.kOobeButtonMargin),
            child: button.filled
                ? FilledButton.large(button.name, button.callback)
                : BorderedButton.large(button.name, button.callback),
          ),
      ],
    );
  }
}

class OobeButtonModel {
  final String name;
  final VoidCallback callback;
  final bool filled;

  OobeButtonModel(this.name, this.callback, {this.filled = false});
}
