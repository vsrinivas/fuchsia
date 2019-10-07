// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:math';

import 'package:flutter/material.dart';

import 'time_stringer.dart';

/// System Clock in the Base Shell
class Clock extends StatelessWidget {
  final TimeStringer _time = TimeStringer();

  @override
  Widget build(BuildContext context) {
    return LayoutBuilder(
      builder: (BuildContext context, BoxConstraints constraints) {
        return AnimatedBuilder(
          animation: _time,
          builder: (BuildContext context, Widget child) {
            return Container(
              child: Text(
                _time.timeOnly,
                style: TextStyle(
                  color: Colors.white,
                  fontSize: min(
                    constraints.maxWidth / 6.0,
                    constraints.maxHeight / 6.0,
                  ),
                  fontWeight: FontWeight.w200,
                  letterSpacing: 4.0,
                ),
              ),
            );
          },
        );
      },
    );
  }
}
