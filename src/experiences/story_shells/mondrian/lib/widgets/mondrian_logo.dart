// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:flutter/widgets.dart';

/// Logo Yellow
const Color yellow = Color(0xFFFFFD3B);

/// Logo Red
const Color red = Color(0xFFFC5D60);

/// Logo Blue
const Color blue = Color(0xFF4D8AE9);

/// Logo Border
const Color borderColor = Color(0xFF353535);

/// Programmatic implementation of Static Mondrian Logo
class MondrianLogo extends StatelessWidget {
  /// A Programmatic Mondrian Logo
  const MondrianLogo();

  @override
  Widget build(BuildContext context) {
    return LayoutBuilder(
      builder: (BuildContext layoutContext, BoxConstraints constraints) {
        double fontPoint = constraints.maxHeight / 2.3;
        double borderWidth = constraints.maxWidth / 37.5;
        double borderRadius = constraints.maxWidth * 0.0625;
        return Material(
          child: Container(
            child: Row(
              children: <Widget>[
                Flexible(
                  flex: 1,
                  child: Container(
                    decoration: BoxDecoration(
                      color: blue,
                      border: Border(
                        right: BorderSide(
                            color: borderColor, width: borderWidth),
                      ),
                    ),
                  ),
                ),
                Flexible(
                  flex: 1,
                  child: Column(
                    children: <Widget>[
                      Flexible(
                        flex: 1,
                        child: Container(
                          decoration: BoxDecoration(
                            color: red,
                            border: Border(
                              bottom: BorderSide(
                                  color: borderColor, width: borderWidth),
                            ),
                          ),
                        ),
                      ),
                      Flexible(
                        flex: 1,
                        child: Container(
                          child: Center(
                            child: Text(
                              'M',
                              style: TextStyle(
                                  fontFamily: 'Kanit', // ToDo - djmurphy
                                  fontStyle: FontStyle.normal,
                                  fontSize: fontPoint),
                            ),
                          ),
                          decoration: BoxDecoration(color: yellow),
                        ),
                      )
                    ],
                  ),
                ),
              ],
            ),
            foregroundDecoration: BoxDecoration(
              borderRadius: BorderRadius.all(
                Radius.circular(borderRadius),
              ),
              border: Border.all(width: borderWidth, color: borderColor),
            ),
          ),
          borderRadius: BorderRadius.all(
            Radius.circular(borderRadius),
          ),
        );
      },
    );
  }
}
