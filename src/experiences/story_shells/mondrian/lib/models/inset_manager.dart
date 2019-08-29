// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:lib.widgets/model.dart';
import 'package:lib.widgets/widgets.dart';

export 'package:lib.widgets/model.dart'
    show ScopedModel, Model, ScopedModelDescendant;

const RK4SpringDescription _kSimulationDesc =
    RK4SpringDescription(tension: 450.0, friction: 50.0);

/// Frame for child views
class InsetManager extends SpringModel {
  /// Constructor.
  InsetManager() : super(springDescription: _kSimulationDesc);

  /// Call with the number of surfaces that are in the graph.
  void onSurfacesChanged({int surfaces}) {
    target = surfaces > 1 ? 12.0 : 0.0;
  }
}
