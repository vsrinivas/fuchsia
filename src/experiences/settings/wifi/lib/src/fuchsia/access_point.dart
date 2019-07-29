// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:math' as math;

/// An Access point you can connect to via wifi.
class AccessPoint {
  /// Name of the access point.
  final String name;

  /// The signal strength of the access point.
  final double signalStrength;

  /// True if this access point is secured.
  final bool isSecure;

  /// Constructor.
  const AccessPoint({this.name, this.signalStrength, this.isSecure});

  /// The image url for an icon representing the signal strength for this access
  /// point.
  String get url {
    int percent = ((_getInt8(signalStrength.round()) + 100) * 2).clamp(0, 100);
    int bars = (percent > 80)
        ? 4
        : (percent > 60) ? 3 : (percent > 40) ? 2 : (percent > 20) ? 1 : 0;
    return 'assets/ic_signal_wifi_'
        '$bars'
        '_bar_'
        '${isSecure && bars > 0 ? 'lock_' : ''}'
        'grey600_48dp.png';
  }

  int _getInt8(int uint8) {
    if (uint8 > math.pow(2, 7) - 1) {
      return uint8 - math.pow(2, 8);
    }
    return uint8;
  }

  @override
  String toString() => 'AccessPoint($name => $signalStrength)';
}
