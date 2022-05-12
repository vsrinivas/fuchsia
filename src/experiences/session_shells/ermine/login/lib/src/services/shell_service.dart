// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

//ignore: unused_import
import 'package:ermine_utils/ermine_utils.dart';
import 'package:fidl_fuchsia_component/fidl_async.dart';
import 'package:fidl_fuchsia_component_decl/fidl_async.dart';
import 'package:fidl_fuchsia_io/fidl_async.dart';
import 'package:fidl_fuchsia_ui_app/fidl_async.dart';
import 'package:fidl_fuchsia_ui_scenic/fidl_async.dart';
import 'package:fidl_fuchsia_ui_views/fidl_async.dart' hide FocusState;
import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_scenic_flutter/fuchsia_view.dart';
import 'package:fuchsia_services/services.dart';
import 'package:flutter/material.dart';
import 'package:mobx/mobx.dart';
import 'package:zircon/zircon.dart';

/// Defines a service to launch and support Ermine user shell.
class ShellService {
  late final StreamSubscription<bool> _focusSubscription;
  late final VoidCallback onShellReady;
  late final VoidCallback onShellExit;
  late final bool _useFlatland;
  _ErmineViewConnection? _ermine;
  var _loadedCompleter = Completer();

  ShellService() {
    ScenicProxy scenic = ScenicProxy();
    Incoming.fromSvcPath().connectToService(scenic);
    scenic.usesFlatland().then((scenicUsesFlatland) {
      _useFlatland = scenicUsesFlatland;
      runInAction(() => {_ready.value = true});
    });
    WidgetsFlutterBinding.ensureInitialized();
    _focusSubscription = FocusState.instance.stream().listen(_onFocusChanged);
  }

  /// Returns [true] after call to [Scenic.usesFlatland] completes.
  bool get ready => _ready.value;
  final _ready = false.asObservable();

  /// Returns the future when shell is loaded and finished rendering a frame.
  Future get loaded => _loadedCompleter.future;

  void dispose() {
    _focusSubscription.cancel();
  }

  /// Launch Ermine shell and return [FuchsiaViewConnection].
  FuchsiaViewConnection launchErmineShell() {
    assert(_ermine == null, 'Instance of ermine shell already exists.');
    _ermine = _ErmineViewConnection(
      useFlatland: _useFlatland,
      onReady: () {
        _loadedCompleter.complete();
        _loadedCompleter = Completer();
        onShellReady();
      },
      onExit: onShellExit,
    );
    return _ermine!.fuchsiaViewConnection;
  }

  void disposeErmineShell() {
    _ermine = null;
  }

  // Transfer focus to Ermine shell whenever login shell receives focus.
  void _onFocusChanged(bool focused) {
    if (focused) {
      _ermine?.setFocus();
    }
  }
}

class _ErmineViewConnection {
  final bool useFlatland;
  final VoidCallback onReady;
  final VoidCallback onExit;
  late final FuchsiaViewConnection fuchsiaViewConnection;
  bool _focusRequested = false;

  _ErmineViewConnection({
    required this.useFlatland,
    required this.onReady,
    required this.onExit,
  }) {
    // Connect to the Realm.
    final realm = RealmProxy();
    Incoming.fromSvcPath().connectToService(realm);

    // Get the ermine shell's exposed /svc directory.
    final exposedDir = DirectoryProxy();
    realm.openExposedDir(
        ChildRef(name: 'ermine_shell'), exposedDir.ctrl.request());

    // Get the ermine shell's view provider.
    final viewProvider = ViewProviderProxy();
    Incoming.withDirectory(exposedDir).connectToService(viewProvider);
    viewProvider.ctrl.whenClosed.then((_) => onExit());

    fuchsiaViewConnection = _launch(viewProvider);
  }

  void setFocus() {
    if (_focusRequested) {
      fuchsiaViewConnection.requestFocus().catchError((e) {
        log.shout(e);
      });
    }
  }

  FuchsiaViewConnection _launch(ViewProvider viewProvider) {
    if (useFlatland) {
      final viewTokens = ChannelPair();
      assert(viewTokens.status == ZX.OK);
      final viewportCreationToken =
          ViewportCreationToken(value: viewTokens.first!);
      final viewCreationToken = ViewCreationToken(value: viewTokens.second!);

      final createViewArgs =
          CreateView2Args(viewCreationToken: viewCreationToken);
      viewProvider.createView2(createViewArgs);

      return FuchsiaViewConnection.flatland(
        viewportCreationToken,
        onViewStateChanged: _onViewStateChanged,
      );
    } else {
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

      return FuchsiaViewConnection(
        viewHolderToken,
        viewRef: viewRefInject,
        onViewStateChanged: _onViewStateChanged,
      );
    }
  }

  void _onViewStateChanged(FuchsiaViewController _, bool? state) {
    if (state == true && !_focusRequested) {
      _focusRequested = true;
      setFocus();
      onReady();
    }
  }
}
