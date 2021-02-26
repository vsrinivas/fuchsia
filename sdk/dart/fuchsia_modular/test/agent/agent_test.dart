// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:test/test.dart';

import 'package:fuchsia_modular/src/agent/agent.dart'; // ignore: implementation_imports

void main() {
  test('factory should return same instance', () {
    expect(Agent(), Agent());
  });
}
