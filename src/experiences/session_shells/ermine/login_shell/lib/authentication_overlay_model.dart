// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_fuchsia_ui_views/fidl_async.dart';
import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';
import 'package:fuchsia_scenic_flutter/child_view_connection.dart'
    show ChildViewConnection;
import 'package:lib.widgets/model.dart';

/// Manages the connection and animation of the authentication window.
class AuthenticationOverlayModel extends Model implements TickerProvider {
  ChildViewConnection _childViewConnection;
  AnimationController _transitionAnimation;
  CurvedAnimation _curvedTransitionAnimation;

  /// Constructor.
  AuthenticationOverlayModel() {
    _transitionAnimation = AnimationController(
      value: 0.0,
      duration: Duration(seconds: 1),
      vsync: this,
    );
    _curvedTransitionAnimation = CurvedAnimation(
      parent: _transitionAnimation,
      curve: Curves.fastOutSlowIn,
      reverseCurve: Curves.fastOutSlowIn,
    );
  }

  /// If not null, returns the handle of the current requested overlay.
  ChildViewConnection get childViewConnection => _childViewConnection;

  /// The animation controlling the fading in and out of the authentication
  /// overlay.
  CurvedAnimation get animation => _curvedTransitionAnimation;

  /// Starts showing an overlay over all other content.
  void onStartOverlay(ViewHolderToken overlayViewHolderToken) {
    _childViewConnection = ChildViewConnection(
      overlayViewHolderToken,
      onAvailable: (ChildViewConnection connection) {
        connection.requestFocus();
      },
      onUnavailable: (ChildViewConnection connection) {
        _transitionAnimation.reverse();
      },
    );
    _transitionAnimation.forward();
    notifyListeners();
  }

  /// Stops showing a previously started overlay.
  void onStopOverlay() {
    _transitionAnimation.reverse();
    notifyListeners();
  }

  @override
  Ticker createTicker(TickerCallback onTick) => Ticker(onTick);
}
