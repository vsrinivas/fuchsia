// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// regexp that extracts a suitable 'fx build' target from a "label" field
// from a test_spec entry in tests.json.
// For example, it extracts "pa/th:label" from "//pa/th:label(//tool/chain:toolchain)"
RegExp _testLabelRe = RegExp(r'\/\/(.*)\((\/\/.*)\)');
String? getBuildTarget(String? testLabel) {
  return _testLabelRe.firstMatch(testLabel!)?.group(1);
}
