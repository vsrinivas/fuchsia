// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';

import 'package:fidl/fidl.dart';
import 'package:fidl_fuchsia_io/fidl_async.dart' as fidl_io;
import 'package:fidl_fuchsia_web/fidl_async.dart' as fidl_web;
import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_scenic/views.dart';
import 'package:fuchsia_scenic_flutter/fuchsia_view.dart';
import 'package:fuchsia_services/services.dart';
import 'package:fuchsia_vfs/vfs.dart';
import 'package:zircon/zircon.dart';

import 'utils.dart' as utils;

/// The default remote debugging port is 0 which will pick a random port each
/// time.
///
/// This is important to avoid binding to a port opened by another simultaneous
/// Context. To find the port you can look in the logs for `DevTools`, use the
/// GetRemoteDebuggingPort FIDL method, or access the SL4F webdriver facade.
const _defaultRemoteDebuggingPort = 0;

/// This helper class connects and interfaces with 'fuchsia.web.*' services.
class FuchsiaWebServices {
  // TODO(fxbug.dev/54313) We are currently enabling all of the "base" features on
  // all builds but they should be conditionally enabled based on the values
  // specified in the parameters.
  /// This is the base set of [fidl_fuchsia_web.ContextFeatureFlags] that is enabled in all circumstances.
  static final baseWebFeatures = fidl_web.ContextFeatureFlags.network |
      fidl_web.ContextFeatureFlags.audio |
      fidl_web.ContextFeatureFlags.hardwareVideoDecoder;

  /// This helper computes the appropriate `fidl_fuchsia_web.ContextFeatureFlags` from passed-in settings.
  static fidl_web.ContextFeatureFlags webFeaturesFromSettings(
      {bool useSoftwareRendering = false}) {
    return FuchsiaWebServices.baseWebFeatures |
        (useSoftwareRendering
            ? fidl_web.ContextFeatureFlags.$none
            : fidl_web.ContextFeatureFlags.vulkan);
  }

  final fidl_web.ContextProviderProxy _contextProviderProxy =
      fidl_web.ContextProviderProxy();
  final fidl_web.ContextProxy _contextProxy = fidl_web.ContextProxy();
  final fidl_web.FrameProxy _frameProxy = fidl_web.FrameProxy();
  final fidl_web.NavigationControllerProxy _navigationControllerProxy =
      fidl_web.NavigationControllerProxy();

  final fidl_web.NavigationEventListenerBinding
      _navigationEventObserverBinding =
      fidl_web.NavigationEventListenerBinding();

  FuchsiaViewConnection? _viewConnection;

  /// Constructs [FuchsiaWebServices] and connects to various 'fuchsia.web.*`
  /// services.
  FuchsiaWebServices({bool useSoftwareRendering = false}) {
    Incoming.fromSvcPath()
      ..connectToService(_contextProviderProxy)
      ..close();

    // TODO(nkorsote): [service_directory] is effectively the sandbox inside
    // which the created Context will run. If you give it a direct handle to
    // component's /svc directory then it'll have access to everything the
    // component can access. Alternatively, refactor this to use an Outgoing
    // Directory.
    if (!Directory('/svc').existsSync()) {
      log.shout('no /svc directory');
      return;
    }
    final channel = Channel.fromFile('/svc');
    final directory = fidl_io.DirectoryProxy()
      ..ctrl.bind(InterfaceHandle<fidl_io.Directory>(channel));
    final composedDir =
        ComposedPseudoDir(directory: directory, inheritedNodes: [
      'fuchsia.accessibility.semantics.SemanticsManager',
      'fuchsia.device.NameProvider',
      'fuchsia.fonts.Provider',
      'fuchsia.input.virtualkeyboard.ControllerCreator',
      'fuchsia.intl.PropertyProvider',
      'fuchsia.logger.LogSink',
      'fuchsia.media.Audio',
      'fuchsia.media.SessionAudioConsumerFactory',
      'fuchsia.mediacodec.CodecFactory',
      'fuchsia.memorypressure.Provider',
      'fuchsia.net.name.Lookup',
      'fuchsia.net.interfaces.State',
      'fuchsia.posix.socket.Provider',
      'fuchsia.process.Launcher',
      'fuchsia.sysmem.Allocator',
      'fuchsia.tracing.provider.Registry',
      'fuchsia.ui.input3.Keyboard',
      'fuchsia.ui.scenic.Scenic',
      'fuchsia.vulkan.loader.Loader',
    ]);
    final pair = ChannelPair();
    composedDir.serve(InterfaceRequest<fidl_io.Node>(pair.first));

    final contextParams = fidl_web.CreateContextParams(
        features: FuchsiaWebServices.webFeaturesFromSettings(
            useSoftwareRendering: useSoftwareRendering),
        serviceDirectory: InterfaceHandle<fidl_io.Directory>(pair.second),
        remoteDebuggingPort: _defaultRemoteDebuggingPort);

    _contextProviderProxy.create(contextParams, _contextProxy.ctrl.request());
    _contextProxy.createFrameWithParams(
        fidl_web.CreateFrameParams(enableRemoteDebugging: false),
        frame.ctrl.request());

    // Create token pair and pass one end to the webview and the other to child
    // view connection which will be used to construct the child view widget
    // that the webview will live in.
    final tokenPair = ViewTokenPair();
    frame.createView(tokenPair.viewToken);
    _viewConnection = FuchsiaViewConnection(tokenPair.viewHolderToken);
    frame.getNavigationController(_navigationControllerProxy.ctrl.request());
  }

  /// Sets the javascript log level for the frame.
  Future<void> setJavaScriptLogLevel(fidl_web.ConsoleLogLevel level) {
    return frame.setJavaScriptLogLevel(level);
  }

  /// Returns a connection to a child view.
  ///
  /// It can be used to construct a [FuchsiaView] widget that will display the
  /// view's contents.
  FuchsiaViewConnection? get viewConnection => _viewConnection;

  /// Returns [fidl_fuchsia_web.NavigationControllerProxy]
  fidl_web.NavigationControllerProxy get navigationController =>
      _navigationControllerProxy;

  /// Returns [fidl_fuchsia_web.FrameProxy]
  fidl_web.FrameProxy get frame => _frameProxy;

  /// Preforms the all the necessary cleanup.
  void dispose() {
    _navigationControllerProxy.ctrl.close();
    _frameProxy.ctrl.close();
    _contextProxy.ctrl.close();
    _contextProviderProxy.ctrl.close();
    _navigationEventObserverBinding.close();
  }

  /// Executes a UTF-8 encoded [script] in the frame if the frame's URL has
  /// an origin which matches entries in [origins].
  ///
  /// At least one [origins] entry must be specified.
  /// If a wildcard "*" is specified in [origins], then the script will be
  /// evaluated unconditionally.
  ///
  /// Note that scripts share the same execution context as the document,
  /// meaning that document may modify variables, classes, or objects set by
  /// the script in arbitrary or unpredictable ways.
  ///
  /// If an error occured, the FrameError will be set to one of these values:
  /// BUFFER_NOT_UTF8: [script] is not UTF-8 encoded.
  /// INVALID_ORIGIN: The Frame's current URL does not match any of the
  ///                 values in [origins] or [origins] is an empty vector.
  // TODO(crbug.com/900391): Investigate if we can run the scripts in
  // isolated JS worlds.
  Future<void> runJavascript(List<String> origins, String script) async {
    await runJavascriptReturningResult(origins, script);
  }

  /// Executes a UTF-8 encoded [script] in the frame if the frame's URL has
  /// an origin which matches entries in [origins].
  ///
  /// At least one [origins] entry must be specified.
  /// If a wildcard "*" is specified in [origins], then the script will be
  /// evaluated unconditionally.
  ///
  /// Note that scripts share the same execution context as the document,
  /// meaning that document may modify variables, classes, or objects set by
  /// the script in arbitrary or unpredictable ways.
  ///
  /// If an error occured, the FrameError will be set to one of these values:
  /// BUFFER_NOT_UTF8: [script] is not UTF-8 encoded.
  /// INVALID_ORIGIN: The Frame's current URL does not match any of the
  ///                 values in [origins] or [origins] is an empty vector.
  // TODO(crbug.com/900391): Investigate if we can run the scripts in
  // isolated JS worlds.
  Future<String> runJavascriptReturningResult(
      List<String> origins, String script) async {
    final buffer = utils.stringToBuffer(script);
    // TODO(nkosote): add catchError and decorate the error based on the error
    // code.
    final result = await frame.executeJavaScript(origins, buffer);
    return utils.bufferToString(result);
  }

  /// Executes a UTF-8 encoded `script` for every subsequent page load where the
  /// [`fidl_fuchsia_web.Frame`]'s URL has an origin reflected in `origins`. The script is executed
  /// early, prior to the execution of the document's scripts.
  ///
  /// Scripts are identified by a client-managed identifier `id`. Any script previously injected
  /// using the same `id` will be replaced.
  ///
  /// The order in which multiple bindings are executed is the same as the order in which the
  /// bindings were added. If a script is added which clobbers an existing script of the same
  /// `id`, the previous script's precedence in the injection order will be preserved.
  ///
  /// At least one `origins` entry must be specified. If a wildcard `"*"` is specified in
  /// `origins`, then the script will be evaluated unconditionally.
  ///
  /// If an error occured, the [`fidl_fuchsia_web.FrameError`] will be set to one of these values:
  /// - `BUFFER_NOT_UTF8`: `script` is not UTF-8 encoded.
  /// - `INVALID_ORIGIN`: `origins` is an empty vector.
  Future<void> runJavascriptBeforeLoad(
      int id, List<String> origins, String script) async {
    final buffer = utils.stringToBuffer(script);
    // TODO(miguelfrde): add catchError and decorate the error based on the error
    // code.
    await frame.addBeforeLoadJavaScript(id, origins, buffer);
  }

  /// Posts a message to the [fidl_fuchsia_web.Frame]'s onMessage handler.
  ///
  /// [targetOrigin] restricts message delivery to the specified origin. If
  /// [targetOrigin] is "*", then the message will be sent to the document
  /// regardless of its origin.
  ///
  /// If an error occurred, the FrameError will be set to one of these values:
  /// - INTERNAL_ERROR: The WebEngine failed to create a message pipe.
  /// - BUFFER_NOT_UTF8: The script in [message]'s [fidl_fuchsia_web.WebMessage#data]
  ///   property is not UTF-8 encoded.
  /// - INVALID_ORIGIN: origins is an empty vector.
  /// - NO_DATA_IN_MESSAGE: The [fidl_fuchsia_web.WebMessage#data] property is missing
  ///   in [message].
  Future<void> postMessage(
    String targetOrigin,
    String message, {
    InterfaceRequest<fidl_web.MessagePort>? outgoingMessagePortRequest,
  }) {
    final data = utils.stringToBuffer(message);
    var msg = fidl_web.WebMessage(
      data: data,
      outgoingTransfer: outgoingMessagePortRequest != null
          ? [
              fidl_web.OutgoingTransferable.withMessagePort(
                  outgoingMessagePortRequest)
            ]
          : null,
    );
    // TODO(nkosote): add catchError and decorate the error based on the error
    // code
    return frame.postMessage(targetOrigin, msg);
  }

  /// Sets the listener for handling page navigation events.
  ///
  /// The [observer] to use. Unregisters any existing listener if null.
  void setNavigationEventListener(fidl_web.NavigationEventListener observer) {
    frame.setNavigationEventListener(
        _navigationEventObserverBinding.wrap(observer));
  }
}
