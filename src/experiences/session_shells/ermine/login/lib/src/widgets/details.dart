// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(http://fxbug.dev/107480): Resolve lint issues and reenable analysis for file
// ignore_for_file: deprecated_member_use

import 'package:flutter/material.dart';
import 'package:login/src/states/oobe_state.dart';

/// Layout template widget used by each OOBE screens.
class Details extends StatelessWidget {
  final String title;
  final String description;
  final Widget? scrollableContent;
  final List<ButtonStyleButton> buttons;

  const Details(
      {required this.title,
      required this.description,
      required this.buttons,
      this.scrollableContent});

  @override
  Widget build(BuildContext context) {
    return FocusScope(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          // Header.
          Text(title, style: Theme.of(context).textTheme.headline3),
          SizedBox(height: 24),
          // Description.
          Text(description, style: Theme.of(context).textTheme.bodyText1),
          SizedBox(height: 48),

          // Scrollable content area.
          Expanded(
            child: SingleChildScrollView(
              child: SizedBox(width: kContentWidth, child: scrollableContent),
            ),
          ),
          SizedBox(height: 48),

          // Buttons.
          Row(
            children: [
              for (final button in buttons) ...[button, SizedBox(width: 24)],
            ],
          ),
        ],
      ),
    );
  }
}
