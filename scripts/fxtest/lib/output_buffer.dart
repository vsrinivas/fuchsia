// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io' as io;

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
  final io.Stdout stdout;

  /// Set to `true` if we previously entered a trailing newline for cosmetic
  /// purposes. Useful when we hope to emit whole lines after emitting
  /// substrings.
  bool _isCursorOnNewline;

  final _ansiEscape = String.fromCharCode(27);

  OutputBuffer([stdout])
      : _buffer = [],
        _isCursorOnNewline = false,
        stdout = stdout ?? io.stdout;

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
    stdout
      ..write('$_ansiEscape[2K') // clear line
      ..write('$_ansiEscape[0G'); // cursor to 0-index on current line
  }

  void _cursorUp() {
    stdout.write('$_ansiEscape[1A'); // cursor up
  }

  /// Appends additional characters to the last line of the buffer
  void addSubstring(String msg, {bool shouldFlush = true}) {
    if (_buffer.isEmpty) {
      _buffer.add('');
    }
    _buffer.last += msg;

    if (shouldFlush) {
      stdout.write(msg);
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
      stdout.writeln('');
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
    stdout.writeln(lines.join('\n'));
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
