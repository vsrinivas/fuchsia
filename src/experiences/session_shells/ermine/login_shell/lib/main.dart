// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_fuchsia_netstack/fidl_async.dart';
import 'package:flutter/material.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_services/services.dart';
import 'package:lib.widgets/application.dart';
import 'package:lib.widgets/model.dart';
import 'package:meta/meta.dart';
import 'package:fidl_fuchsia_sys/fidl_async.dart';

import 'authentication_overlay.dart';
import 'authentication_overlay_model.dart';
import 'authentication_ui_context_impl.dart';
import 'base_shell_widget.dart';
import 'netstack_model.dart';
import 'user_picker_base_shell_model.dart';
import 'user_picker_base_shell_screen.dart';

const double _kMousePointerElevation = 800.0;
const double _kIndicatorElevation = _kMousePointerElevation - 1.0;

/// The main base shell widget.
BaseShellWidget<UserPickerBaseShellModel> _baseShellWidget;

void main() {
  setupLogger(name: 'userpicker_base_shell');
  StartupContext startupContext = StartupContext.fromStartupInfo();
  final launcherProxy = LauncherProxy();
  startupContext.incoming.connectToService(launcherProxy);

  NetstackProxy netstackProxy = NetstackProxy();
  startupContext.incoming.connectToService(netstackProxy);

  NetstackModel netstackModel = NetstackModel(netstack: netstackProxy)..start();

  _OverlayModel wifiInfoOverlayModel = _OverlayModel();

  final AuthenticationOverlayModel authModel = AuthenticationOverlayModel();

  UserPickerBaseShellModel userPickerBaseShellModel = UserPickerBaseShellModel(
    onBaseShellStopped: () {
      netstackProxy.ctrl.close();
      netstackModel.dispose();
    },
    onLogin: () {
      wifiInfoOverlayModel.showing = false;
    },
    onWifiTapped: () {
      wifiInfoOverlayModel.showing = !wifiInfoOverlayModel.showing;
    },
  );

  Widget mainWidget = Stack(
    fit: StackFit.passthrough,
    children: <Widget>[
      UserPickerBaseShellScreen(
        launcher: launcherProxy,
      ),
      ScopedModel<AuthenticationOverlayModel>(
        model: authModel,
        child: AuthenticationOverlay(),
      ),
    ],
  );

  Widget app = mainWidget;

  List<OverlayEntry> overlays = <OverlayEntry>[
    OverlayEntry(
      builder: (BuildContext context) => MediaQuery(
        data: MediaQueryData(),
        child: FocusScope(
          node: FocusScopeNode(),
          autofocus: true,
          child: app,
        ),
      ),
    ),
    OverlayEntry(
      builder: (BuildContext context) => ScopedModel<_OverlayModel>(
        model: wifiInfoOverlayModel,
        child: _WifiInfo(
          wifiWidget: ApplicationWidget(
            url:
                'fuchsia-pkg://fuchsia.com/wifi_settings#meta/wifi_settings.cmx',
            launcher: launcherProxy,
          ),
        ),
      ),
    ),
  ];

  _baseShellWidget = BaseShellWidget<UserPickerBaseShellModel>(
    startupContext: startupContext,
    baseShellModel: userPickerBaseShellModel,
    authenticationUiContext: AuthenticationUiContextImpl(
        onStartOverlay: authModel.onStartOverlay,
        onStopOverlay: authModel.onStopOverlay),
    child: LayoutBuilder(
      builder: (BuildContext context, BoxConstraints constraints) =>
          (constraints.biggest == Size.zero)
              ? const Offstage()
              : ScopedModel<NetstackModel>(
                  model: netstackModel,
                  child: Overlay(key: Key('main'), initialEntries: overlays),
                ),
    ),
  );

  runApp(_baseShellWidget);

  _baseShellWidget.advertise();
}

class _WifiInfo extends StatelessWidget {
  final Widget wifiWidget;

  const _WifiInfo({@required this.wifiWidget}) : assert(wifiWidget != null);

  @override
  Widget build(BuildContext context) => ScopedModelDescendant<_OverlayModel>(
        builder: (
          BuildContext context,
          Widget child,
          _OverlayModel model,
        ) =>
            Offstage(
          offstage: !model.showing,
          child: Stack(
            children: <Widget>[
              Listener(
                behavior: HitTestBehavior.opaque,
                onPointerDown: (PointerDownEvent event) {
                  model.showing = false;
                },
              ),
              Center(
                child: FractionallySizedBox(
                  widthFactor: 0.75,
                  heightFactor: 0.75,
                  child: Container(
                    margin: EdgeInsets.all(8.0),
                    child: PhysicalModel(
                      color: Colors.grey[900],
                      elevation: _kIndicatorElevation,
                      borderRadius: BorderRadius.circular(8.0),
                      child: wifiWidget,
                    ),
                  ),
                ),
              ),
            ],
          ),
        ),
      );
}

class _OverlayModel extends Model {
  bool _showing = false;

  set showing(bool showing) {
    if (_showing != showing) {
      _showing = showing;
      notifyListeners();
    }
  }

  bool get showing => _showing;
}
