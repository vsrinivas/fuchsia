// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:internationalization/strings.dart';
import 'package:login/src/states/oobe_state.dart';
import 'package:login/src/widgets/details.dart';

/// Defines a widget for the final screen when oobe is complete.
class Ready extends StatelessWidget {
  final OobeState oobe;

  const Ready(this.oobe);

  @override
  Widget build(BuildContext context) {
    return Details(
      // Header: Title and description.
      title: Strings.passwordIsSet,
      description: Strings.readyToUse,

      // Button: Start workstation.
      buttons: [
        OutlinedButton(
          key: ValueKey('startWorkstation'),
          autofocus: true,
          onPressed: oobe.finish,
          child: Text(Strings.startWorkstation.toUpperCase()),
        ),
      ],
    );
  }
}
