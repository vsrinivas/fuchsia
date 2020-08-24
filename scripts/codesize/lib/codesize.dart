// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// @dart = 2.8

/// The codesize library.
library codesize;

export 'bloaty.dart';
export 'build.dart';
export 'common_util.dart';
export 'io.dart' show Io, runWithIo;
export 'queries/index.dart';
export 'queries/source_lang.dart';
export 'render/html.dart' show HtmlRenderer;
export 'render/terminal.dart' show TerminalRenderer;
export 'render/tsv.dart' show TsvRenderer;
export 'run_queries.dart';
export 'symbols/cache.dart';
export 'symbols/repo.dart';
export 'types.dart';
