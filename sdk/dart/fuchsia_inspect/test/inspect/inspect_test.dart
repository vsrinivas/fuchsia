// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fuchsia_inspect/inspect.dart';
import 'package:test/test.dart';

void main() {
  test('configureInspect changes the VMO size each time', () {
    Inspect.configure(vmoSizeBytes: 1024);
    expect(Inspect.vmoSize, 1024);
    Inspect.configure(vmoSizeBytes: 2048);
    expect(Inspect.vmoSize, 2048);
  });

  test('configureInspect rejects negative or too-small VMO size', () {
    expect(() => Inspect.configure(vmoSizeBytes: -1024),
        throwsA(const TypeMatcher<ArgumentError>()));
    expect(() => Inspect.configure(vmoSizeBytes: 0),
        throwsA(const TypeMatcher<ArgumentError>()));
    expect(() => Inspect.configure(vmoSizeBytes: 16),
        throwsA(const TypeMatcher<ArgumentError>()));
    expect(Inspect.vmoSize, greaterThanOrEqualTo(64));
  });

  test('configureInspect does nothing if called with no parameters', () {
    Inspect.configure(vmoSizeBytes: 2048);
    expect(Inspect.vmoSize, 2048);
    Inspect.configure();
    expect(Inspect.vmoSize, 2048);
  });
}
