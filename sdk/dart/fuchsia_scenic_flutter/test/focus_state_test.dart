// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fuchsia_scenic_flutter/fuchsia_view.dart';

/// Since mock method call handlers can resolve quickly (before streams get to
/// report a new event to their listeners), it's helpful to have a
/// semaphore-like class to ensure that stream listeners get a chance to execute
/// before the next mock method call is handled.
class ReadersWriterLock {
  int _numReaders = 0;
  int _completedReaders = 0;

  void registerReader() {
    ++_numReaders;
  }

  void readerFinished() {
    ++_completedReaders;
  }

  Future<void> lockExclusive() async {
    // Drain the event loop until all readers have finished reading.
    while (_completedReaders < _numReaders) {
      await Future(() {});
    }
    _completedReaders = 0;
  }
}

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  test('FocusState: current and next', () async {
    // Because this test doesn't purely rely on Futures and relies on callbacks
    // (the mock method call handler), create a completer that will finish when
    // all of the focus states have been dispatched and recieved.
    final completer = Completer();
    final lock = ReadersWriterLock();

    // A list of expected focus events.
    final focuses = [false, true, true, false, true, false, true, true];

    // The index of the current focus state of the test.
    int focusIndex = 0;
    FuchsiaViewsService.instance.platformViewChannel
        .setMockMethodCallHandler((call) async {
      expect(call.arguments, null);

      if (call.method == 'View.focus.getNext') {
        // Wait for streams to be ready before advancing the focus index.
        await lock.lockExclusive();
        if (++focusIndex < focuses.length) {
          return focuses[focusIndex];
        }
        completer.complete();
      } else if (call.method == 'View.focus.getCurrent') {
        return focuses[focusIndex];
      } else {
        fail('Invalid method!');
      }
    });

    void testStream(int targetFocusIndex) async {
      // Defer our execution in the event loop until
      // focusIndex == targetFocusIndex.
      while (focusIndex < targetFocusIndex) {
        await Future(() {});
      }
      lock.registerReader();

      // Set the index at the correct initial position. We use this to keep
      // track of which focus event we're at.
      int index = focusIndex;
      late StreamSubscription<bool> stream;
      stream = FocusState.instance.stream().listen((focused) async {
        // Ensure stream callbacks and the focus state position are consistent.
        expect(index, focusIndex);

        // Check if the focused value is accurate.
        expect(focused, focuses[index]);
        expect(await FocusState.instance.isFocused(), focused);

        // Advance the stream index. If there are no more focus events, cancel
        // and complete this stream.
        if (++index >= focuses.length) {
          await stream.cancel();
        }

        // Unblock the next focus state.
        lock.readerFinished();
      });
    }

    testStream(0);
    testStream(3);
    testStream(6);

    return completer.future;
  });

  test('FocusState: requestFocus', () async {
    late final MethodCall invokedMethod;
    FuchsiaViewsService.instance.platformViewChannel
        .setMockMethodCallHandler((call) {
      invokedMethod = call;
      return Future.value(0);
    });
    await FocusState.instance.requestFocus(42);
    expect(invokedMethod.method, 'View.focus.request');
    expect(invokedMethod.arguments, <String, dynamic>{'viewRef': 42});
  });
}
