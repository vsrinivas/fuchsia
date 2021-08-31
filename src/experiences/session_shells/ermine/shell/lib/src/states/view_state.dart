// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:ui';

import 'package:ermine_utils/ermine_utils.dart';
import 'package:fuchsia_scenic_flutter/fuchsia_view.dart';

/// Defines the state of an application view.
abstract class ViewState {
  FuchsiaViewConnection get viewConnection;
  ViewHandle get view;
  String get title;
  String? get url;
  String? get id;
  Rect? get viewport;

  abstract bool hitTestable;
  abstract bool focusable;

  /// Returns true if the application fails to render a frame until a timeout.
  bool get timeout;

  /// Returns true if the application closes itself due to error or exit.
  bool get closed;

  /// Call to close (quit) the application.
  void close();

  /// Call to continue waiting on the application to render its first frame.
  void wait();

  /// Returns true when the view has rendered a frame.
  bool get loaded;

  /// Returns true when the view is connected to the view tree but has not
  /// rendered a frame.
  bool get loading;

  /// Returns true if the view is visible on the screen (partly or fullscreen).
  bool get visible;

  /// Set focus on this view.
  void setFocus({
    int retry,
    Duration backOff,
  });

  /// Cancel any pending set focus operation on this view.
  void cancelSetFocus();
}
