// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:developer' show Timeline;

import 'package:fidl/fidl.dart';
import 'package:fidl_fuchsia_cobalt/fidl_async.dart' as cobalt;
import 'package:fidl_fuchsia_modular/fidl_async.dart';
import 'package:fidl_fuchsia_modular_auth/fidl_async.dart';
import 'package:fidl_fuchsia_netstack/fidl_async.dart';
import 'package:fidl_fuchsia_sys/fidl_async.dart';
import 'package:fidl_fuchsia_ui_gfx/fidl_async.dart';
import 'package:fidl_fuchsia_ui_input/fidl_async.dart' as input;
import 'package:fidl_fuchsia_ui_policy/fidl_async.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_services/services.dart' as app;
import 'package:meta/meta.dart';
import 'package:zircon/zircon.dart' show Channel;

import 'base_shell_model.dart';
import 'netstack_model.dart';
import 'user_manager.dart';

export 'package:lib.widgets/model.dart'
    show ScopedModel, ScopedModelDescendant, ModelFinder;

/// Function signature for GetPresentationMode callback
typedef GetPresentationModeCallback = void Function(PresentationMode mode);

const Duration _kCobaltTimerTimeout = Duration(seconds: 20);
const int _kSessionShellLoginTimeMetricId = 14;

// This class is extends the Presentation protocol and implements and PresentationModeListener.
// It delegates the methods to the Presentation received by the CommonBaseShellModel that owns it.
class CommonBaseShellPresentationImpl extends Presentation
    implements PresentationModeListener {
  final CommonBaseShellModel _model;

  CommonBaseShellPresentationImpl(this._model);

  /// |Presentation|.
  @override
  // ignore: avoid_positional_boolean_parameters
  Future<void> enableClipping(bool enabled) async {
    await _model.presentation.enableClipping(enabled);
  }

  @override
  Future<void> useOrthographicView() async {
    await _model.presentation.useOrthographicView();
  }

  @override
  Future<void> usePerspectiveView() async {
    await _model.presentation.usePerspectiveView();
  }

  @override
  Future<void> setRendererParams(List<RendererParam> params) async {
    await _model.presentation.setRendererParams(params);
  }

  @override
  Future<void> setDisplayUsage(DisplayUsage usage) async {
    await _model.presentation.setDisplayUsage(usage);
  }

  @override
  // ignore: avoid_positional_boolean_parameters
  Future<void> setDisplayRotation(
      double displayRotationDegrees, bool animate) async {
    await _model.presentation
        .setDisplayRotation(displayRotationDegrees, animate);
  }

  @override
  Future<void> setDisplaySizeInMm(num widthInMm, num heightInMm) async {
    await _model.presentation.setDisplaySizeInMm(widthInMm, heightInMm);
  }

  @override
  Future<void> captureKeyboardEventHack(input.KeyboardEvent eventToCapture,
      InterfaceHandle<KeyboardCaptureListenerHack> listener) async {
    await _model.presentation
        .captureKeyboardEventHack(eventToCapture, listener);
  }

  @override
  Future<void> capturePointerEventsHack(
      InterfaceHandle<PointerCaptureListenerHack> listener) async {
    await _model.presentation.capturePointerEventsHack(listener);
  }

  @override
  Future<PresentationMode> getPresentationMode() async {
    return await _model.presentation.getPresentationMode();
  }

  @override
  Future<void> setPresentationModeListener(
      InterfaceHandle<PresentationModeListener> listener) async {
    await _model.presentation.setPresentationModeListener(listener);
  }

  @override
  Future<void> registerMediaButtonsListener(
      InterfaceHandle<MediaButtonsListener> listener) async {
    await _model.presentation.registerMediaButtonsListener(listener);
  }

  /// |PresentationModeListener|.
  @override
  Future<void> onModeChanged() async {
    PresentationMode mode = await getPresentationMode();
    log.info('Presentation mode changed to: $mode');
    switch (mode) {
      case PresentationMode.tent:
        await setDisplayRotation(180.0, true);
        break;
      case PresentationMode.tablet:
        // TODO(sanjayc): Figure out up/down orientation.
        await setDisplayRotation(90.0, true);
        break;
      case PresentationMode.laptop:
      default:
        await setDisplayRotation(0.0, true);
        break;
    }
  }
}

/// Provides common features needed by all base shells.
///
/// This includes user management, presentation handling,
/// and keyboard shortcuts.
class CommonBaseShellModel extends BaseShellModel
    implements
        ServiceProvider,
        KeyboardCaptureListenerHack,
        PointerCaptureListenerHack {
  /// Handles login, logout, and adding/removing users.
  ///
  /// Shouldn't be used before onReady.
  BaseShellUserManager _userManager;

  NetstackModel _netstackModel;

  /// Logs metrics to Cobalt. May be null, in which case no metrics are logged.
  final cobalt.Logger logger;

  /// A list of accounts that are already logged in on the device.
  ///
  /// Only updated after [refreshUsers] is called.
  List<Account> _accounts;

  final List<KeyboardCaptureListenerHackBinding> _keyBindings = [];

  final PresentationModeListenerBinding _presentationModeListenerBinding =
      PresentationModeListenerBinding();
  final PointerCaptureListenerHackBinding _pointerCaptureListenerBinding =
      PointerCaptureListenerHackBinding();

  final List<PresentationBinding> _presentationBindings =
      <PresentationBinding>[];

  CommonBaseShellPresentationImpl _presentationImpl;

  /// Has the user logged in or not yet?
  bool _loggedIn = false;

  /// Constructor
  CommonBaseShellModel([this.logger]) : super() {
    _presentationImpl = CommonBaseShellPresentationImpl(this);
  }

  List<Account> get accounts => _accounts;

  // |ServiceProvider|.
  @override
  Future<void> connectToService(String serviceName, Channel channel) {
    if (serviceName == 'ui.Presentation') {
      _presentationBindings.add(PresentationBinding()
        ..bind(_presentationImpl, InterfaceRequest<Presentation>(channel)));
    } else {
      log.warning(
          'UserPickerBaseShell: received request for unknown service: $serviceName !');
      channel.close();
    }

    return null;
  }

  /// Create a new user and login with that user
  Future createAndLoginUser() async {
    try {
      final userId = await _userManager.addUser();
      await login(userId);
    } on UserLoginException catch (ex) {
      log.severe(ex);
    } finally {
      notifyListeners();
    }
  }

  /// Whether or not the device has an internet connection.
  ///
  /// Currently, having an IP is equivalent to having internet, although
  /// this is not completely reliable. This will be always false until
  /// onReady is called.
  bool get hasInternetConnection =>
      _netstackModel?.networkReachable?.value ?? false;

  Future<void> waitForInternetConnection() async {
    if (hasInternetConnection) {
      return null;
    }

    final completer = Completer<void>();

    void listener() {
      if (hasInternetConnection) {
        _netstackModel.removeListener(listener);
        completer.complete();
      }
    }

    _netstackModel.addListener(listener);

    return completer.future;
  }

  /// Login with given user
  Future<void> login(String accountId) async {
    if (_loggedIn) {
      log.warning(
        'Ignoring unsupported attempt to log in while already logged in!',
      );
      return;
    }

    Timeline.instantSync('logging in', arguments: {'accountId': '$accountId'});
    
    if (logger != null) {
      await logger
      .startTimer(
        _kSessionShellLoginTimeMetricId,
        0,
        '',
        'session_shell_login_timer_id',
        DateTime.now().millisecondsSinceEpoch,
        _kCobaltTimerTimeout.inSeconds)
      .then((status) {
          if (status != cobalt.Status.ok) {
            log.warning(
              'Failed to start timer metric '
              '$_kSessionShellLoginTimeMetricId: $status. ',
            );
          }
      });
    }

    _userManager.login(accountId);
    _loggedIn = true;

    notifyListeners();
  }

  /// Called when the the session shell logs out.
  @mustCallSuper
  Future<void> onLogout() async {
    _loggedIn = false;

    for (PresentationBinding presentationBinding in _presentationBindings) {
      presentationBinding.close();
    }
    await refreshUsers();

    notifyListeners();
  }

  /// |KeyboardCaptureListener|.
  @override
  Future<void> onEvent(input.KeyboardEvent ev) async {}

  /// |PointerCaptureListener|.
  @override
  Future<void> onPointerEvent(input.PointerEvent event) async {}

  // |BaseShellModel|.
  // TODO: revert to default state when client logs out.
  @mustCallSuper
  @override
  Future<void> onReady(
    UserProvider userProvider,
    BaseShellContext baseShellContext,
    Presentation presentation,
  ) async {
    super.onReady(userProvider, baseShellContext, presentation);

    final netstackProxy = NetstackProxy();
    app.StartupContext.fromStartupInfo()
        .incoming
        .connectToService(netstackProxy);
    _netstackModel = NetstackModel(netstack: netstackProxy)..start();

    await presentation
        .capturePointerEventsHack(_pointerCaptureListenerBinding.wrap(this));
    await presentation.setPresentationModeListener(
        _presentationModeListenerBinding.wrap(_presentationImpl));

    _userManager = BaseShellUserManager(userProvider);

    _userManager.onLogout.listen((_) async {
        if (logger != null) {
          await logger
          .endTimer(
            'session_shell_log_out_timer_id',
            DateTime.now().millisecondsSinceEpoch,
            _kCobaltTimerTimeout.inSeconds)
          .then((status) {
              if (status != cobalt.Status.ok) {
                log.warning(
                  'Failed to end timer metric '
                  'session_shell_log_out_timer_id: $status. ',
                );
              }
          });
        }
        log.info('UserPickerBaseShell: User logged out!');
        await onLogout();
    });
    
    await refreshUsers();
  }

  // |BaseShellModel|
  // TODO: revert to default state when client logs out.
  @override
  void onStop() {
    for (final binding in _keyBindings) {
      binding.close();
    }
    _presentationModeListenerBinding.close();
    _netstackModel.dispose();
    super.onStop();
  }

  // TODO: revert to default state when client logs out.
  /// Refreshes the list of users.
  Future<void> refreshUsers() async {
    _accounts = List<Account>.from(await _userManager.getPreviousUsers());
    notifyListeners();
  }

  // TODO: revert to default state when client logs out.
  /// Permanently removes the user.
  Future removeUser(Account account) async {
    try {
      await _userManager.removeUser(account.id);
    } on UserLoginException catch (ex) {
      log.severe(ex);
    } finally {
      await refreshUsers();
    }
  }

  @override
  // TODO: implement $serviceData
  ServiceData get $serviceData => null;
}
