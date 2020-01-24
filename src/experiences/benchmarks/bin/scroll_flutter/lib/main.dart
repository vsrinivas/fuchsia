// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

void main() => runApp(Scroll());

class Scroll extends StatefulWidget {
  @override
  State<StatefulWidget> createState() => ScrollState();
}

class ScrollState extends State<Scroll> {
  ScrollController _controller;

  @override
  void initState() {
    _controller = ScrollController(initialScrollOffset: 8000.0);
    super.initState();
  }

  @override
  Widget build(BuildContext context) {
    return ConstrainedBox(
      constraints: BoxConstraints.expand(),
      child: Container(
        color: Colors.white,
        child: ListView(
          controller: _controller,
          children: <Widget>[
            Container(height: 8192.0),
            Center(
              child: Text(
                'Scroll Me!',
                style: TextStyle(
                  color: Colors.black,
                  fontWeight: FontWeight.bold,
                  fontSize: 32,
                ),
              ),
            ),
            Container(height: 8192.0),
          ],
        ),
      ),
    );
  }
}
