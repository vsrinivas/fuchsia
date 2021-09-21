// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

// Refer to //src/tests/end_to_end/perf/test/dart_inspect_benchmarks_test.dart
// for the benchmark runner. It needs to be kept in sync with this code.

import 'dart:developer' show Timeline;
import 'dart:typed_data';

import 'package:args/args.dart';
import 'package:fuchsia/fuchsia.dart' as fuchsia;
import 'package:fuchsia_inspect/inspect.dart';
import 'package:fuchsia_services/services.dart';

// ignore: avoid_classes_with_only_static_members
class UniqueNumber {
  static int value = 0;

  static int next() => value++;
}

// Returns a periodic extension of `pattern` up until the
// given `length`.  Example: `periodic('foo', 7) => "foofoof"`
String periodic(String pattern, int length) {
  return (pattern * (length ~/ pattern.length + 1)).substring(0, length);
}

// Runs one-shot create-set-reset-delete for a string property.  reps allows one
// to vary the string size and generate long strings based on repetitions of a
// short message.  setMsg and resetMsg are test names used to refer to the
// measurements in the sl4f test runner.
void stringPropertyLifecycle(Node root,
    {int length = -1,
    String getMsg = 'undefined',
    String setMsg = 'undefined',
    String resetMsg = 'undefined'}) {
  String str1 = periodic('hello world', length);
  String str2 = periodic('groetjes wereld (nl)', length);

  Timeline.startSync(getMsg);
  var property = root.stringProperty('property-${UniqueNumber.next()}');
  Timeline.finishSync();
  try {
    Timeline.startSync(setMsg);
    property.setValue(str1);
    Timeline.finishSync();

    Timeline.startSync(resetMsg);
    property.setValue(str2);
    Timeline.finishSync();
  } finally {
    property.delete();
  }
}

// Measure the time it takes to allocate a string which is about as large as
// a longish URL.  This is similar to exerciseStringPropertyLong, except the
// length of a string we store is somewhat more realistic.
void exerciseLongURL(Node root) {
  String pseudoURL = 'X' * 100;
  Timeline.startSync('URL sized string');
  var property = root.stringProperty('property-${UniqueNumber.next()}');
  try {
    property.setValue(pseudoURL);
  } finally {
    property.delete();
  }
  Timeline.finishSync();
}

// Checks if there are gotchas with long strings exported as properties.
void exerciseStringPropertyLong(Node root) {
  stringPropertyLifecycle(root,
      length: 1000,
      getMsg: 'Get string long',
      setMsg: 'Set string long',
      resetMsg: 'Reset string long');
}

void exerciseStringPropertyShort(Node root) {
  stringPropertyLifecycle(root,
      length: 1,
      getMsg: 'Get string',
      setMsg: 'Set string',
      resetMsg: 'Reset string');
}

// Similar to stringPropertyLifecycle above, but for bytes.
void bytePropertyLifecycle(Node root,
    {int length = 100,
    String getMsg = 'Get byte',
    String setMsg = 'Set byte',
    String resetMsg = 'Reset byte'}) {
  // We could perhaps factor common code out of here and stringPropertyLifecycle.
  ByteData data1 = ByteData(length);
  ByteData data2 = ByteData(length)..setInt8(length - 1, 0x42);
  Timeline.startSync(getMsg);
  var property = root.byteDataProperty('property-${UniqueNumber.next()}');
  Timeline.finishSync();

  try {
    Timeline.startSync(setMsg);
    property.setValue(data1);
    Timeline.finishSync();

    Timeline.startSync(resetMsg);
    property.setValue(data2);
    Timeline.finishSync();
  } finally {
    property.delete();
  }
}

// Times the creation,setting and deletion of a short byte property.
void exerciseBytePropertyShort(Node root) {
  bytePropertyLifecycle(root,
      length: 32,
      getMsg: 'Get byte',
      setMsg: 'Set byte',
      resetMsg: 'Reset byte');
}

// Times the creation,setting and deletion of a long byte property.
void exerciseBytePropertyLong(Node root) {
  bytePropertyLifecycle(root,
      length: 1025,
      getMsg: 'Get byte long',
      setMsg: 'Set byte long',
      resetMsg: 'Reset byte long');
}

// Times the creation, value change and removal of a double.
void exerciseDoubleCounter(Node root) {
  Timeline.startSync('Get double');
  var counter = root.doubleProperty('double-${UniqueNumber.next()}');
  Timeline.finishSync();

  try {
    Timeline.startSync('Set double');
    counter.setValue(1);
    Timeline.finishSync();

    Timeline.startSync('Inc double');
    counter.add(1);
    Timeline.finishSync();

    Timeline.startSync('Dec double');
    counter.subtract(1);
    Timeline.finishSync();
  } finally {
    counter.delete();
  }
}

// Times the creation, value change and removal of an integer counter.
void exerciseIntCounter(Node root) {
  Timeline.startSync('Get integer');
  var counter = root.intProperty('int-${UniqueNumber.next()}');
  Timeline.finishSync();

  try {
    Timeline.startSync('Set integer');
    counter.setValue(1);
    Timeline.finishSync();

    Timeline.startSync('Inc integer');
    counter.add(1);
    Timeline.finishSync();

    Timeline.startSync('Dec integer');
    counter.subtract(1);
    Timeline.finishSync();
  } finally {
    counter.delete();
  }
}

// Adds and removes a child property, timing individual operations.
void exerciseChildNode(Node root) {
  Timeline.startSync('Add child');
  Node child = root.child('child');
  Timeline.finishSync();

  try {
    String name = 'foo${UniqueNumber.next()}';
    Timeline.startSync('Add property');
    var childProp = child.intProperty(name);
    Timeline.finishSync();

    Timeline.startSync('Delete property');
    childProp.delete();
    Timeline.finishSync();
  } finally {
    Timeline.startSync('Delete node');
    child.delete();
    Timeline.finishSync();
  }
}

// Measures the effects of the allocator to the allocation timing.
// Creates a subtree of uniquely named properties, which should cause
// allocations to happen.
void exerciseAllocation(Node root) {
  Timeline.startSync('Allocation init');
  Node child = root.child('alloc');
  Timeline.finishSync();

  try {
    var testNum = UniqueNumber.next();
    Timeline.startSync('Allocation');
    var string = child.stringProperty('string-$testNum');
    var int = child.intProperty('int-$testNum');
    var string2 = child.stringProperty('string2-$testNum');
    child.intProperty('int-$testNum').delete();
    Timeline.finishSync();

    // Ensure no objects are removed.
    string2.delete();
    int.delete();
    string.delete();
  } finally {
    child.delete();
  }
}

void doSingleIteration() {
  Timeline.startSync('Get root');
  var root = Inspect().root;
  Timeline.finishSync();

  exerciseIntCounter(root);
  exerciseDoubleCounter(root);
  exerciseChildNode(root);
  exerciseStringPropertyShort(root);
  exerciseStringPropertyLong(root);
  exerciseBytePropertyShort(root);
  exerciseBytePropertyLong(root);
  exerciseAllocation(root);
  exerciseLongURL(root);
}

void main(List<String> args) {
  final context = ComponentContext.createAndServe();

  var parser = ArgParser()
    ..addOption('iterations', defaultsTo: '500', valueHelp: 'iterations');

  int iterations;
  try {
    var parsedArgs = parser.parse(args);
    iterations = int.parse(parsedArgs['iterations']);
  } on FormatException {
    print('dart_inspect_benchmarks got bad args. Please check usage.');
    print('  args = "$args"');
    print(parser.usage);
    fuchsia.exit(1);
  }

  Timeline.startSync('Init and get root');
  (Inspect()..serve(context.outgoing)).root;
  Timeline.finishSync();

  for (int i = 0; i < iterations; i++) {
    doSingleIteration();
  }
  fuchsia.exit(0);
}
