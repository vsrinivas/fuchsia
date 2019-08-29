// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:developer' show Timeline;

import 'package:lib.widgets/model.dart';

const Duration _kPlaceholderDuration = Duration(milliseconds: 2700);

/// A model for handling the transition of placeholders to Surfaces.
/// This implementation does not do a crossfade as labelling nodes non-shadow
/// casting with opacity enabled is not yet plumbed, but the transition timer
/// is in place.
class SurfaceTransitionModel extends SpringModel {
  // If the Surface has just been added, run the transition to cover
  // the module startup
  SurfaceTransitionModel() {
    Timer(_kPlaceholderDuration, _start);
  }

  double get opacity => value;

  void _start() {
    Timeline.instantSync('starting placeholder transition');
    target = 1.0; // start ticking
  }
}
