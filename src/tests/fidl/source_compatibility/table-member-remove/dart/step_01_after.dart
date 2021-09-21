// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:fidl_fidl_test_tablememberremove/fidl_async.dart' as fidllib;

// [START contents]
void useTable(fidllib.Profile profile) {
  if (profile.timezone != null) {
    print('timezone: ${profile.timezone}');
  }
  if (profile.temperatureUnit != null) {
    print('preferred unit: ${profile.temperatureUnit}');
  }
  profile.$unknownData?.forEach((ordinal, data) {
    print('unknown ordinal $ordinal with bytes ${data.data}');
  });
}
// [END contents]
