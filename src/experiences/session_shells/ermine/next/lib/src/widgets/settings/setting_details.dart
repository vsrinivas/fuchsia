// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

/// Defines a scaffold widget to displaying details of an individual setting.
class SettingDetails extends StatelessWidget {
  final VoidCallback onBack;
  final String title;
  final Widget? trailing;
  final Widget? child;

  const SettingDetails({
    required this.onBack,
    required this.title,
    this.child,
    this.trailing,
  });

  @override
  Widget build(BuildContext context) {
    return Column(
      children: [
        AppBar(
          elevation: 0,
          leading: IconButton(
            onPressed: onBack,
            icon: Icon(Icons.arrow_back),
          ),
          title: Text(
            title,
            style: Theme.of(context).appBarTheme.titleTextStyle,
          ),
          actions: trailing == null ? null : [trailing!],
        ),
        Expanded(
          child: Container(
            decoration: BoxDecoration(
              border: Border(
                  top: BorderSide(color: Theme.of(context).indicatorColor)),
              color: Theme.of(context).bottomAppBarColor,
            ),
            child: child,
          ),
        ),
      ],
    );
  }
}
