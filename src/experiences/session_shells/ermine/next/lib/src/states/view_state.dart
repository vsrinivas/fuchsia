// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fuchsia_scenic_flutter/fuchsia_view.dart';
import 'package:mobx/mobx.dart';

import 'package:next/src/utils/view_handle.dart';

/// Defines the state of an application view.
abstract class ViewState with Store {
  FuchsiaViewConnection get viewConnection;
  ViewHandle get view;
  String get title;

  ObservableValue<bool> get hitTestable;
  ObservableValue<bool> get focusable;

  /// Returns true when the view has rendered a new frame.
  ObservableValue<bool> get ready;

  /// Returns true if the application closes itself due to error or exit.
  ObservableValue<bool> get closed;

  /// Call to close (quit) the application.
  Action get close;
}
