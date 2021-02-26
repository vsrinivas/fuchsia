// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_fuchsia_diagnostics/fidl_async.dart';

/// Diagnostic data returned by the reader.
///
/// Most data from the component will be found in the [payload].
class DiagnosticsData<METADATA> {
  final DataType dataType;
  final METADATA metadata;
  final String moniker;
  final Map<String, dynamic> payload;
  final int version;

  DiagnosticsData({
    required this.dataType,
    required this.metadata,
    required this.moniker,
    required this.payload,
    required this.version,
  });

  @override
  String toString() {
    return '''
      InspectMetadata(
          dataType: $dataType
          metadata: $metadata
          moniker: $moniker
          version: $version
      )
    ''';
  }
}

/// Metadata returned for a [DataType.inspect] request.
class InspectMetadata {
  List<String> errors;
  String filename;
  String componentUrl;
  int timeStamp;

  InspectMetadata({
    required this.errors,
    required this.filename,
    required this.componentUrl,
    required this.timeStamp,
  });

  @override
  String toString() {
    return '''
        InspectMetadata(
          errors: $errors
          file name: $filename
          component url: $componentUrl
          time stamp: $timeStamp
        )
    ''';
  }
}
