// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_fuchsia_modular/fidl_async.dart' as fidl;

import '../intent.dart';

/// Converts the [fidlIntent] to an [Intent] object.
Intent convertFidlIntentToIntent(fidl.Intent fidlIntent) {
  Intent intent = Intent(
    action: fidlIntent.action,
    handler: fidlIntent.handler,
  );

  // We shouldn't have null fidl intent parameters but in the case that we
  // do we avoid adding them as it will cause a crash.
  if (fidlIntent.parameters != null) {
    intent.parameters!.addAll(fidlIntent.parameters!);
  }

  return intent;
}
