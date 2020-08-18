// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';
import 'package:archive/archive.dart';
import 'sl4f_client.dart';

class FeedbackSnapshot {
  /// The loaded zip.
  final Archive archive;

  /// Creates a new [FeedbackSnapshot] from a string with the zip contents.
  FeedbackSnapshot(String zipContents)
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

  /// Performs a call to `fuchsia.feedback.DataProvider#GetSnapshot` and returns
  /// a [FeedbackSnapshot] that contains the resulting zip.
  Future<FeedbackSnapshot> getSnapshot() async {
    final result =
        await sl4f.request('feedback_data_provider_facade.GetSnapshot', {}) ??
            {};
    if (result.containsKey('zip')) {
      return FeedbackSnapshot(result['zip']);
    }
    return null;
  }
}
