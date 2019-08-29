// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:lib.widgets/model.dart';

const String _kMode = 'mode';
const String _kModeNormal = 'normal';
const String _kModeEdgeToEdge = 'edgeToEdge';

const double _kMinScreenWidth = 200.0;
const double _kMinScreenRatio = 1.0 / 5.0;

const double _kMinScreenWidthEdgeToEdge = 200.0;
const double _kMinScreenRatioEdgeToEdge = 1.0 / 3.0;

/// Manages layout constants.
class LayoutModel extends Model {
  double _minScreenWidth = _kMinScreenWidth;
  double _minScreenRatio = _kMinScreenRatio;

  /// The minimum width for copresentation.
  double get minScreenWidth => _minScreenWidth;

  /// The minimum screen ratio for copresentation.
  double get minScreenRatio => _minScreenRatio;

  /// Called when the device profile changes.
  void onDeviceProfileChanged(Map<String, String> deviceProfile) {
    switch (deviceProfile[_kMode]) {
      case _kModeNormal:
        if (_minScreenWidth != _kMinScreenWidth ||
            _minScreenRatio != _kMinScreenRatio) {
          _minScreenWidth = _kMinScreenWidth;
          _minScreenRatio = _kMinScreenRatio;
          notifyListeners();
        }
        break;
      case _kModeEdgeToEdge:
        if (_minScreenWidth != _kMinScreenWidthEdgeToEdge ||
            _minScreenRatio != _kMinScreenRatioEdgeToEdge) {
          _minScreenWidth = _kMinScreenWidthEdgeToEdge;
          _minScreenRatio = _kMinScreenRatioEdgeToEdge;
          notifyListeners();
        }
        break;
      default:
        // Unknown mode.
        break;
    }
  }
}
