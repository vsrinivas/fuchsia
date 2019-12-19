// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'status.dart';

/// Defines a widget to render a Button for a status entry.
class StatusButton extends StatelessWidget {
  final String label;
  final VoidCallback onTap;

  const StatusButton(this.label, this.onTap, [Key key]) : super(key: key);

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      onTap: onTap,
      child: Container(
        height: kItemHeight,
        color: Colors.white,
        padding: EdgeInsets.symmetric(vertical: 0, horizontal: 2),
        child: Text(
          label.toUpperCase(),
          style: TextStyle(
            color: Colors.black,
            fontWeight: FontWeight.w400,
          ),
        ),
      ),
    );
  }
}
