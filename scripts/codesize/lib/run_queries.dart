// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// @dart = 2.8

import 'dart:async';
import 'dart:io';
import 'dart:isolate';
import 'dart:typed_data';

import 'queries/index.dart';
import 'queries/source_lang.dart';
import 'types.dart';

/// A funtion which produces a `Query`.
typedef QueryThunk = Query Function();

class IsolateWithPort {
  const IsolateWithPort(
      this.isolate, this.sendPort, this.receivePort, this.shutdownEvent);

  final Isolate isolate;
  final SendPort sendPort;
  final ReceivePort receivePort;
  final Future<List<Query>> shutdownEvent;
}

/// Run queries on bloaty reports in parallel, and collect their results
/// in `query`.
class QueryRunner {
  QueryRunner(this.queryThunks, {int numConcurrency, this.onlyLang})
      : queries = queryThunks.map((f) => f()).toList(growable: false),
        concurrency = numConcurrency != null
            ? numConcurrency
            // Up to 12 isolates if inferring from `numberOfProcessors`.
            : ((v) =>
                v < 2 ? 2 : v > 12 ? 12 : v)(Platform.numberOfProcessors ~/ 2) {
    for (var i = 0; i < concurrency; i++) {
      final rx = ReceivePort();
      final isolateTx = rx.sendPort;
      Future<IsolateWithPort> future;
      final errorEvent = ReceivePort();
      final subs = <StreamSubscription>[];
      future = Isolate.spawn(isolateMain, isolateTx,
              errorsAreFatal: true, onError: errorEvent.sendPort)
          .then((isolate) async {
        final rxBroadcast = rx.asBroadcastStream();
        final tx = await rxBroadcast.first;
        tx.send(onlyLang);
        tx.send(queryThunks.length);
        for (final f in queryThunks) {
          tx.send(f());
        }

        final shutdownCompleter = Completer<List<Query>>();
        final key = IsolateWithPort(isolate, tx, rx, shutdownCompleter.future);
        taskDepth[key] = 0;
        if (!spawning.remove(future))
          throw Exception('Unexpected isolated spawned');
        final collatedQueries = <Query>[];
        subs.add(rxBroadcast.listen((message) {
          if (message is int) {
            taskDepth[key] -= message;
            if (taskDepth[key] < 0) throw Exception('Negative task depth');
          } else if (message is Query) {
            collatedQueries.add(message);
            if (collatedQueries.length == queries.length) {
              // Isolate processed our shutdown message.
              // Clean up all the streams and subscriptions.
              subs
                ..forEach((sub) => sub.cancel())
                ..clear();
              isolate.kill();
              errorEvent.close();
              rx.close();
              shutdownCompleter.complete(collatedQueries);
            }
          }
        }));
        isolates.add(key);
        return key;
      });
      spawning.add(future);
      subs.add(errorEvent.listen((message) {
        // Detected error from within the isolate.
        throw message;
      }));
    }
  }

  /// List of functions which when evaluated, constructs a query that could be
  /// sent to an isolate.
  final List<QueryThunk> queryThunks;

  /// If non-null, only collect statistics from binaries of this language.
  final SourceLang onlyLang;
  final List<Query> queries;
  final int concurrency;
  List<IsolateWithPort> isolates = [];
  Set<Future<IsolateWithPort>> spawning = <Future<IsolateWithPort>>{};
  Map<IsolateWithPort, int> taskDepth = <IsolateWithPort, int>{};

  Future<void> addReport(AnalysisItem item) async {
    // Find the most free isolate.
    IsolateWithPort mostFree;
    int minDepth;
    for (final depth in taskDepth.entries) {
      if (minDepth == null || depth.value < minDepth) {
        minDepth = depth.value;
        mostFree = depth.key;
      }
    }
    // Bump the task depth by report file size.
    // We use the size of the report file as a proxy to how much work it is
    // to process the report.
    final fileSize = File(item.path).statSync().size;
    // If we have not finished launching all isolates, wait for the next
    // isolate to start up and send the item there.
    if (minDepth != 0 && taskDepth.length < concurrency) {
      final nextSpawned = await spawning.first;
      taskDepth[nextSpawned] += fileSize;
      nextSpawned.sendPort.send(item);
      return;
    }
    // Otherwise, send the item to the most free isolate.
    taskDepth[mostFree] += fileSize;
    mostFree.sendPort.send(item);
  }

  Future<void> join() async {
    await Future.wait(spawning);
    for (final isolate in isolates) isolate.sendPort.send(null);
    final queriesFromIsolates =
        await Future.wait(isolates.map((e) => e.shutdownEvent));
    for (final isolateQueries in queriesFromIsolates) {
      if (isolateQueries.length != queries.length)
        throw Exception('Expected ${queries.length} queries from isolate');
    }
    for (var i = 0; i < queries.length; i++) {
      queries[i].mergeWith(queriesFromIsolates.map((e) => e[i]));
    }
    isolates = [];
    spawning = {};
    taskDepth = {};
  }

  static Future<void> isolateMain(SendPort sendPort) async {
    final receivePort = ReceivePort();
    sendPort.send(receivePort.sendPort);
    final rxStream = receivePort.asBroadcastStream();
    final SourceLang onlyLang = await rxStream.first;
    final int numQueries = await rxStream.first;
    final queries = <Query>[];
    for (var i = 0; i < numQueries; i++) {
      queries.add(await rxStream.first);
    }

    await for (final message in rxStream) {
      if (message == null) {
        queries.forEach(sendPort.send);
        return;
      }

      final AnalysisItem reportFile = message;
      Report parse(String name, Uint8List bytes,
          {ProgramContext reuseContext}) {
        try {
          return Report.fromBytes(name, bytes, reuseContext: reuseContext);
        } on Exception {
          print('Error loading report from ${reportFile.path}');
          rethrow;
        }
      }

      final bytes = await File(reportFile.path).readAsBytes();
      final report = parse(reportFile.name, bytes);

      // Additionally load the filtered report if generated.
      Report filteredReport = report;
      if (reportFile.filteredCounterpart != null) {
        final filteredBytes =
            await File(reportFile.filteredCounterpart).readAsBytes();
        filteredReport =
            parse(reportFile.name, filteredBytes, reuseContext: report.context);
      }

      for (final query in queries) {
        if (onlyLang != null) {
          if (filteredReport.context.lang != onlyLang) continue;
        }
        if (query is IgnorePageInHeatmapFilter)
          query.addReport(report);
        else
          query.addReport(filteredReport);
      }
      sendPort.send(bytes.length);
    }
  }
}
