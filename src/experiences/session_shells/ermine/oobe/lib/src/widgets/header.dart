// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

/// Defines a widget to display the title and description on OOBE screens.
class Header extends StatelessWidget {
  final String title;
  final String description;

  const Header({required this.title, required this.description});

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        // Title.
        Text(
          title,
          textAlign: TextAlign.center,
          style: Theme.of(context).textTheme.headline3,
        ),

        // Description.
        Container(
          alignment: Alignment.center,
          padding: EdgeInsets.all(24),
          child: SizedBox(
            width: 600,
            child: Text(
              description,
              textAlign: TextAlign.center,
              style:
                  Theme.of(context).textTheme.bodyText1!.copyWith(height: 1.55),
            ),
          ),
        ),
      ],
    );
  }
}
