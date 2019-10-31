// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter_test/flutter_test.dart';
import 'package:settings/settings.dart';

void main() {
  test('Datetime', () async {
    final stopWatch = Stopwatch()..start();
    final datetime = Datetime();
    var spec = await datetime.getSpec();

    expect(spec.title, isNotNull);
    expect(spec.groups.first.values.first.text.text, isNotNull);

    // Make sure the next update is received after [Datetime.refreshDuration].
    spec = await datetime.getSpec();
    stopWatch.stop();

    expect(stopWatch.elapsed >= Datetime.refreshDuration, true);
  });
}
