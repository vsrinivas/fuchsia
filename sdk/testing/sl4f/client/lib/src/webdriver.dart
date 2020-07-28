// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'dart:io' as io;

import 'package:logging/logging.dart';
import 'package:sl4f/sl4f.dart';
import 'package:retry/retry.dart';
import 'package:webdriver/sync_core.dart' show WebDriver, NoSuchWindowException;
import 'package:webdriver/sync_io.dart' as sync_io;

final _log = Logger('Webdriver');

/// `WebDriverConnector` is a utility for host-driven tests that control Chrome
/// contexts running on a remote device under test(DuT).  `WebDriverConnector`
/// vends `WebDriver` objects connected to remote Chrome instances.
/// Check the [webdriver package](https://pub.dev/documentation/webdriver/)
/// documentation for details on using `WebDriver`.
///
/// `WebDriverConnector` additionally starts an instance of the ChromeDriver
/// binary that runs locally on the test host.  `WebDriver` instances
/// communicate with ChromeDriver, which in turn communicates with Chrome
/// instances on the DuT.
///
/// TODO(satsukiu): Add e2e test for facade functionality
class WebDriverConnector {
  /// Relative path of chromedriver binary, only provided if an existing
  /// chromedriver is not already running.
  final String _chromedriverPath;

  /// SL4F client.
  final Sl4f _sl4f;

  /// Helper for starting processes.
  ///
  /// Will be null if constructed with [fromExistingChromedriver()].
  final ProcessHelper _processHelper;

  /// Helper for instantiating WebDriver objects.
  final WebDriverHelper _webDriverHelper;

  /// A handle to the process running Chromedriver.
  ///
  /// Will be null if constructed using [fromExistingChromedriver()], or if
  /// [initialize()] hasn't been called.
  io.Process _chromedriverProcess;
  io.Process get chromedriverProcess => _chromedriverProcess;

  /// The port Chromedriver is listening on.
  ///
  /// If the [fromExistingChromedriver()] constructor is used, Chromedriver has
  /// already started; use that port. If it isn't passed in, an unused port is
  /// picked.
  int _chromedriverPort;

  /// A mapping from an exposed port number on the DUT to an open WebDriver
  /// session.
  Map<int, WebDriverSession> _webDriverSessions;

  WebDriverConnector(String chromeDriverPath, Sl4f sl4f,
      {ProcessHelper processHelper, WebDriverHelper webDriverHelper})
      : _chromedriverPath = chromeDriverPath,
        _sl4f = sl4f,
        _processHelper = processHelper ?? ProcessHelper(),
        _webDriverHelper = webDriverHelper ?? WebDriverHelper(),
        _webDriverSessions = {};

  WebDriverConnector.fromExistingChromedriver(int chromedriverPort, Sl4f sl4f)
      : _chromedriverPort = chromedriverPort,
        _sl4f = sl4f,
        _webDriverHelper = WebDriverHelper(),
        _webDriverSessions = {},
        // Chromedriver is already running so the below are set to null.
        _chromedriverPath = null,
        _processHelper = null;

  /// Starts ChromeDriver (if not already running) and enables DevTools for any
  /// future created Chrome contexts.
  ///
  /// As this will not enable DevTools on any already opened contexts,
  /// `initialize` must be called prior to the instantiation of the Chrome
  /// context that needs to be driven.
  Future<void> initialize() async {
    if (_chromedriverPort == null) {
      await _startChromedriver();
    }
    // TODO(satsukiu): return a nicer error, or don't fail if devtools is already enabled
    await _sl4f.request('webdriver_facade.EnableDevTools');
  }

  /// Stops Chromedriver and removes any connections that are still open.
  Future<void> tearDown() async {
    if (_chromedriverProcess != null) {
      _log.info('Stopping chromedriver');
      _chromedriverProcess.kill();
      await _chromedriverProcess.exitCode.timeout(Duration(seconds: 5),
          onTimeout: () {
        _log.warning('Chromedriver did not shut down, killing it.');
        _chromedriverProcess.kill(io.ProcessSignal.sigkill);
        return _chromedriverProcess.exitCode;
      });
      _chromedriverProcess = null;
      _chromedriverPort = null;
    }

    for (final session in _webDriverSessions.entries) {
      await _dropForwardedPort(session.value.openPort);
    }
    _webDriverSessions = {};
  }

  /// Get all nonEmpty Urls obtained from current _webDriverSessions.
  Iterable<String> get sessionsUrls => _webDriverSessions.values
      .map((session) => session.webDriver.currentUrl)
      .where((url) => url.isNotEmpty);

  /// Search for Chrome contexts based on the host of the currently displayed
  /// page and return their entries.
  ///
  /// For a returned entry, entry.key is the port, and entry.value is the
  /// WebDriver object.
  Future<List<WebDriverSession>> _webDriverSessionsForHost(String host) async {
    await _updateWebDriverSessions();
    final hosts = List.from(_webDriverSessions.values
        .map((session) => Uri.parse(session.webDriver.currentUrl).host));
    _log.fine('All webdriver host urls: $hosts');
    return List.from(_webDriverSessions.values.where(
        (session) => Uri.parse(session.webDriver.currentUrl).host == host));
  }

  /// Searches for Chrome contexts based on the host of the currently displayed
  /// page, and returns `WebDriver` connections to the found contexts.
  Future<List<WebDriver>> webDriversForHost(String host) async {
    _log.info('Finding webdrivers for $host');
    return List.from((await _webDriverSessionsForHost(host))
        .map((session) => session.webDriver));
  }

  /// Checks whether a debugging [endpoint] matches the specified [filters].
  ///
  /// To match, for each (key, value) pair in [filters] must have key present in
  /// the [endpoint] object, and the corresponding values must be equal.
  bool _checkDebuggerEndpointFilters(
      Map<String, dynamic> endpoint, Map<String, dynamic> filters) {
    if (filters == null) {
      return true;
    }
    for (final key in filters.keys) {
      if (!endpoint.containsKey(key) || endpoint[key] != filters[key]) {
        return false;
      }
    }
    return true;
  }

  /// Obtains a list of URLs for connecting to the DevTools debugger via
  /// websockets.
  ///
  /// The websocket targets are pulled from [the /json
  /// endpoint](https://chromedevtools.github.io/devtools-protocol/#endpoints).
  /// To be returned, an endpoint's url field must match the specified [host],
  /// and for each (key, value) pair in [filters], the corresponding
  /// field in the endpoint description must be present and equal to the
  /// specified value.
  ///
  /// This may return more than one URL per context, as a context can have
  /// multiple debugging targets, and more than one of them may match the
  /// given host.
  Future<List<String>> webSocketDebuggerUrlsForHost(String host,
      {Map<String, dynamic> filters}) async {
    final portsForHost = (await _webDriverSessionsForHost(host))
        .map((session) => session.openPort);

    final devToolsUrls = <String>[];
    for (final port in portsForHost) {
      final request = await io.HttpClient()
          .getUrl(Uri.parse('http://${_sl4f.target}:$port/json'));
      final response = await request.close();
      final endpoints = json.decode(await utf8.decodeStream(response));

      for (final endpoint in endpoints) {
        if (_checkDebuggerEndpointFilters(endpoint, filters)) {
          devToolsUrls.add(endpoint['webSocketDebuggerUrl']);
        }
      }
    }

    return devToolsUrls;
  }

  /// Starts Chromedriver on the host.
  Future<void> _startChromedriver() async {
    if (_chromedriverProcess == null) {
      _chromedriverPort = await _sl4f.ssh.pickUnusedPort();

      final chromedriver =
          io.Platform.script.resolve(_chromedriverPath).toFilePath();
      final args = ['--port=$_chromedriverPort'];
      _chromedriverProcess = await _processHelper.start(chromedriver, args);
      _chromedriverProcess.stderr
          .transform(utf8.decoder)
          .transform(const LineSplitter())
          .listen((error) {
        _log.info('[Chromedriver] $error');
      });

      _chromedriverProcess.stdout
          .transform(utf8.decoder)
          .transform(const LineSplitter())
          .listen((log) {
        _log.info('[Chromedriver] $log');
      });
    }
  }

  /// Updates the set of open WebDriver connections.
  Future<void> _updateWebDriverSessions() async {
    final remotePortsResult =
        await _sl4f.request('webdriver_facade.GetDevToolsPorts');

    final ports = Set.from(remotePortsResult['ports']);

    // Remove port forwarding for any ports that aren't open or shown.
    _webDriverSessions.removeWhere((port, session) {
      if (!ports.contains(port) || !_isSessionDisplayed(session)) {
        _dropForwardedPort(port);
        return true;
      }
      return false;
    });

    // Add new sessions for new ports.
    for (final remotePort in ports) {
      final webDriverSession = await _createWebDriverSession(remotePort);
      _webDriverSessions.putIfAbsent(remotePort, () => webDriverSession);
    }
  }

  bool _isSessionDisplayed(WebDriverSession session) {
    try {
      session.webDriver.window;
    } on NoSuchWindowException {
      return false;
    }

    return true;
  }

  /// Creates a `Webdriver` connection using the specified port.  Retries
  /// on errors that may occur due to network issues.
  Future<WebDriverSession> _createWebDriverSession(int targetPort,
      {int tries = 5}) async {
    // For a given Chrome context listening on port p on the DuT, we open a
    // proxy on the DuT that forwards Dut:x to Dut:p. We then create a WebDriver
    // pointing at Dut:x.
    final openPort = await _forwardPort(targetPort);
    try {
      final webDriver = await retry(
        () => _webDriverHelper.createDriver(
            _sl4f.target, openPort, targetPort, _chromedriverPort),
        maxAttempts: tries,
      );
      return WebDriverSession(openPort, webDriver);
    } on Exception catch (e) {
      _log.warning('Error creating driver: $e');
      rethrow;
    }
  }

  Future<int> _forwardPort(int targetPort) async {
    final response = await _sl4f.request('proxy_facade.OpenProxy', targetPort);
    return response;
  }

  /// Indicate to the DUT we no longer need a targetPort. Forwarding stops if
  /// no other clients are using the proxy.
  Future<void> _dropForwardedPort(int targetPort) =>
      _sl4f.request('proxy_facade.DropProxy', targetPort);
}

/// A representation of a `WebDriver` connection from a host device to a DUT.
class WebDriverSession {
  /// The remote port exposed on the DUT.
  final int openPort;

  /// The webdriver connection.
  final WebDriver webDriver;

  WebDriverSession(this.openPort, this.webDriver);
}

/// A wrapper around static dart:io Process methods.
class ProcessHelper {
  ProcessHelper();

  /// Start a new process.
  Future<io.Process> start(String cmd, List<String> args,
          {bool runInShell = false}) =>
      io.Process.start(cmd, args, runInShell: runInShell);
}

/// A wrapper around static WebDriver creation methods.
class WebDriverHelper {
  WebDriverHelper();

  /// Create a new WebDriver pointing to Chromedriver on the given uri and with
  /// given desired capabilities.
  WebDriver createDriver(
      String target, int openPort, int remotePort, int chromedriverPort) {
    final modTarget = target == '[::1]'
        ? 'localhost'
        : target; // chromedriver does not like ipv6.
    final chromeOptions = {
      'debuggerAddress': '$modTarget:$openPort',
    };
    final capabilities = sync_io.Capabilities.chrome;
    capabilities[sync_io.Capabilities.chromeOptions] = chromeOptions;
    return sync_io.createDriver(
        desired: capabilities,
        uri: Uri.parse('http://localhost:$chromedriverPort'));
  }
}
