// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:internationalization/strings.dart';

class License extends StatefulWidget {
  @override
  _LicenseText createState() => _LicenseText();
}

class _LicenseText extends State<License> {
  // Localized strings.
  static String get _loading => Strings.loading;

  @override
  Widget build(BuildContext context) => FutureBuilder(
        future: fetchText(),
        builder: (context, snapshot) =>
            snapshot.hasData ? Text('${snapshot.data}') : Text(_loading),
      );

  Future<String> fetchText() async {
    String licenseBody = await rootBundle.loadString('assets/license.html');
    return licenseBody;
  }
}
