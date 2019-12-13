// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter_driver/flutter_driver.dart';
import 'package:fuchsia_remote_debug_protocol/fuchsia_remote_debug_protocol.dart'
    as frdp;
import 'package:logging/logging.dart';
import 'package:quiver/check.dart';
import 'package:sl4f/sl4f.dart';

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

  frdp.FuchsiaRemoteConnection _connection;

  FlutterDriverConnector(this._sl4f);

  /// Initializes the connection to fuchsia.
  Future<void> initialize() async {
    _logger.info('Initializing Flutter port forwarding to Fuchsia...');
    if (_connection != null) {
      return;
    }
    frdp.fuchsiaPortForwardingFunction = _sl4fPortForwardingFunction;
    _connection =
        await frdp.FuchsiaRemoteConnection.connectWithSshCommandRunner(
            _Sl4fCommandRunner(_sl4f.ssh));
    _logger.info('Connected to FlutterDriver port on device.');
  }

  /// Shutdown the connection to flutter runner on the DUT.
  ///
  /// Must call this to cancel any port forwarding on the local host.
  Future<void> tearDown() async {
    _logger.info('(Tearing down FlutterDriver connection.)');
    await _connection?.stop();
    _connection = null;
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

  /// Sets up port forwarding using our SSH facilities.
  ///
  /// Only [remotePort] is used, the other parameters are not needed when using
  /// [Ssh]. Will throw [PortForwardException] on failure.
  Future<frdp.PortForwarder> _sl4fPortForwardingFunction(
      String addr, int remotePort,
      [String iface, String cfgFile]) async {
    final localPort = await _sl4f.ssh.forwardPort(remotePort: remotePort);
    return _Sl4fPortForwarder(localPort, remotePort, _sl4f.ssh);
  }
}

/// A wrapper around our Ssh class for Flutter's FuchsiaRemoteConnection class.
class _Sl4fCommandRunner extends frdp.SshCommandRunner {
  final Ssh _ssh;
  _Sl4fCommandRunner(this._ssh) : super(address: _fixAddress(_ssh.target));

  // Uri.parseIpv6Address doesn't support RFC6874 addresses so they need to be
  // cleaned to avoid failures. More specifically, it does not parse zone-id.
  // See: https://howdoesinternetwork.com/2013/ipv6-zone-id
  static String _fixAddress(String address) {
    return address.split('%').first;
  }

  @override
  Future<List<String>> run(String cmd) async {
    final result = await _ssh.run(cmd);
    if (result.exitCode != 0) {
      throw frdp.SshCommandError('SSH Command failed: $cmd');
    }
    return result.stdout.split('\n');
  }
}

/// Keeps track of a forwarded port.
class _Sl4fPortForwarder extends frdp.PortForwarder {
  @override
  final int port;

  @override
  final int remotePort;

  final Ssh _ssh;

  _Sl4fPortForwarder(this.port, this.remotePort, this._ssh);

  @override
  Future<void> stop() =>
      _ssh.cancelPortForward(port: port, remotePort: remotePort);
}
