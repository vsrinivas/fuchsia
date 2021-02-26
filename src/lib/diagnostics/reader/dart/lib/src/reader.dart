// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'package:fidl_fuchsia_diagnostics/fidl_async.dart';
import 'package:fidl_fuchsia_mem/fidl_async.dart';
import 'package:fuchsia_services/services.dart';
import 'package:zircon/zircon.dart';

import 'diagnostic_config.dart';
import 'diagnostic_data.dart';

/// Selector for specifying data to retrieve from [ArchiveReader].
class Selector {
  String _rawSelector;

  /// Creates [Selector] from raw selector string.
  ///
  /// See https://fuchsia.dev/reference/fidl/fuchsia.diagnostics#SelectorArgument
  /// for raw selector format.
  factory Selector.fromRawSelector(String rawSelector) {
    return Selector._internal(rawSelector: rawSelector);
  }

  Selector._internal({required String rawSelector})
      : _rawSelector = rawSelector;

  SelectorArgument get _selectorArgument =>
      SelectorArgument.withRawSelector(_rawSelector);
}

/// Wrapper for the [ArchiveAccessor](https://fuchsia.dev/reference/fidl/fuchsia.diagnostics#ArchiveAccessor)
/// service.
///
/// Basic use of this class is as follows:
///
/// ```dart
/// final reader =
///     ArchiveReader.forInspect();
/// final snapshot = reader.snapshot;
/// ```
///
/// TODO(fxbug.dev/71014): Implement lifecycle and log reading.
class ArchiveReader<METADATA> {
  List<Selector>? selectors;
  DiagnosticsConfiguration<METADATA> diagnosticsConfiguration;

  /// Create an [ArchiveReader] for reading the data specified by
  /// a [InspectConfiguration], using the given [selectors].
  ///
  /// If no selector is specified the entire inspect tree will be returned.
  static ArchiveReader<InspectMetadata> forInspect(
          {List<Selector>? selectors}) =>
      ArchiveReader._internal(
          selectors: selectors,
          diagnosticsConfiguration: InspectConfiguration());

  ArchiveReader._internal(
      {required this.diagnosticsConfiguration, this.selectors});

  /// Obtain a snapshot of the current inspect data as constrained by
  /// [selector].
  ///
  /// The return value is the json object returned by [FormattedContent.json] as
  /// parsed by [jsonDecode].
  ///
  /// If [acceptSnapshot] is not null, this method returns a snapshot once
  /// [acceptSnapshot] returns true.  If [maxAttempts] is not null, at most
  /// [maxAttempts] are made to obtain a snapshot.  [delay] controls the delay
  /// between snapshot attempts.
  Future<List<DiagnosticsData<METADATA>>> snapshot(
      {bool acceptSnapshot(List<DiagnosticsData<METADATA>> snapshot)?,
      Duration attemptDelay = const Duration(milliseconds: 100),
      int? maxAttempts = 200}) async {
    for (var attempts = 0;
        maxAttempts == null || attempts < maxAttempts;
        attempts++) {
      final snapshot = await _snapshotAttempt;
      if (acceptSnapshot == null || acceptSnapshot(snapshot)) {
        return snapshot;
      }
      await Future.delayed(attemptDelay);
    }
    throw Exception('snapshot failed');
  }

  Future<List<DiagnosticsData<METADATA>>> get _snapshotAttempt async {
    final archiveAccessor = ArchiveAccessorProxy();
    final incoming = Incoming.fromSvcPath()..connectToService(archiveAccessor);
    final iterator = BatchIteratorProxy();

    late ClientSelectorConfiguration clientSelectorConfiguration;

    if (selectors != null) {
      clientSelectorConfiguration = ClientSelectorConfiguration.withSelectors(
          selectors!.map((selector) => selector._selectorArgument).toList());
    } else {
      clientSelectorConfiguration =
          ClientSelectorConfiguration.withSelectAll(true);
    }

    await archiveAccessor.streamDiagnostics(
        StreamParameters(
            dataType: diagnosticsConfiguration.dataType,
            streamMode: StreamMode.snapshot,
            format: Format.json,
            clientSelectorConfiguration: clientSelectorConfiguration),
        iterator.ctrl.request());
    final List<DiagnosticsData<METADATA>> results = [];
    for (var content = await iterator.getNext();
        content.isNotEmpty;
        content = await iterator.getNext()) {
      for (final entry in content) {
        Buffer? entryJson = entry.json;
        if (entryJson != null) {
          final jsonString = _readBuffer(entryJson);
          final jsonData = jsonDecode(jsonString);
          results.add(DiagnosticsData(
              metadata:
                  diagnosticsConfiguration.buildMetadata(jsonData['metadata']),
              dataType: diagnosticsConfiguration.dataType,
              moniker: jsonData['moniker'] ?? '',
              payload: jsonData['payload'] ?? <String, dynamic>{},
              version: jsonData['version'] ?? 0));
        }
      }
    }
    iterator.ctrl.close();
    await incoming.close();
    return results;
  }
}

String _readBuffer(Buffer buffer) =>
    utf8.decode(SizedVmo(buffer.vmo.handle, buffer.size)
        .read(buffer.size)
        .bytesAsUint8List());
