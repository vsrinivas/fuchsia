// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(http://fxbug.dev/107480): Resolve lint issues and reenable analysis for file
// ignore_for_file: deprecated_member_use

import 'package:flutter/material.dart';

/// Defines a widget to display the title and description on OOBE screens.
class Header extends StatelessWidget {
  final String title;
  final String description;

  const Header({required this.title, required this.description});

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        // Title.
        Text(
          title,
          style: Theme.of(context).textTheme.headline3,
        ),
        SizedBox(height: 24),
        // Description.
        Text(
          description,
          style: Theme.of(context).textTheme.bodyText1,
        ),
      ],
    );
  }
}
