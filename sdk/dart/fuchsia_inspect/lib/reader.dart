// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// This package contains a wrapper around [ArchiveAccessor](https://fuchsia.dev/reference/fidl/fuchsia.diagnostics#ArchiveAccessor)
/// for accessing inspect data.
///
/// The main class for this library is [ArchiveReader].
import 'src/reader/reader.dart';

export 'src/reader/diagnostic_config.dart';
export 'src/reader/diagnostic_data.dart';
export 'src/reader/reader.dart';
