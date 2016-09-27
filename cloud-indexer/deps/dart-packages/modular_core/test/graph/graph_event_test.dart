// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:modular_core/graph/graph.dart';
import 'package:modular_core/uuid.dart';

import 'package:test/test.dart';

void main() {
  test('GraphEvent Uuid', () {
    Uuid id = new Uuid.random();
    GraphEvent event1 = new GraphEvent(null, [], id: id);
    GraphEvent event2 = new GraphEvent(null, [], id: id);
    GraphEvent event3 = new GraphEvent(null, []);
    GraphEvent event4 = new GraphEvent(null, []);
    expect(event1.id, equals(event2.id));
    expect(event2.id, isNot(equals(event3.id)));
    expect(event3.id, isNot(equals(event4.id)));
  });
}
