// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:flutter_driver/flutter_driver.dart';
import 'package:fuchsia_remote_debug_protocol/fuchsia_remote_debug_protocol.dart'
    as frdp;
import 'package:logging/logging.dart';
import 'package:quiver/check.dart';
import 'package:retry/retry.dart';
import 'package:sl4f/sl4f.dart';

String getVmServicePortFromInspectSnapshot(
    List<Map<String, dynamic>> inspectSnapshot) {
  for (Map<String, dynamic> item in inspectSnapshot) {
    final payload = item['payload'];
    if (payload == null) continue;

    final root = payload['root'];
    if (root == null) continue;

    final runner = root['runner'];
    if (runner == null) continue;

    return runner['vm_service_port'];
  }
  return null;
}

/// Controls connections to the DUT's observatory.
///
/// This class wraps around Flutter's fuchsia_remote_debug_protocol library to
/// use our existing SSH connection and port forwarding capabilities.
///
/// The expected usage is:
///   setUpAll(() {
///     ...
///     final connector = FlutterDriverConnector(sl4f)..initialize();
///     driver = await connector.driverForIsolate('my_isolate');
///   });
///   tearDownAll((() {
///     ...
///     driver.close();
///     connector.tearDown();
///     sl4f.tearDown();
///     ...
///   });
///   test(() {
///     driver.enterText(); // or whatever
///     ...
///   });
class FlutterDriverConnector {
  final _logger = Logger('fuchsia_flutter_driver_connector');
  final Sl4f _sl4f;
  final Inspect _inspect;
  final TcpProxyController _proxyController;
  static const maxInspectSnapshotAttempts = 5;

  frdp.FuchsiaRemoteConnection _connection;

  FlutterDriverConnector(this._sl4f)
      : _proxyController = _sl4f.proxy,
        _inspect = Inspect(_sl4f);

  /// Initializes the connection to fuchsia.
  ///
  /// sshCommandRunner - An optional [frdp.SshCommandRunner] that can be used
  /// to override commands sent to the device.
  /// TODO(http://fxb/89277): Remove after support for flutter runner CFv2 is
  /// fixed.
  Future<void> initialize([frdp.SshCommandRunner sshCommandRunner]) async {
    _logger.info('Initializing Flutter port forwarding to Fuchsia...');
    if (_connection != null) {
      return;
    }
    frdp.fuchsiaPortForwardingFunction = _sl4fPortForwardingFunction;
    _connection =
        await frdp.FuchsiaRemoteConnection.connectWithSshCommandRunner(
            sshCommandRunner ?? _Sl4fCommandRunner(_sl4f.ssh));
    _logger.info('Connected to FlutterDriver port on device.');
  }

  /// Shutdown the connection to flutter runner on the DUT.
  ///
  /// Must call this to cancel any port forwarding on the local host.
  Future<void> tearDown() async {
    _logger.info('(Tearing down FlutterDriver connection.)');
    await _connection?.stop();
    await _proxyController.stopAllProxies();
    _connection = null;
  }

  /// Connects to [FlutterDriver] for an isolate that matches [namePattern] and
  /// [selector].
  ///
  /// Note that if no isolates match namePattern, this function will wait until
  /// one comes up, for up to 1 minute after which it returns null. See also
  /// [frdp.FuchsiaRemoteConnection.getMainIsolatesByPattern].
  ///
  /// [selector] is the producer of the diagnostics data.
  ///
  /// [printCommunication] and [logCommunicationToFile] are forwarded to
  /// [FlutterDriver.connect] so they have the same effect.
  Future<FlutterDriver> driverForIsolateBySelector(
    String namePattern,
    String selector, {
    bool printCommunication = false,
    bool logCommunicationToFile = false,
  }) async {
    return retry(
      () async {
        final inspectSnapshot = await _inspect.snapshot(['$selector:root']);
        if (inspectSnapshot == null || inspectSnapshot.isEmpty) {
          throw Exception('Inspect Snapshot for $selector could not be found.');
        }
        final vmServicePort =
            getVmServicePortFromInspectSnapshot(inspectSnapshot);
        if (vmServicePort == null) {
          _logger.severe('Could not find the vm service port for $selector');
          return null;
        }

        final openPort =
            await _proxyController.openProxy(int.parse(vmServicePort));
        final vmUri = Uri(
            scheme: 'ws', host: _sl4f.ssh.target, port: openPort, path: '/ws');

        final dartVm = await frdp.DartVm.connect(vmUri);
        final isolates = await dartVm.getMainIsolatesByPattern(namePattern);
        return isolates == null || isolates.isEmpty
            ? null
            : FlutterDriver.connect(
                dartVmServiceUrl: vmUri.toString(),
                isolateNumber: isolates.first.number,
                printCommunication: printCommunication,
                logCommunicationToFile: logCommunicationToFile);
      },
      retryIf: (e) => e is Exception,
      maxAttempts: maxInspectSnapshotAttempts,
    );
  }

  /// Connects to [FlutterDriver] for an isolate that matches [namePattern].
  ///
  /// Note that if no isolates match namePattern, this function will wait until
  /// one comes up, for up to 1 minute after which it returns null. See also
  /// [frdp.FuchsiaRemoteConnection.getMainIsolatesByPattern].
  ///
  /// [printCommunication] and [logCommunicationToFile] are forwarded to
  /// [FlutterDriver.connect] so they have the same effect.
  Future<FlutterDriver> driverForIsolate(
    Pattern namePattern, {
    String componentMoniker,
    bool printCommunication = false,
    bool logCommunicationToFile = false,
  }) async {
    final isolate = await this.isolate(namePattern);
    return isolate == null
        ? null
        : FlutterDriver.connect(
            dartVmServiceUrl: isolate.dartVm.uri.toString(),
            isolateNumber: isolate.number,
            printCommunication: printCommunication,
            logCommunicationToFile: logCommunicationToFile);
  }

  /// Gets the Url and isolate number of an isolate that matches pattern.
  ///
  /// If more than one isolate matches, it returns the first one.
  Future<frdp.IsolateRef> isolate(Pattern namePattern) async {
    checkState(_connection != null,
        message: 'initialize() has not been called');
    final isolates = await _connection.getMainIsolatesByPattern(namePattern);
    if (isolates.isEmpty) {
      _logger.severe('Could not find any isolate for $namePattern');
      return null;
    }
    return isolates.first;
  }

  /// Sets up port forwarding using our TCP proxy.
  ///
  /// Only [remotePort] is used, the other parameters are not needed when using
  /// [Ssh]. Will throw [PortForwardException] on failure.
  Future<frdp.PortForwarder> _sl4fPortForwardingFunction(
      String addr, int remotePort,
      [String iface, String cfgFile]) async {
    final openPort = await _proxyController.openProxy(remotePort);
    return _Sl4fPortForwarder(openPort, addr, remotePort, _proxyController);
  }
}

/// A wrapper around our Ssh class for Flutter's FuchsiaRemoteConnection class.
class _Sl4fCommandRunner extends frdp.SshCommandRunner {
  final Ssh _ssh;
  _Sl4fCommandRunner(this._ssh) : super(address: _ssh.target);

  @override
  Future<List<String>> run(String cmd) async {
    final result = await _ssh.run(cmd);
    if (result.exitCode != 0) {
      throw frdp.SshCommandError(
          'SSH Command failed: $cmd\nstdout: ${result.stdout}\nstderr: ${result.stderr}');
    }
    return result.stdout.split('\n');
  }
}

/// Keeps track of a forwarded port.
class _Sl4fPortForwarder extends frdp.PortForwarder {
  @override
  final int port;

  @override
  final String openPortAddress;

  @override
  final int remotePort;

  final TcpProxyController _proxyController;

  _Sl4fPortForwarder(
      this.port, this.openPortAddress, this.remotePort, this._proxyController);

  @override
  Future<void> stop() async => await _proxyController.dropProxy(remotePort);
}
