// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'dart:io' show File, Platform, Process, ProcessResult, WebSocket;

import 'package:logging/logging.dart';
import 'package:meta/meta.dart';

import 'dump.dart';
import 'sl4f_client.dart';
import 'trace_processing/metrics_results.dart';
import 'trace_processing/metrics_spec.dart';
import 'trace_processing/trace_importing.dart';

String _traceExtension({bool binary, bool compress}) {
  String extension = 'json';
  if (binary) {
    extension = 'fxt';
  }
  if (compress) {
    extension += '.gz';
  }
  return extension;
}

String _traceNameToTargetPath(String traceName, String extension) {
  return '/tmp/$traceName-trace.$extension';
}

final _log = Logger('Performance');

class Performance {
  // Environment variable names used by the catapult converter to tag the test results.
  static const String _builderNameVarName = 'BUILDER_NAME';
  static const String _buildBucketIdVarName = 'BUILDBUCKET_ID';
  static const String _buildCreateTimeVarName = 'BUILD_CREATE_TIME';
  static const String _inputCommitHostVarName = 'INPUT_COMMIT_HOST';
  static const String _inputCommitProjectVarName = 'INPUT_COMMIT_PROJECT';
  static const String _inputCommitRefVarName = 'INPUT_COMMIT_REF';

  final Sl4f _sl4f;
  final Dump _dump;

  /// Constructs a [Performance] object.
  Performance(this._sl4f, [Dump dump]) : _dump = dump ?? Dump();

  /// Closes the underlying HTTP client.
  ///
  /// This need not be called if the Sl4f client is closed instead.
  void close() {
    _sl4f.close();
  }

  /// Starts tracing for the given [duration].
  ///
  /// If [binary] is true, then the trace will be captured in Fuchsia Trace
  /// Format (by default, it is in Chrome JSON Format). If [compress] is true,
  /// the trace will be gzip-compressed. The trace output will be saved to a
  /// path implied by [traceName], [binary], and [compress], and can be
  /// retrieved later via [downloadTraceFile].
  Future<bool> trace(
      {@required Duration duration,
      @required String traceName,
      String categories,
      int bufferSize,
      bool binary = false,
      bool compress = false}) async {
    // Invoke `/bin/trace record --duration=$duration --categories=$categories
    // --output-file=$outputFile --buffer-size=$bufferSize` on the target
    // device via ssh.
    final durationSeconds = duration.inSeconds;
    String command = 'trace record --duration=$durationSeconds';
    if (categories != null) {
      command += ' --categories=$categories';
    }
    if (bufferSize != null) {
      command += ' --buffer-size=$bufferSize';
    }
    if (binary) {
      command += ' --binary';
    }
    if (compress) {
      command += ' --compress';
    }
    final String extension =
        _traceExtension(binary: binary, compress: compress);
    final outputFile = _traceNameToTargetPath(traceName, extension);
    if (outputFile != null) {
      command += ' --output-file=$outputFile';
    }
    final result = await _sl4f.ssh.run(command);
    return result.exitCode == 0;
  }

  /// Copies the trace file specified by [traceName] off of the target device,
  /// and then saves it to the dump directory.
  ///
  /// A [trace] call with the same [traceName], [binary], and [compress] must
  /// have successfully completed before calling [downloadTraceFile].
  ///
  /// Returns the download trace [File].
  Future<File> downloadTraceFile(String traceName,
      {bool binary = false, bool compress = false}) async {
    _log.info('Performance: Downloading trace $traceName');
    final String extension =
        _traceExtension(binary: binary, compress: compress);
    final tracePath = _traceNameToTargetPath(traceName, extension);

    var response = await _sl4f
        .request('traceutil_facade.GetTraceFile', {'path': tracePath});
    List<int> contents = base64.decode(response['data']);
    while (response.containsKey('next_offset')) {
      response = await _sl4f.request('traceutil_facade.GetTraceFile',
          {'path': tracePath, 'offset': response['next_offset']});
      contents += base64.decode(response['data']);
    }

    return _dump.writeAsBytes('$traceName-trace', extension, contents);
  }

  /// Starts a Chrome trace from the given [webSocketUrl] with the default
  /// categories.
  ///
  /// [webSocketUrl] can be obtained from
  /// [Webdriver.webSocketDebuggerUrlsForHost]. Returns a WebSocket object that
  /// is to be passed to [stopChromeTrace] to stop and download the trace data.
  ///
  /// TODO(35714): Allow tracing users to specify categories to trace.
  Future<WebSocket> startChromeTrace(String webSocketUrl) async {
    final webSocket = await WebSocket.connect(webSocketUrl);
    _log.info('Starting chrome trace');
    webSocket.add(json.encode({
      'jsonrpc': '2.0',
      'method': 'Tracing.start',
      'params': {},
      'id': 1,
    }));
    return webSocket;
  }

  /// Stops a Chrome trace that was started by [startChromeTrace] and writes it
  /// to a file.
  ///
  /// Returns the file containing the trace data. Calling [stopChromeTrace] on
  /// the same [webSocket] twice will throw an error.
  Future<File> stopChromeTrace(WebSocket webSocket,
      {@required String traceName}) async {
    _log.info('Stopping and saving chrome trace');
    webSocket.add(json.encode({
      'jsonrpc': '2.0',
      'method': 'Tracing.end',
      'params': {},
      'id': 2,
    }));

    final traceEvents = [];

    await for (final content in webSocket) {
      final obj = json.decode(content);
      if (obj['method'] == 'Tracing.tracingComplete') {
        break;
      } else if (obj['method'] == 'Tracing.dataCollected') {
        traceEvents.addAll(obj['params']['value']);
      }
    }
    await webSocket.close();

    _log.info('Writing chrome trace to file');
    return _dump.writeAsBytes('$traceName-chrome-trace', 'json',
        utf8.encode(json.encode(traceEvents)));
  }

  /// Combine [fuchsiaTrace] and [chromeTrace] into a merged JSON-format trace.
  ///
  /// [fuchsiaTrace] must be a trace file in JSON format (not FXT).
  Future<File> mergeTraces(
      {@required File fuchsiaTrace,
      @required File chromeTrace,
      @required String traceName}) async {
    final fuchsiaTraceData = json.decode(await fuchsiaTrace.readAsString());
    final chromeTraceData = json.decode(await chromeTrace.readAsString());

    final mergedTraceData = fuchsiaTraceData;
    mergedTraceData['traceEvents'].addAll(chromeTraceData);

    return _dump.writeAsBytes('$traceName-merged-trace', 'json',
        utf8.encode(json.encode(mergedTraceData)));
  }

  /// A helper function that runs a process with the given args.
  /// Required by the test to capture the parameters passed to [Process.run].
  ///
  /// Returns [true] if the process ran successufly, [false] otherwise.
  Future<bool> runProcess(String executablePath, List<String> args) async {
    final ProcessResult results = await Process.run(executablePath, args);
    _log..info(results.stdout)..info(results.stderr);
    return results.exitCode == 0;
  }

  /// Runs the provided [MetricsSpecSet] on the given [trace].
  /// It sets the ouptut file location to be the same as the source.
  /// It will also run the catapult converter if the [converterPath] was provided.
  ///
  /// The [converterPath] must be relative to the script path.
  ///
  /// [registry] defines the set of known metrics processors, which can be
  /// specified to allow processing of custom metrics.
  ///
  /// TODO(PT-216): Avoid explicitly passing the [converterPath].
  ///
  /// Returns the benchmark result [File] generated by the processor.
  Future<File> processTrace(MetricsSpecSet metricsSpecSet, File trace,
      {String converterPath,
      Map<String, MetricsProcessor> registry = defaultMetricsRegistry}) async {
    _log.info('Processing trace: ${trace.path}');
    final outputFileName =
        '${trace.parent.absolute.path}/${metricsSpecSet.testName}-benchmark.fuchsiaperf.json';

    final model = await createModelFromFile(trace);
    final List<Map<String, dynamic>> results = [];

    for (final metricsSpec in metricsSpecSet.metricsSpecs) {
      _log.info('Applying metricsSpec ${metricsSpec.name} to ${trace.path}');
      final testCaseResultss =
          processMetrics(model, metricsSpec, registry: registry);
      for (final testCaseResults in testCaseResultss) {
        results.add({
          'label': testCaseResults.label,
          'test_suite': metricsSpecSet.testName,
          'unit': unitToCatapultConverterString(testCaseResults.unit),
          'values': testCaseResults.values,
          'split_first': testCaseResults.splitFirst,
        });
      }
    }

    File(outputFileName)
      ..createSync()
      ..writeAsStringSync(json.encode(results));

    File processedResultFile = File(outputFileName);
    _log.info('Processing trace completed.');
    if (converterPath != null) {
      await convertResults(
          converterPath, processedResultFile, Platform.environment);
    }
    return processedResultFile;
  }

  /// TODO(39301): Replace all uses of this with [processTrace].
  Future<File> processTrace2(MetricsSpecSet metricsSpecSet, File trace,
          {String converterPath,
          Map<String, MetricsProcessor> registry =
              defaultMetricsRegistry}) async =>
      processTrace(metricsSpecSet, trace,
          converterPath: converterPath, registry: registry);

  /// A helper function that converts the results to the catapult format.
  ///
  /// Returns the converted benchmark result [File].
  ///
  /// TODO(fxb/23091): Remove the uploadToCatapultDashboard argument once all
  /// the performance tests are moved over to using SL4F and this argument is
  /// unused.
  Future<File> convertResults(
      String converterPath, File result, Map<String, String> environment,
      {bool uploadToCatapultDashboard = true}) async {
    _log.info('Converting the results into the catapult format');

    var bot = '', logurl = '', master = '', timestamp = 0;
    if (!environment.containsKey(_buildBucketIdVarName)) {
      _log.info(
          'convertResults: No $_buildBucketIdVarName, treating as a local run.');
      bot = 'local-bot';
      master = 'local-master';
      logurl = 'http://ci.example.com/build/300';
      timestamp = new DateTime.now().millisecondsSinceEpoch;
    } else {
      // Verify that all required environment variables are available.
      final builderName = environment[_builderNameVarName];
      final buildbucketId = environment[_buildBucketIdVarName];
      final buildCreateTime = environment[_buildCreateTimeVarName];
      final inputCommitRef = environment[_inputCommitRefVarName];
      final inputCommitHost = environment[_inputCommitHostVarName];
      final inputCommitProject = environment[_inputCommitProjectVarName];
      if (buildbucketId == null ||
          builderName == null ||
          buildCreateTime == null ||
          inputCommitRef == null ||
          inputCommitHost == null ||
          inputCommitProject == null) {
        _log.warning('Some required environment variables are not available. '
            'Current available variables are: ${environment.keys}');
        return null;
      }

      logurl = 'https://ci.chromium.org/b/$buildbucketId';
      bot = builderName;
      timestamp = int.parse(buildCreateTime);
      master =
          '${inputCommitHost.replaceFirst('.googlesource.com', '')}.$inputCommitProject';

      const releasesRefPrefix = 'refs/heads/releases/';
      if (inputCommitRef.startsWith(releasesRefPrefix)) {
        master += '.${inputCommitRef.substring(releasesRefPrefix.length)}';
      } else {
        assert(inputCommitRef == 'refs/heads/master');
      }
    }

    final resultsPath = result.absolute.path;
    assert(resultsPath.endsWith('.fuchsiaperf.json'));
    // The infra recipe looks for the filename extension '.catapult_json',
    // so uploading to the Catapult performance dashboard is disabled if we
    // use a different extension.
    final catapultExtension = uploadToCatapultDashboard
        ? '.catapult_json'
        : '.catapult_json_disabled';
    final outputFileName = resultsPath.replaceFirst(
        RegExp(r'\.fuchsiaperf\.json$'), catapultExtension);

    final List<String> args = [
      '--input',
      result.absolute.path,
      '--output',
      outputFileName,
      '--execution-timestamp-ms',
      timestamp.toString(),
      '--masters',
      master,
      '--log-url',
      logurl,
      '--bots',
      bot
    ];

    final converter = Platform.script.resolve(converterPath).toFilePath();

    if (!await runProcess(converter, args)) {
      _log.warning('Running the results converter failed.');
      return null;
    }
    _log.info('Conversion to catapult results format completed.');
    return Future.value(File(outputFileName));
  }
}
