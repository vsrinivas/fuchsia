// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:ui';

import 'package:ermine/src/services/presenter_service.dart';
import 'package:ermine/src/states/view_state.dart';
import 'package:ermine_utils/ermine_utils.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_scenic_flutter/fuchsia_view.dart';
import 'package:mobx/mobx.dart';

/// Defines an implementation of [ViewState].
class ViewStateImpl with Disposable implements ViewState {
  @override
  final FuchsiaViewConnection viewConnection;

  @override
  final String title;

  @override
  final String? id;

  @override
  final String? url;

  @override
  final ViewHandle view;

  final Observable<bool> _rendered = false.asObservable();

  final Observable<bool> _connected = false.asObservable();

  @override
  final Observable<bool> timeout = false.asObservable();

  @override
  final Observable<bool> closed = false.asObservable();

  /// Returns true when the view is connected to the view tree but has not
  /// rendered a frame.
  @override
  bool get loading => _connected.value;

  /// Returns true when the view has rendered a frame.
  @override
  bool get loaded => _rendered.value;

  @override
  bool get visible {
    return viewConnection.viewport?.isEmpty == false;
  }

  ViewStateImpl({
    required this.viewConnection,
    required this.view,
    required this.title,
    required VoidCallback onClose,
    this.id,
    this.url,
  }) : _onClose = onClose;

  @override
  final Observable<bool> hitTestable = true.asObservable();

  @override
  final Observable<bool> focusable = true.asObservable();

  @override
  Rect? get viewport => viewConnection.viewport;

  final VoidCallback _onClose;
  @override
  late final Action close = () {
    closed.value = true;
    _onClose();
  }.asAction();

  @override
  late final Action wait = () {
    timeout.value = false;
    Future.delayed(
        Duration(seconds: 10), () => runInAction(() => timeout.value = true));
  }.asAction();

  /// Called by [PresenterService] when the view is attached to the scene.
  void viewConnected() {
    // Mark the view as ready, so that it can be focused.
    runInAction(() => _connected.value = true);

    // Mark the view as timed-out if it fails to render in time.
    Future.delayed(
        Duration(seconds: 10), () => runInAction(() => timeout.value = true));
  }

  /// Called by [PresenterService] when the view has rendered a new frame.
  void viewStateChanged({required bool state}) {
    runInAction(() {
      // Mark the view as ready, so that it can be focused.
      _connected.value = _connected.value || state;

      // The view has rendered its first frame.
      _rendered.value = true;

      // Cancel timeout waiting for the view to render its first frame.
      timeout.value = false;
    });
  }

  bool _requestFocusPending = false;
  static const _kMaxRetries = 10;
  static const _kBackoffDuration = Duration(milliseconds: 8);

  /// Recursively request focus on this view with [_kMaxRetries] and exponential
  /// [backOff].
  ///
  /// Use [FocusState] instance to request focus on the underlying scenic view,
  /// retrying recursively.The retry is also aborted if [cancelSetFocus]
  /// is issued while it was pending.
  @override
  void setFocus({
    int retry = _kMaxRetries,
    Duration backOff = _kBackoffDuration,
  }) {
    if (loaded && !visible) {
      // Reset connected flag to retry setFocus on next viewStateChanged.
      _connected.value = false;
    } else {
      if (retry < _kMaxRetries && !_requestFocusPending) {
        return;
      }
      _requestFocusPending = true;
      FocusState.instance.requestFocus(view.handle).then((_) {
        _requestFocusPending = false;
      }).catchError((e) {
        if (retry > 0) {
          Future.delayed(backOff, () {
            setFocus(
              retry: retry - 1,
              backOff: Duration(milliseconds: backOff.inMilliseconds * 2),
            );
          });
        } else {
          log.warning('Failed to set focus on $title: $e');
        }
      });
    }
  }

  @override
  void cancelSetFocus() {
    _requestFocusPending = false;
  }
}
