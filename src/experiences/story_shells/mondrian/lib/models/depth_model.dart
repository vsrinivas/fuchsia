// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:lib.widgets/model.dart';

export 'package:lib.widgets/model.dart'
    show ScopedModel, Model, ScopedModelDescendant;

/// Gathers the max and min depths of the surface frames being displayed.
class DepthModel extends Model {
  final double minDepth;
  final double maxDepth;

  /// Constructor.
  DepthModel({this.minDepth, this.maxDepth});
}
