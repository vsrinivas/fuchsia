// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:convert';
import 'dart:io' as io;

import 'package:logging/logging.dart';
import 'package:retry/retry.dart';
import 'package:sl4f/sl4f.dart';
// Capabilities and exceptions are shared between async and sync.
import 'package:webdriver/async_core.dart'
    show Capabilities, NoSuchWindowException, WebDriverException;
import 'package:webdriver/async_core.dart' as async_core;
import 'package:webdriver/async_io.dart' as async_io;
import 'package:webdriver/sync_core.dart' as sync_core;
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
/// TODO(robertma): Consider removing fromExistingChromedriver and renaming it.
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

  /// Helper for forwarding ports.
  final PortForwarder _portForwarder;
  PortForwarder get portForwarder => _portForwarder;

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

  /// A mapping from a target port number on the DUT to an open WebDriver
  /// session.
  Map<int, WebDriverSession> _webDriverSessions;

  WebDriverConnector(String chromeDriverPath, Sl4f sl4f,
      {ProcessHelper processHelper,
      WebDriverHelper webDriverHelper,
      PortForwarder portForwarder})
      : _chromedriverPath = chromeDriverPath,
        _sl4f = sl4f,
        _processHelper = processHelper ?? ProcessHelper(),
        _webDriverHelper = webDriverHelper ?? WebDriverHelper(),
        _portForwarder = portForwarder ?? PortForwarder.fromSl4f(sl4f),
        _webDriverSessions = {};

  WebDriverConnector.fromExistingChromedriver(int chromedriverPort, Sl4f sl4f)
      : _chromedriverPort = chromedriverPort,
        _sl4f = sl4f,
        _webDriverHelper = WebDriverHelper(),
        _webDriverSessions = {},
        _portForwarder = PortForwarder.fromSl4f(sl4f),
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
      await _portForwarder.stopPortForwarding(
          session.value.accessPoint, session.key);
    }
    _webDriverSessions = {};
    await _portForwarder.tearDown();
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
    return List.from(_webDriverSessions.values.where((session) {
      final url = session.webDriver.currentUrl;
      return url != null && Uri.parse(url).host == host;
    }));
  }

  /// Searches for Chrome contexts based on the host of the currently displayed
  /// page, and returns `WebDriver` connections to the found contexts.
  Future<List<sync_core.WebDriver>> webDriversForHost(String host) async {
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
    final accessPointsForHost = (await _webDriverSessionsForHost(host))
        .map((session) => session.accessPoint);

    final devToolsUrls = <String>[];
    for (final accessPoint in accessPointsForHost) {
      final request = await io.HttpClient().getUrl(Uri(
          scheme: 'http',
          host: accessPoint.host,
          port: accessPoint.port,
          path: 'json'));
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

    // TODO(b/191696991): This ensures that we always check port 9222 for a
    // debugging endpoint. With the componentization of webengine, Cast
    // applications will open a debugging endpoint on port 9222 but the
    // component responsible for them does not implement the fuchsia.web.Debug
    // API, and therefore does not inform SL4F when the port is open. In the
    // long term, webengine is likely to become more componentized and not
    // necessarily with a predictable debugging port, so we will need a suitable
    // evolution for the Debug API.
    // ignore: cascade_invocations
    ports.add(9222);

    // To accommodate the fact that we don't actually know if there is a debug
    // endpoint on port 9222, we add the new sessions before removing any that
    // are not displayed, so the result is that 9222 will be added and
    // immediately removed if it is not actually open.
    for (final remotePort in ports) {
      if (!_webDriverSessions.containsKey(remotePort)) {
        final webDriverSession = await _createWebDriverSession(remotePort);
        if (webDriverSession != null) {
          _webDriverSessions[remotePort] = webDriverSession;
        }
      }
    }

    // Remove port forwarding for any ports that aren't open or shown.
    final removedSessions = [];
    _webDriverSessions.removeWhere((port, session) {
      if (!ports.contains(port) || !_isSessionDisplayed(session)) {
        removedSessions.add(MapEntry(port, session));
        return true;
      }
      return false;
    });
    for (final removedSession in removedSessions) {
      await _portForwarder.stopPortForwarding(
          removedSession.value.accessPoint, removedSession.key);
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
  Future<WebDriverSession> _createWebDriverSession(int remotePort,
      {int tries = 5}) async {
    final accessPoint = await _portForwarder.forwardPort(remotePort);
    final webDriver = await retry(
      () async {
        return await _webDriverHelper.createDriver(
            accessPoint, _chromedriverPort);
      },
      maxAttempts: tries,
    );

    if (webDriver == null) {
      return null;
    }

    return WebDriverSession(accessPoint, webDriver);
  }
}

/// A WebDriver connector that maintains a single DevTools connection at a time.
class SingleWebDriverConnector {
  /// SL4F client.
  final Sl4f _sl4f;

  /// Helper for instantiating WebDriver objects.
  final WebDriverHelper _webDriverHelper;

  /// Helper for forwarding ports.
  final PortForwarder _portForwarder;
  PortForwarder get portForwarder => _portForwarder;

  /// The URI of ChromeDriver.
  final Uri _chromeDriverUri;

  /// The current [WebDriver].
  async_core.WebDriver _webDriver;

  /// The DevTools port on the DUT. Note that this is not guaranteed to be
  /// accessible from the host; use [_devtoolsAccessPoint] instead.
  int _devtoolsDevicePort;

  /// The access point for the current DevTools usable on the host.
  HostAndPort _devtoolsAccessPoint;

  SingleWebDriverConnector(Uri chromeDriverUri, Sl4f sl4f,
      {WebDriverHelper webDriverHelper, PortForwarder portForwarder})
      : _chromeDriverUri = chromeDriverUri,
        _sl4f = sl4f,
        _webDriverHelper = webDriverHelper ?? WebDriverHelper(),
        _portForwarder = portForwarder ?? PortForwarder.fromSl4f(sl4f);

  /// Enables DevTools for any future created Chrome contexts.
  ///
  /// As this will not enable DevTools on any already opened contexts,
  /// `initialize` must be called prior to the instantiation of the Chrome
  /// context that needs to be driven.
  Future<void> initialize() async {
    await _sl4f.request('webdriver_facade.EnableDevTools');
  }

  /// Drops the connection if it is still open.
  Future<void> tearDown() async {
    await _maybeDropProxy();
    await _portForwarder.tearDown();
  }

  /// Searches for a Chrome context whose current URL satisfies [urlMatcher].
  /// Returns null if none is found.
  Future<async_core.WebDriver> webDriverForUrl(
      bool Function(String) urlMatcher) async {
    if (await _checkCurrentWebDriver(urlMatcher)) {
      return _webDriver;
    }

    final remotePortsResult =
        await _sl4f.request('webdriver_facade.GetDevToolsPorts');
    final ports = Set.from(remotePortsResult['ports'])
      ..add(9222); // TODO(b/191696991): always check port 9222.

    for (final remotePort in ports) {
      _log.fine('Trying DevTools on device port $remotePort');
      await _recreateWebDriver(remotePort);
      if (await _checkCurrentWebDriver(urlMatcher)) {
        _log.info('Connected to DevTools on device port $remotePort');
        return _webDriver;
      }
    }
    return null;
  }

  /// Searches for a Chrome context whose currently displayed page matches one
  /// of the provided [hosts]. Returns null if none is found.
  Future<async_core.WebDriver> webDriverForHosts(List<String> hosts) {
    return webDriverForUrl((url) => hosts.contains(Uri.parse(url).host));
  }

  Future<bool> _checkCurrentWebDriver(bool Function(String) urlMatcher) async {
    if (_webDriver == null) {
      _log.fine('No webdriver.');
      return false;
    }
    try {
      await _webDriver.window;
    } on WebDriverException {
      _log.fine('No current window.');
      return false;
    }
    final url = await _webDriver.currentUrl;
    if (url == null) {
      _log.fine('No current URL.');
      return false;
    }
    // Truncate extremely long URLs in logs (e.g. data URLs).
    if (url.length > 80) {
      _log.fine('Current URL: ${url.substring(0, 80)}... (truncated)');
    } else {
      _log.fine('Current URL: $url');
    }
    return urlMatcher(url);
  }

  /// Creates a new Webdriver connection using the specified DUT port.
  ///
  /// It will first drop the current connection if there is one.
  Future<void> _recreateWebDriver(int remotePort) async {
    if (_webDriver != null) {
      try {
        await _webDriver.quit();
      } on WebDriverException {
        // Exceptions are safe to ignore here; we will create a new session.
      }
      _webDriver = null;
    }
    await _maybeDropProxy();
    _devtoolsDevicePort = remotePort;
    _devtoolsAccessPoint = await _portForwarder.forwardPort(remotePort);
    // Do not retry here. ChromeDriver already retries connections to DevTools
    // internally for up to 60s (hard-coded).
    // https://source.chromium.org/chromium/chromium/src/+/main:chrome/test/chromedriver/chrome_launcher.cc;l=390;drc=e35572b59f0e12a3b98a8565e714dc6ce65f9ae4
    try {
      _webDriver = await _webDriverHelper.createAsyncDriver(
          _devtoolsAccessPoint, _chromeDriverUri);
    } on WebDriverException catch (e) {
      // Do not throw so we may try the next port.
      _log.fine('Failed to create WebDriver: $e');
    }
  }

  Future<void> _maybeDropProxy() async {
    if (_devtoolsDevicePort != null && _devtoolsAccessPoint != null) {
      await _portForwarder.stopPortForwarding(
          _devtoolsAccessPoint, _devtoolsDevicePort);
    }
    _devtoolsDevicePort = null;
    _devtoolsAccessPoint = null;
  }
}

/// A host and port pair.
class HostAndPort {
  final String host;
  final int port;
  HostAndPort(this.host, this.port);
}

/// A representation of a `WebDriver` connection from a host device to a DUT.
class WebDriverSession {
  /// The host and port through which the debug port is accessible.
  final HostAndPort accessPoint;

  /// The webdriver connection.
  final sync_core.WebDriver webDriver;

  WebDriverSession(this.accessPoint, this.webDriver);
}

abstract class PortForwarder {
  factory PortForwarder.fromSl4f(Sl4f sl4f) {
    // Chromedriver can't handle zone-id in ipv6 addresses. Since the TCP proxy requires
    // Chromedriver to call the target address, fall back to ssh in the cases Chromedriver can't
    // handle.
    if (sl4f.target.startsWith('[') && sl4f.target.contains('%')) {
      _log.warning('Using SSH to forward webdriver ports.');
      return SshPortForwarder(sl4f);
    }
    return TcpPortForwarder(sl4f);
  }

  /// Open a tunnel to `targetPort` on the DUT. Returns the host and port through
  /// which the tunnel is accessible.
  Future<HostAndPort> forwardPort(int targetPort);

  /// Stop forwarding a port previously opened with `forwardPort`.
  Future<void> stopPortForwarding(HostAndPort openAddr, int targetPort);

  /// Stop all proxies. Intended as a teardown step to clean up remaining proxies at the end of a
  /// test.
  Future<void> tearDown();
}

/// A PortForwarder that uses SSH.
class SshPortForwarder implements PortForwarder {
  final Sl4f _sl4f;

  @override
  Future<HostAndPort> forwardPort(int targetPort) async {
    final openPort = await _sl4f.ssh.forwardPort(remotePort: targetPort);
    return HostAndPort('localhost', openPort);
  }

  @override
  Future<void> stopPortForwarding(HostAndPort openAddr, int targetPort) async =>
      await _sl4f.ssh
          .cancelPortForward(port: openAddr.port, remotePort: targetPort);

  @override
  Future<void> tearDown() async {}

  SshPortForwarder(this._sl4f);
}

/// A PortForwarder that uses the TCP proxy on the DUT.
class TcpPortForwarder implements PortForwarder {
  final String _targetHost;
  final int _hostPort;
  final TcpProxyController proxyControl;

  @override
  Future<HostAndPort> forwardPort(int targetPort) async {
    final openPort = await proxyControl.openProxy(targetPort);
    return HostAndPort(_targetHost, _hostPort ?? openPort);
  }

  @override
  Future<void> stopPortForwarding(HostAndPort openAddr, int targetPort) async =>
      await proxyControl.dropProxy(targetPort);

  @override
  Future<void> tearDown() async => await proxyControl.stopAllProxies();

  /// Creates a TcpPortForwarder.
  ///
  /// Callers can optionally provide:
  /// * [proxyPort]: The port number on the DUT for TCP proxy to use.
  /// * [hostPort]: The host port that is forwarded to DUT [proxyPort].
  /// * [targetHost]: The domain name of the host (defaults to [sl4f.target]).
  /// This is useful for e.g. QEMU user-mode networking where a static port
  /// forwarding is set up before starting the virtual device.
  TcpPortForwarder(Sl4f sl4f, {int proxyPort, int hostPort, String targetHost})
      : proxyControl = proxyPort == null
            ? sl4f.proxy
            : TcpProxyController(sl4f, proxyPorts: [proxyPort]),
        _hostPort = hostPort,
        _targetHost = targetHost ?? sl4f.target;
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
  final io.HttpClient _httpClient;

  /// Creates a WebDriverHelper.
  ///
  /// If an [HttpClient] is provided, it'll be used by everything *except* the
  /// sync [WebDriver] from [createDriver].
  WebDriverHelper({io.HttpClient httpClient})
      : _httpClient = httpClient ?? io.HttpClient();

  Map<String, dynamic> _capabilities(HostAndPort debuggerAddress) {
    final chromeOptions = {
      'debuggerAddress': '${debuggerAddress.host}:${debuggerAddress.port}'
    };
    final capabilities = Capabilities.chrome;
    capabilities[Capabilities.chromeOptions] = chromeOptions;
    return capabilities;
  }

  Future<bool> _checkDebugger(HostAndPort debuggerAddress) async {
    // Check if the devtools port is responsive, to allow for this function to
    // be called on non-existent debugging ports. Without this check webdriver's
    // createDriver function may infinite loop trying to connect.
    try {
      final request = await _httpClient.getUrl(
          Uri.parse('http://${debuggerAddress.host}:${debuggerAddress.port}/'));
      await request.close();
    } on Exception {
      return false;
    }
    return true;
  }

  /// Creates a new sync WebDriver pointing to ChromeDriver on the given *HTTP*
  /// port of *localhost*.
  ///
  /// If [debuggerAddress] is not reachable, return null immediately.
  ///
  /// Note: [HttpClient] is only used to check [debuggerAddress] connectivity,
  /// but not used by the created [WebDriver].
  Future<sync_core.WebDriver> createDriver(
      HostAndPort debuggerAddress, int chromedriverPort) async {
    if (!await _checkDebugger(debuggerAddress)) {
      return null;
    }
    return sync_io.createDriver(
        desired: _capabilities(debuggerAddress),
        uri: Uri.parse('http://localhost:$chromedriverPort'));
  }

  /// Creates a new async WebDriver pointing to ChromeDriver on the given uri.
  ///
  /// If [debuggerAddress] is not reachable, return null immediately.
  ///
  /// The async version of [WebDriver] uses [HttpClient], so it supports HTTPS,
  /// proxy, custom headers, etc., which may be needed if ChromeDriver isn't
  /// running locally.
  Future<async_core.WebDriver> createAsyncDriver(
      HostAndPort debuggerAddress, Uri chromedriverUri) async {
    if (!await _checkDebugger(debuggerAddress)) {
      return null;
    }
    return async_core.createDriver(
        (prefix) => _CustomAsyncIoRequestClient(prefix, _httpClient),
        desired: _capabilities(debuggerAddress),
        uri: chromedriverUri);
  }
}

class _CustomAsyncIoRequestClient extends async_io.AsyncIoRequestClient {
  @override
  // ignore: overridden_fields
  final io.HttpClient client;

  _CustomAsyncIoRequestClient(Uri prefix, this.client) : super(prefix);
}
