// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:io' as io;

import 'package:fxtest/exceptions.dart';

/// Wrapper around iteratively build command line output.
///
/// Uses control/escape sequences to reset output when necessary, but otherwise
/// simply appends new output to the end of previously written messages.
///
/// Usage:
///
/// ```dart
/// OutputBuffer outputBuffer = OutputBuffer(standardOut: stdout);
///
/// // Prints "this will appear on its own line\n"
/// outputBuffer.addLine('this will appear on its own line');
///
/// // Prints "this will be added to an in-progress line"  (no newline!)
/// outputBuffer.addSubstring('this will be added to');
/// outputBuffer.addSubstring(' an in-progress line');
///
/// // Prints "\nbut now back to whole lines\n" (leading newline bc it followed
/// // a partial line, plus trailing)
/// outputBuffer.addLine('but now back to whole lines')
/// ```
class OutputBuffer {
  /// The actual content of our output. Can be built and flushed iteratively.
  final List<String> _buffer;

  /// The ultimate destination for our output. If not provided, defaults to
  /// the global [Stdout]
  final io.IOSink _stdout;

  /// Set to `true` if we previously entered a trailing newline for cosmetic
  /// purposes. Useful when we hope to emit whole lines after emitting
  /// substrings.
  bool _isCursorOnNewline;

  /// Driver used to programmatically resolve a future that approximates
  /// `_stdout.done`.
  ///
  /// This is important because if the user runs `fx test | -head X`, where X
  /// is greater than the number of lines of output, then having awaited the
  /// future from `_stdout.done` will cause the program to hang. On the other
  /// hand, not awaiting that future causes the error to not be caught and
  /// bubble up as an unhandled exception.
  ///
  /// Thus, the only way to drive this ourselves is to expose both, 1) a future
  /// inside a completer, and 2) a method external actors can use to close said
  /// future. Calling classes make use of the [close] method once the test suite
  /// reaches its natural conclusion.
  final Completer _stdoutCompleter;

  final _ansiEscape = String.fromCharCode(27);

  OutputBuffer({
    io.IOSink stdout,

    // Controls how the buffer should receive its first content.
    // Does not have a visual impact immediately
    bool cursorStartsOnNewLine = false,
  })  : _buffer = [],
        _stdoutCompleter = Completer(),
        _isCursorOnNewline = cursorStartsOnNewLine,
        _stdout = stdout ?? io.stdout {
    /// Listen to the actual `_stdout.done` future and resolve our approximation
    /// with an error if that closes before the test suite completes.
    _stdout.done.catchError((err) => _closeFutureWithError());
  }

  void _closeFutureWithError() => !_stdoutCompleter.isCompleted
      ? _stdoutCompleter.completeError(OutputClosedException())
      : null;

  /// Future used to approximate when the stdout is closed
  Future stdOutClosedFuture() => _stdoutCompleter.future;

  /// Resolves the future that waits for the stdout to close. Use this when
  /// the test suite has reached its natural conclusion.
  void close() =>
      !_stdoutCompleter.isCompleted ? _stdoutCompleter.complete(true) : null;

  void forcefullyClose() => _closeFutureWithError();

  void _clearLines([int lines]) {
    lines ??= 1;

    if (_isCursorOnNewline) {
      _cursorUp();
      _isCursorOnNewline = false;
    }

    int counter = 0;
    while (counter < lines) {
      _clearLine();
      if (counter < lines - 1) {
        _cursorUp();
      }
      counter++;
    }
  }

  void _clearLine() {
    _stdout
      ..write('$_ansiEscape[2K') // clear line
      ..write('$_ansiEscape[0G'); // cursor to 0-index on current line
  }

  void _cursorUp() {
    _stdout.write('$_ansiEscape[1A'); // cursor up
  }

  /// Appends additional characters to the last line of the buffer
  void addSubstring(String msg, {bool shouldFlush = true}) {
    if (_buffer.isEmpty) {
      _buffer.add('');
    }
    _buffer.last += msg;

    if (shouldFlush) {
      _stdout.write(msg);
      _isCursorOnNewline = false;
    }
  }

  /// Adds an additional line to the end of the buffer
  void addLine(String msg, {bool shouldFlush = true}) {
    addLines([msg], shouldFlush: shouldFlush);
  }

  /// Adds N additional lines to the end of the buffer
  void addLines(List<String> msgs, {bool shouldFlush = true}) {
    if (!_isCursorOnNewline) {
      _stdout.writeln('');
      _isCursorOnNewline = true;
    }
    msgs.forEach(_registerLine);
    if (shouldFlush) {
      _flushLines(msgs);
    }
  }

  /// Gracefully handles adding lines that themselves contain newlines, since
  /// otherwise that breaks some assumptions.
  void _registerLine(String line) {
    _buffer.addAll(line.split('\n'));
  }

  /// Replaces the content of individual lines in the buffer and, optionally,
  /// reflushes them to the [stdout].
  ///
  /// Assumes the passed list of strings should overlay the end of [buffer].
  /// Thus, if [buffer] is a list of 10 strings, and this function is invoked
  /// with a list of 3 strings, indices 7, 8, and 9 (the last three elements)
  /// will be replaced.
  void updateLines(List<String> msgs, {bool shouldFlush = true}) {
    while (msgs.length > _buffer.length) {
      _buffer.add('');
    }
    for (int count in Iterable<int>.generate(msgs.length)) {
      int indexToReplace = _buffer.length - msgs.length + count;
      _buffer[indexToReplace] = msgs[count];
    }

    if (shouldFlush) {
      _clearLines(msgs.length);
      _flushLines(msgs);
    }
  }

  /// Eliminates lines off the end of the buffer and moves the cursor up the
  /// same number of lines.
  void stripLines(int numLines) {
    _buffer.removeRange(_buffer.length - numLines, _buffer.length);
    _clearLines(numLines);
  }

  /// Sends a list of strings to the [stdout]. Is completely unaware of the
  /// [buffer], so calling this directly can desync the internal state from what
  /// is rendered in the terminal.
  void _flushLines(List<String> lines) {
    _stdout.writeln(lines.join('\n'));
    _isCursorOnNewline = true;
  }

  /// Writes the entire [buffer] to the [stdout].
  void flush({int start, int end}) {
    _flushLines(_buffer.sublist(start ?? 0, end ?? _buffer.length));
  }

  /// Clears the [stdout], without touching the [buffer]. If you are not
  /// planning to re-flush, you should consider also resetting [buffer].
  void clear() {
    _clearLines(_isCursorOnNewline ? _buffer.length : _buffer.length - 1);
  }
}
