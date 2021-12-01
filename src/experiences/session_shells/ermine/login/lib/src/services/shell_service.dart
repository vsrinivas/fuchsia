// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:ermine_utils/ermine_utils.dart';
import 'package:fidl_fuchsia_component/fidl_async.dart';
import 'package:fidl_fuchsia_component_decl/fidl_async.dart';
import 'package:fidl_fuchsia_io/fidl_async.dart';
import 'package:fidl_fuchsia_ui_app/fidl_async.dart';
import 'package:fidl_fuchsia_ui_scenic/fidl_async.dart';
import 'package:fidl_fuchsia_ui_views/fidl_async.dart' hide FocusState;
import 'package:fuchsia_scenic_flutter/fuchsia_view.dart';
import 'package:fuchsia_services/services.dart';
import 'package:zircon/zircon.dart';

/// Defines a service to launch and support Ermine user shell.
class ShellService {
  late final FuchsiaViewConnection _fuchsiaViewConnection;
  bool _focusRequested = false;
  late final StreamSubscription<bool> _focusSubscription;
  bool _useFlatland = false;

  ShellService() {
    ScenicProxy scenic = ScenicProxy();
    Incoming.fromSvcPath().connectToService(scenic);
    scenic.usesFlatland().then((scenicUsesFlatland) {
      _useFlatland = scenicUsesFlatland;
      _ready.value = true;
    });
  }

  bool get ready => _ready.value;
  final _ready = false.asObservable();

  void dispose() {
    _focusSubscription.cancel();
  }

  /// Launch Ermine shell and return [FuchsiaViewConnection].
  FuchsiaViewConnection launchErmineShell() {
    _focusSubscription = FocusState.instance.stream().listen(_onFocusChanged);

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

    if (_useFlatland) {
      final viewTokens = ChannelPair();
      assert(viewTokens.status == ZX.OK);
      final viewportCreationToken =
          ViewportCreationToken(value: viewTokens.first!);
      final viewCreationToken = ViewCreationToken(value: viewTokens.second!);

      final createViewArgs =
          CreateView2Args(viewCreationToken: viewCreationToken);
      viewProvider.createView2(createViewArgs);
      viewProvider.ctrl.close();

      return _fuchsiaViewConnection = FuchsiaViewConnection.flatland(
        viewportCreationToken,
        onViewStateChanged: (_, state) {
          if (state == true && !_focusRequested) {
            _focusRequested = true;
            _fuchsiaViewConnection.requestFocus();
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
