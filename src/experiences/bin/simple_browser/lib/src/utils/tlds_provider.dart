// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'package:fuchsia_logger/logger.dart';
import 'package:http/http.dart' as http;

class TldsProvider {
  String _data;

  // Creates the TldsModel Once when the simple browser is initiated.
  Future<String> loadIanaTldsList() async {
    final response =
        await http.get('http://data.iana.org/TLD/tlds-alpha-by-domain.txt');

    if (response.statusCode == 200) {
      log.info('Successfully loaded a TLD list from iana.org');
      return response.body;
    } else {
      log.warning('Failed to load a TLD list from iana.org.');
      return null;
    }
  }

  Future<List<String>> fetchTldsList() async {
    String data;

    data = _data ?? await loadIanaTldsList();

    if (data == null) {
      return null;
    }

    List<String> tldsList = data.split('\n');

    // Removes all white spaces.
    for (int i = 0; i < tldsList.length; i++) {
      tldsList[i] = tldsList[i].replaceAll(RegExp(r'\s+'), '');
    }

    // Removes all comments and empty elements.
    tldsList.removeWhere((item) => item.isEmpty || item.startsWith('#'));

    return tldsList;
  }

  set data(String testData) => _data = testData;
}
