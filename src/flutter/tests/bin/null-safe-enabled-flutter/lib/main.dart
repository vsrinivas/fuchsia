// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:null_enabled_adder/adder.dart';

void main() {
  var total = 0;
  total = safeAdd(total, 1);
  total = safeAdd(total, null);
  runApp(
    MaterialApp(
      home: Scaffold(
        body: Container(
          child: Center(
            child: Text('After adding: $total'),
          ),
        ),
      ),
    ),
  );
}
