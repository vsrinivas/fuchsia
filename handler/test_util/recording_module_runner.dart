// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:handler/module_instance.dart';
import 'package:handler/module_runner.dart';

// A module runner that record its calls for inspection.
class RecordingModuleRunner implements ModuleRunner {
  ModuleInstance instance;
  int updateCallCount = 0;
  bool running = false;

  @override
  void start(ModuleInstance instance) {
    assert(!running);
    running = true;
    assert(this.instance == null);
    this.instance = instance;
  }

  @override
  void update() {
    assert(running);
    updateCallCount++;
  }

  @override
  void stop() {
    assert(running);
    running = false;
  }
}
