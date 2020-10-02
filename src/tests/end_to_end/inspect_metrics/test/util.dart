// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:test/test.dart';
import 'package:sl4f/sl4f.dart' as sl4f;

// Return all matched inspect properties as a list.
Future<List<dynamic>> getInspectValues(
  sl4f.Inspect inspect,
  String selector, {
  sl4f.InspectPipeline pipeline = sl4f.InspectPipeline.none,
}) async {
  final list = selector.split(':');
  if (list.length != 3) {
    fail('selector format should contain 2 colons');
  }

  final top = await inspect.snapshot(['$selector'], pipeline: pipeline);
  if (top == null || top.isEmpty) {
    print('inspect selector $selector does not exist');
    return [];
  }

  List<dynamic> out = [];

  for (final component in top) {
    if (component['errors']?.isNotEmpty ?? false) {
      for (var e in component['errors']) {
        print('Error: $e');
      }
    }
    List<dynamic> next = [component['payload']];

    while (next.isNotEmpty) {
      var cur = next.removeLast();

      if (cur == null) {
        // Skip nulls
        continue;
      } else if (cur is Map<String, dynamic>) {
        if (cur.isEmpty) {
          // Add empty maps as output values.
          out.add(cur);
        } else {
          // Process each child of this map.
          cur.forEach((_, v) {
            next.add(v);
          });
        }
      } else {
        // If the value is not a map, add it to the output.
        out.add(cur);
      }
    }
  }

  return out;
}

dynamic singleValue(dynamic matcher) {
  return allOf(isNotNull, hasLength(1), contains(matcher));
}

dynamic multiValue(dynamic matcher, {dynamic length}) {
  return allOf(
      isNotNull, hasLength(length ?? greaterThan(1)), everyElement(matcher));
}

void printErrorHelp() {
  print('If this test fails, see '
      'https://fuchsia.googlesource.com/a/fuchsia/+/master/src/tests/end_to_end/inspect_metrics/README.md'
      ' for details!');
}
