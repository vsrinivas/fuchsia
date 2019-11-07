// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

/// A pane to display in the settings container.
abstract class SettingsPane extends StatelessWidget {
  /// The title to display as the title of the button in the top bar
  final String title;

  const SettingsPane({this.title, Key key}) : super(key: key);
}

class ProfilesSetttingsPane extends SettingsPane {
  const ProfilesSetttingsPane() : super(title: 'PROFILES');

  @override
  Widget build(BuildContext context) {
    return Container(
      color: Theme.of(context).colorScheme.background,
      child: Center(
        child: Text('Terminal Profiles'),
      ),
    );
  }
}
