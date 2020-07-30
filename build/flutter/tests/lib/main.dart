// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';

import 'package:flutter/material.dart';

void main() {
  // Verify that resource loading works
  final txt = File('/pkg/data/text_file.txt').readAsStringSync();

  runApp(
    MaterialApp(
      home: Scaffold(
        body: Container(
          color: Color(0xFF00FFFF),
          child: Center(
            child: Row(
              mainAxisAlignment: MainAxisAlignment.spaceEvenly,
              children: [
                Text(txt),
                // verify that the assets get parsed
                Image.asset("assets/logo.png"),
              ],
            ),
          ),
        ),
      ),
    ),
  );
}
