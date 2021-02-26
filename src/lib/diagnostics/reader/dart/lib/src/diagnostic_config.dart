// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_fuchsia_diagnostics/fidl_async.dart';

import 'diagnostic_data.dart';

/// A configuration specifying what type of data the reader should return.
abstract class DiagnosticsConfiguration<METADATA> {
  DataType get dataType;
  METADATA buildMetadata(Map<String, dynamic>? metadataSource);
}

/// A configuration for retrieving inspect data.
class InspectConfiguration
    implements DiagnosticsConfiguration<InspectMetadata> {
  @override
  DataType get dataType => DataType.inspect;

  @override
  InspectMetadata buildMetadata(Map<String, dynamic>? metadataSource) =>
      InspectMetadata(
        errors: metadataSource?['errors'] ?? [],
        componentUrl: metadataSource?['component_url'] ?? '',
        filename: metadataSource?['filename'] ?? '',
        timeStamp: metadataSource?['timestamp'] ?? 0,
      );
}
