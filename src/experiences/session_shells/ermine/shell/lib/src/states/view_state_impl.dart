// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:ui';

import 'package:ermine/src/services/presenter_service.dart';
import 'package:ermine/src/states/view_state.dart';
import 'package:ermine_utils/ermine_utils.dart';
import 'package:fuchsia_scenic_flutter/fuchsia_view.dart';
import 'package:mobx/mobx.dart';

/// Defines an implementation of [ViewState].
class ViewStateImpl with Disposable implements ViewState {
  @override
  final FuchsiaViewConnection viewConnection;

  @override
  final String title;

  final String? id;

  @override
  final String? url;

  @override
  final ViewHandle view;

  @override
  final Observable<bool> ready = false.asObservable();

  @override
  final Observable<bool> closed = false.asObservable();

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

  /// Called by [PresenterService] when the view has rendered a new frame.
  void viewStateChanged({required bool state}) {
    runInAction(() {
      ready.value = ready.value || state;
    });
  }
}
