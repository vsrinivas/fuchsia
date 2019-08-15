// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

/// A widget for the button show in Topbar.
class Button extends StatelessWidget {
  final Widget child;
  final Decoration decoration;
  final EdgeInsets padding;
  final VoidCallback onTap;

  const Button({
    @required this.child,
    Key key,
    this.decoration,
    this.padding,
    this.onTap,
  }) : super(key: key);

  @override
  Widget build(BuildContext context) {
    return InkWell(
      focusColor: Colors.transparent,
      splashColor: Colors.transparent,
      child: Container(
        padding: padding ?? EdgeInsets.all(8),
        decoration: decoration,
        child: Center(child: child),
      ),
      onTap: onTap,
    );
  }
}
