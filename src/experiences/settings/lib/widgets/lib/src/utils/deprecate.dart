// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// WARNING(MS-1613): to support a clean separation of concerns Flutter widgets
/// will no longer depend the Fuchsia system (even transitively). Instead system
/// interactions should be coupled to a Flutter application by authors directly
/// using either the ModuleDriver (//topaz/public/lib/app_driver) or the raw,
/// Dart FIDL bindings. A special class of Fuchisa specific widgets will be
/// provided as code is refactored.

import 'package:fuchsia_logger/logger.dart';

/// Adds a logger message about the impending deprecation of a given class or
/// method related to MS-1613.
void deprecate(String name) {
  log.warning(
      '''"$name" is deprecated, see MS-1613 for context. Some suggestions
* System interactions: use //topaz/public/lib/app_driver/dart or the raw Dart FIDL bindings.
* App state: use 'package:lib.widgets/model.dart' directly or 'package:scoped_model/model.dart' when it's avaialble.
* Model access in widget tree: use 'package:lib.widgets/model.dart' or 'package:scoped_model/model.dart' when it's avaialble.
''');
}
