// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io' as io;

import 'package:sl4f/sl4f.dart';
import 'package:webdriver/sync_core.dart' show WebDriver;
import 'package:webdriver/sync_io.dart' as sync_io;

/// Port Chromedriver listens on.
const _chromedriverPort = 9072;

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
/// Note that the latest version of Chromedriver currently needs to be manually
/// downloaded and placed in the location passed when constructing
/// `WebDriverConnector`.  This is necessary as the automatically downloaded
/// version in prebuilt is not kept up to date with the Chrome version. Effort
/// for this is tracked in IN-1321.
/// TODO(satsukiu): Remove notice and add e2e test for facade functionality
/// on completion of IN-1321
class WebDriverConnector {
  /// Relative path of chromedriver binary.
  final String _chromedriverPath;

  /// SL4F client.
  final Sl4f _sl4f;

  /// Helper for starting processes.
  final ProcessHelper _processHelper;

  /// Helper for instantiating WebDriver objects.
  final WebDriverHelper _webDriverHelper;

  /// A handle to the process running Chromedriver.
  io.Process _chromedriverProcess;

  /// A mapping from an exposed port number to an open WebDriver session.
  Map<int, _WebDriverSession> _webDriverSessions;

  WebDriverConnector(String chromeDriverPath, Sl4f sl4f,
      {ProcessHelper processHelper, WebDriverHelper webDriverHelper})
      : _chromedriverPath = chromeDriverPath,
        _sl4f = sl4f,
        _processHelper = processHelper ?? ProcessHelper(),
        _webDriverHelper = webDriverHelper ?? WebDriverHelper(),
        _webDriverSessions = {};

  /// Starts ChromeDriver and enables DevTools for any future created Chrome
  /// contexts.  As this will not enable DevTools on any already opened
  /// contexts, `initialize` must be called prior to the instantiation of the
  /// Chrome context that needs to be driven.
  Future<void> initialize() async {
    await _sl4f.request('webdriver_facade.EnableDevTools');
    await _startChromedriver();
  }

  /// Stops Chromedriver and removes any connections that are still open.
  void tearDown() {
    _chromedriverProcess?.kill();
    _chromedriverProcess = null;
    for (var session in _webDriverSessions.values) {
      session.portForwardingProcess.kill();
    }
    _webDriverSessions = {};
  }

  /// Get all nonEmpty Urls obtained from current _webDriverSessions.
  Iterable<String> get sessionsUrls => _webDriverSessions.values
      .map((session) => session.webDriver.currentUrl)
      .where((url) => url.isNotEmpty);

  /// Searches for Chrome contexts based on the host of the currently displayed
  /// page, and returns `WebDriver` connections to the found contexts.
  Future<List<WebDriver>> webDriversForHost(String host) async {
    await _updateWebDriverSessions();

    return List.from(_webDriverSessions.values
        .where(
            (session) => Uri.parse(session.webDriver.currentUrl).host == host)
        .map((session) => session.webDriver));
  }

  /// Starts Chromedriver on the host.
  Future<void> _startChromedriver() async {
    if (_chromedriverProcess == null) {
      final chromedriver =
          io.Platform.script.resolve(_chromedriverPath).toFilePath();
      final args = ['--port=$_chromedriverPort'];
      _chromedriverProcess = await _processHelper.start(chromedriver, args);
    }
  }

  /// Updates the set of open WebDriver connections.
  Future<void> _updateWebDriverSessions() async {
    var portsResult = await _sl4f.request('webdriver_facade.GetDevToolsPorts');
    var ports = Set.from(portsResult['ports']);

    // terminate and remove port forwarding for any ports that aren't open anymore
    _webDriverSessions.removeWhere((port, session) {
      if (!ports.contains(port)) {
        session.portForwardingProcess.kill();
        return true;
      } else {
        return false;
      }
    });

    // Add new sessions for new ports.  For a given Chrome context listening on
    // port p on the DuT, we forward localhost:p to DuT:p, and create a
    // WebDriver instance pointing to localhost:p.
    for (var port in ports) {
      if (!_webDriverSessions.containsKey(port)) {
        var forwardProcess = await _processHelper.start(
            'ssh',
            [
              '-i',
              _sl4f.ssh.sshKeyPath,
              '-N',
              '-o',
              'UserKnownHostsFile=/dev/null',
              '-o',
              'StrictHostKeyChecking=no',
              '-L',
              '$port:localhost:$port',
              _sl4f.target,
            ],
            runInShell: true);

        var webDriver = _webDriverHelper.createDriver(port, _chromedriverPort);

        _webDriverSessions.putIfAbsent(
            port, () => _WebDriverSession(webDriver, forwardProcess));
      }
    }
  }
}

/// A wrapper around static dart:io Process methods.
class ProcessHelper {
  ProcessHelper();

  /// Start a new process.
  Future<io.Process> start(String cmd, List<String> args,
      {bool runInShell = false}) async {
    return await io.Process.start(cmd, args, runInShell: runInShell);
  }
}

/// A wrapper around static WebDriver creation methods.
class WebDriverHelper {
  WebDriverHelper();

  /// Create a new WebDriver pointing to Chromedriver on the given uri and with
  /// given desired capabilities.
  WebDriver createDriver(int devToolsPort, int chromedriverPort) {
    var chromeOptions = {'debuggerAddress': 'localhost:$devToolsPort'};
    var capabilities = sync_io.Capabilities.chrome;
    capabilities[sync_io.Capabilities.chromeOptions] = chromeOptions;
    return sync_io.createDriver(
        desired: capabilities,
        uri: Uri.parse('http://localhost:$_chromedriverPort'));
  }
}

/// A wrapper around WebDriver object that also holds the port forwarding process.
class _WebDriverSession {
  /// WebDriver connection.
  final WebDriver webDriver;

  /// Handle to the process forwarding the port from the host to DuT.
  final io.Process portForwardingProcess;

  _WebDriverSession(this.webDriver, this.portForwardingProcess);
}
