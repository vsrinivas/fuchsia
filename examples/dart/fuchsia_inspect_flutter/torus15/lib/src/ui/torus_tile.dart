// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

class TorusTile extends StatelessWidget {
  const TorusTile({@required this.number, @required this.tileMoveHandler});

  final int number;
  final GestureDragEndCallback tileMoveHandler;

  static const tileSize = 64.0;
  static const tileFontSize = 24.0;

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      // drag event handler
      onPanEnd: tileMoveHandler,

      // tile UI
      child: Container(
        width: tileSize,
        height: tileSize,
        decoration: BoxDecoration(
          color: Colors.blue[50],
          border: Border.all(
            color: Colors.blue,
            width: 4.0,
            style: BorderStyle.solid,
          ),
          borderRadius: BorderRadius.circular(8.0),
        ),
        margin: const EdgeInsets.all(4.0),
        child: Center(
          child: Text(
            number.toString(),
            // textDirection : TextDirection.ltr,
            style: TextStyle(
              color: Colors.indigo[900],
              fontSize: tileFontSize,
              fontWeight: FontWeight.bold,
            ),
          ),
        ),
      ),
    );
  }
}
