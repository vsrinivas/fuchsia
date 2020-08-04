// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';
import 'package:archive/archive.dart';
import 'sl4f_client.dart';

class FeedbackBugreport {
  /// The loaded zip.
  final Archive archive;

  /// Creates a new [FeedbackBugreport] from a string with the zip contents.
  FeedbackBugreport(String zipContents)
      : archive = ZipDecoder().decodeBytes(base64Decode(zipContents));

  /// Reads the json in inspect.json
  List<dynamic> get inspect {
    for (final file in archive) {
      if (file.name == 'inspect.json') {
        return jsonDecode(String.fromCharCodes(file.content));
      }
    }
    return [];
  }
}

class FeedbackDataProvider {
  final Sl4f sl4f;

  /// Construct a [Feedback] object.
  FeedbackDataProvider(this.sl4f);

  /// Performs a call to `fuchsia.feedback.DataProvider#GetBugreport` and returns
  /// a [FeedbackBugreport] that contains the resulting zip.
  Future<FeedbackBugreport> getBugreport() async {
    final result =
        await sl4f.request('feedback_data_provider_facade.GetBugreport', {}) ??
            {};
    if (result.containsKey('zip')) {
      return FeedbackBugreport(result['zip']);
    }
    return null;
  }
}
