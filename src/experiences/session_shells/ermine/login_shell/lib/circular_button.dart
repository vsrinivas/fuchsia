// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:meta/meta.dart';

/// A button that is circular
class CircularButton extends StatelessWidget {
  /// Callback that is fired when the button is tapped
  final VoidCallback onTap;

  /// The icon to show in the button
  final IconData icon;

  /// Constructor
  const CircularButton({@required this.icon, this.onTap})
      : assert(icon != null);

  @override
  Widget build(BuildContext context) => Material(
        type: MaterialType.circle,
        elevation: 2.0,
        color: Colors.grey[200],
        child: InkWell(
          onTap: () => onTap?.call(),
          child: Container(
            padding: EdgeInsets.all(12.0),
            child: Icon(icon),
          ),
        ),
      );
}
