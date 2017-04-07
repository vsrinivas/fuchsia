// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';

import 'package:flutter/services.dart';
import 'package:flutter/widgets.dart';
import 'package:lib.widgets/model.dart';

import 'child_constraints_changer.dart';

const String _kJsonUrl =
    'packages/userpicker_device_shell/res/screen_config.json';

/// Reads possible screen sizes from a configuraion json file.
class ConstraintsModel extends Model {
  List<BoxConstraints> _currentConstraints = const <BoxConstraints>[
    const BoxConstraints()
  ];

  /// Loads this model's JSON from [assetBundle].
  void load(AssetBundle assetBundle) {
    assetBundle.loadString(_kJsonUrl).then(parseJson);
  }

  /// Parses [json] for the [constraints] it contains.
  void parseJson(String json) {
    final Map<String, List<Map<String, String>>> decodedJson =
        JSON.decode(json);

    // Load screen sizes.
    _currentConstraints = decodedJson['screen_sizes']
        .map(
          (Map<String, Object> constraint) => new BoxConstraints.tightFor(
                width: constraint['width'] != null
                    ? double.parse(constraint['width'])
                    : null,
                height: constraint['height'] != null
                    ? double.parse(constraint['height'])
                    : null,
              ),
        )
        .toList();
    _currentConstraints.insert(0, const BoxConstraints());
    notifyListeners();
  }

  /// The [constraints] Armadillo can switch between.  See
  /// [ChildConstraintsChanger] for more information.
  List<BoxConstraints> get constraints => _currentConstraints;
}
