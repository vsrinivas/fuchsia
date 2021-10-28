// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:fidl_fuchsia_element/fidl_async.dart';
import 'package:fidl_fuchsia_sys/fidl_async.dart';
import 'package:fidl_fuchsia_ui_app/fidl_async.dart';
import 'package:fidl_fuchsia_ui_views/fidl_async.dart' hide FocusState;
import 'package:fuchsia_scenic_flutter/fuchsia_view.dart';
import 'package:fuchsia_services/services.dart';
import 'package:zircon/zircon.dart';

/// Defines a service to launch and support Ermine user shell.
class ShellService {
  final _graphicalPresenter = GraphicalPresenterProxy();
  late final FuchsiaViewConnection _fuchsiaViewConnection;
  bool _focusRequested = false;
  late final StreamSubscription<bool> _focusSubscription;

  void advertise(Outgoing outgoing) {
    outgoing.addPublicService((request) {
      GraphicalPresenterBinding().bind(_graphicalPresenter, request);
    }, GraphicalPresenter.$serviceName);
  }

  void dispose() {
    _focusSubscription.cancel();
    _graphicalPresenter.ctrl.close();
  }

  /// Launch Ermine shell and return [FuchsiaViewConnection].
  FuchsiaViewConnection launchErmineShell() {
    _focusSubscription = FocusState.instance.stream().listen(_onFocusChanged);

    final incoming = Incoming();
    final componentController = ComponentControllerProxy();
    final elementManager = ManagerProxy();

    final launcher = LauncherProxy();
    Incoming.fromSvcPath()
      ..connectToService(elementManager)
      ..connectToService(launcher)
      ..close();

    final binding = ServiceProviderBinding();
    final provider = ServiceProviderImpl()
      ..addServiceForName(
        (request) => ManagerBinding().bind(elementManager, request),
        Manager.$serviceName,
      );

    launcher.createComponent(
      LaunchInfo(
        url: 'fuchsia-pkg://fuchsia.com/ermine#meta/ermine.cmx',
        directoryRequest: incoming.request().passChannel(),
        additionalServices: ServiceList(
          names: [Manager.$serviceName],
          provider: binding.wrap(provider),
        ),
      ),
      componentController.ctrl.request(),
    );
    launcher.ctrl.close();

    ViewProviderProxy viewProvider = ViewProviderProxy();
    incoming
      ..connectToService(viewProvider)
      ..connectToService(_graphicalPresenter);

    // TODO(fxbug.dev/86450): Flip this to use Flatland instead of legacy Scenic
    // Gfx API.
    const useFlatland = false;

    // ignore: dead_code
    if (useFlatland) {
      final viewTokens = ChannelPair();
      assert(viewTokens.status == ZX.OK);
      final viewportCreationToken =
          ViewportCreationToken(value: viewTokens.first!);
      final viewCreationToken = ViewCreationToken(value: viewTokens.second!);

      final createViewArgs =
          CreateView2Args(viewCreationToken: viewCreationToken);
      viewProvider.createView2(createViewArgs);
      viewProvider.ctrl.close();

      // TODO(fxbug.dev/86649): We should let the child send us the one they
      // minted for Flatland. Once that is available, we can call requestFocus()
      // on onViewStateChanged.
      return _fuchsiaViewConnection = FuchsiaViewConnection.flatland(
        viewportCreationToken,
        onViewStateChanged: (_, state) {
          // Wait until ermine shell has rendered before focusing it.
          if (state == true && !_focusRequested) {
            _focusRequested = true;
          }
        },
      );
    }

    final viewTokens = EventPairPair();
    assert(viewTokens.status == ZX.OK);
    final viewHolderToken = ViewHolderToken(value: viewTokens.first!);
    final viewToken = ViewToken(value: viewTokens.second!);

    final viewRefPair = EventPairPair();
    final viewRef =
        ViewRef(reference: viewRefPair.first!.duplicate(ZX.RIGHTS_BASIC));
    final viewRefControl = ViewRefControl(
        reference: viewRefPair.second!
            .duplicate(ZX.DEFAULT_EVENTPAIR_RIGHTS & (~ZX.RIGHT_DUPLICATE)));
    final viewRefInject =
        ViewRef(reference: viewRefPair.first!.duplicate(ZX.RIGHTS_BASIC));

    viewProvider.createViewWithViewRef(
        viewToken.value, viewRefControl, viewRef);
    viewProvider.ctrl.close();

    return _fuchsiaViewConnection = FuchsiaViewConnection(
      viewHolderToken,
      viewRef: viewRefInject,
      onViewStateChanged: (_, state) {
        // Wait until ermine shell has rendered before focusing it.
        if (state == true && !_focusRequested) {
          _focusRequested = true;
          _fuchsiaViewConnection.requestFocus();
        }
      },
    );
  }

  void _onFocusChanged(bool focused) {
    if (_focusRequested && focused) {
      // ignore: unawaited_futures
      _fuchsiaViewConnection.requestFocus();
    }
  }
}
