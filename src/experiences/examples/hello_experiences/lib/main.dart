// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:fuchsia_logger/logger.dart';

void main() {
  setupLogger(name: 'experiences');
  runApp(
    MaterialApp(
      home: Scaffold(
        body: Container(
          color: Colors.pink,
          child: Center(
            child: Text('Hello Experiences'),
          ),
        ),
      ),
    ),
  );
}
