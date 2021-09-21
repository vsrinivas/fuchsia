// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:async';
import 'dart:convert';
import 'dart:io' as io;
import 'dart:math';
import 'package:fxtest/exceptions.dart';
import 'package:meta/meta.dart';
import 'package:path/path.dart' as p;

abstract class StandardOut {
  void write(String line);
  void writeln(String line);
  Future<dynamic> get done;
  Future<dynamic> close();
}

class RealStandardOut implements StandardOut {
  @override
  void write(String line) => io.stdout.write(line);
  @override
  void writeln(String line) => io.stdout.writeln(line);
  @override
  Future<dynamic> get done => io.stdout.done;
  @override
  Future<dynamic> close() => io.stdout.close();
}

/// Implementation of an [OutputBuffer]'s [StandardOut] that writes all content
/// to a file on disk.
class FileStandardOut implements StandardOut {
  final bool deleteIfExisting;

  /// If true, we create the full path as a folder and then place our test
  /// file in there.
  final Completer<bool> _closedCompleter;
  String path;
  io.File _file;
  io.IOSink _sink;

  FileStandardOut(this.path, {this.deleteIfExisting = false})
      : _closedCompleter = Completer<bool>(),
        assert(path != null),
        assert(path != '');

  void initPath() {
    if (_file == null && io.Directory(path).existsSync()) {
      // If the path points to a directory, create a timestamped file.
      path = p.join(path, 'fxtest-${DateTime.now().toIso8601String()}.log');
    }
  }

  void initFile() {
    initPath();
    if (_file == null) {
      if (deleteIfExisting && io.File(path).existsSync()) {
        // If the path points to an existing file, delete it if requested.
        // (Otherwise, we'll append our output.)
        io.File(path).deleteSync();
      }
      _file = io.File(path);
      _sink = _file.openWrite(mode: io.FileMode.append);
    }
  }

  @override
  Future<dynamic> close() async {
    _sink?.writeln('');
    await _sink?.close();
    _closedCompleter.complete(true);
  }

  @override
  Future get done => _closedCompleter.future;

  @override
  void write(String line) {
    initFile();
    _sink?.add(utf8.encode(line));
  }

  @override
  void writeln(String line) {
    initFile();
    _sink?.add(utf8.encode('\n$line'));
  }
}

class LocMemStandardOut implements StandardOut {
  bool isOpen;
  final List<String> buffer;
  final Completer<dynamic> _done;
  LocMemStandardOut()
      : isOpen = true,
        buffer = [],
        _done = Completer<dynamic>();
  @override
  void write(String line) => isOpen
      // If the buffer is empty,
      ? buffer.isEmpty //
          // add the line as the first element
          ? buffer.add(line)
          // otherwise, append the line
          : buffer.last += line
      // And if we weren't even open, raise an error
      : throw io.StdoutException('IO is closed.');
  @override
  void writeln(String line) =>
      isOpen ? buffer.add(line) : throw io.StdoutException('IO is closed.');
  @override
  Future<dynamic> get done => _done.future;
  @override
  Future<dynamic> close() {
    isOpen = false;
    !_done.isCompleted
        ? _done.complete()
        : throw io.StdoutException('IO is already closed.');
    return _done.future;
  }
}

/// Wrapper around iteratively build command line output.
///
/// Uses control/escape sequences to reset output when necessary, but otherwise
/// simply appends new output to the end of previously written messages.
///
/// Usage:
///
/// ```dart
/// OutputBuffer outputBuffer = OutputBuffer(stdout: stdout);
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
  /// The actual stuff of our output. Can be built and flushed iteratively.
  final List<String> content;

  /// Helper which implements all necessary functions of [io.stdout].
  final StandardOut stdout;

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

  /// OS-aware line splitting resource.
  final _splitter = LineSplitter();

  final _ansiEscape = String.fromCharCode(27);

  OutputBuffer._({
    this.stdout,

    // Controls how the buffer should receive its first content. Does not have
    // immediate visual impact.
    bool cursorStartsOnNewLine = false,
  })  : content = [],
        _stdoutCompleter = Completer(),
        _isCursorOnNewline = cursorStartsOnNewLine ?? false {
    /// Listen to the actual `stdout.done` future and resolve our approximation
    /// with an error if that closes before the test suite completes.
    stdout.done.catchError((err) => _closeFutureWithError());
  }

  factory OutputBuffer.realIO({bool cursorStartsOnNewLine}) {
    return OutputBuffer._(
      cursorStartsOnNewLine: cursorStartsOnNewLine,
      stdout: RealStandardOut(),
    );
  }

  factory OutputBuffer.fileIO({@required String path}) {
    return OutputBuffer._(
      cursorStartsOnNewLine: false,
      stdout: FileStandardOut(path),
    );
  }

  factory OutputBuffer.locMemIO({bool cursorStartsOnNewLine}) {
    return OutputBuffer._(
      cursorStartsOnNewLine: cursorStartsOnNewLine,
      stdout: LocMemStandardOut(),
    );
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

  void _clearLines([int lines = 1]) {
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
    if (content.isEmpty) {
      content.add('');
    }
    content.last += msg;

    if (shouldFlush) {
      stdout.write(msg);
      _isCursorOnNewline = false;
    }
  }

  /// Adds an additional line to the end of the buffer
  void addLine(String msg, {bool shouldFlush = true}) {
    addLines(_splitter.convert(msg), shouldFlush: shouldFlush);
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
    content.addAll(_splitter.convert(line));
  }

  /// Replaces the content of individual lines in the buffer and, optionally,
  /// reflushes them to the [stdout].
  ///
  /// Assumes the passed list of strings should overlay the end of [buffer].
  /// Thus, if [buffer] is a list of 10 strings, and this function is invoked
  /// with a list of 3 strings, indices 7, 8, and 9 (the last three elements)
  /// will be replaced.
  void updateLines(List<String> msgs, {bool shouldFlush = true}) {
    while (msgs.length > content.length) {
      content.add('');
    }
    List<String> replacedLines = content.sublist(content.length - msgs.length);
    for (int count in Iterable<int>.generate(msgs.length)) {
      int indexToReplace = content.length - msgs.length + count;
      content[indexToReplace] = msgs[count];
    }

    if (shouldFlush) {
      var numToFlush = _getTerminalRowsFromLines(replacedLines);
      _clearLines(numToFlush);
      _flushLines(msgs);
    }
  }

  /// Calculates how much various strings had to wrap on the current terminal.
  ///
  /// This is important because printing one extra long sentence will wrap onto
  /// multiple rows in the terminal, but then clearing "a line" does not go back
  /// to the last newline -- but instead just clears a given terminal row.
  int _getTerminalRowsFromLines(List<String> lines) {
    if (!io.stdout.hasTerminal) {
      return lines.length;
    }
    var rowsPerLine = lines.fold<int>(0, (previousValue, line) {
      var bareLine = _stripAnsi(line).length;
      var numWholeLines = bareLine ~/ io.stdout.terminalColumns;
      var numPartialLines = bareLine % io.stdout.terminalColumns > 0 ? 1 : 0;
      return previousValue + numWholeLines + numPartialLines;
    });
    return max(rowsPerLine, lines.length);
  }

  String _stripAnsi(String val) {
    var re = RegExp(r'(\x9B|\x1B\[)[0-?]*[ -\/]*[@-~]');
    return val.replaceAll(re, '');
  }

  /// Sends a list of strings to the [stdout]. Is completely unaware of the
  /// [buffer], so calling this directly can desync the internal state from what
  /// is rendered in the terminal.
  void _flushLines(List<String> lines) {
    stdout.writeln(lines.join('\n'));
    _isCursorOnNewline = true;
  }

  /// Scans the end of the buffer and sets the amount of trailing newlines
  /// (represented by empty strings in our list of strings) to the desired level.
  /// If the passed number is greater than the current number of empty lines,
  /// an appropriate amount of newlines are added.
  void reduceEmptyRowsTo(int number) {
    bool stillLookingForLineWithContent = true;
    int depth = 0;
    while (stillLookingForLineWithContent) {
      if (content[content.length - depth - 1].isEmpty) {
        depth += 1;
      } else {
        stillLookingForLineWithContent = false;
      }
    }
    if (depth > number) {
      _clearLines(depth - number);
    } else if (depth <= number) {
      var emptyLines = <String>[];
      for (var counter = 0; counter < (number - depth + 1); counter++) {
        emptyLines.add('');
      }
      addLines(emptyLines);
    }
  }

  /// Writes the entire [buffer] to the [stdout].
  void flush({int start, int end}) {
    _flushLines(content.sublist(start ?? 0, end ?? content.length));
  }

  /// Clears the [stdout], without touching the [buffer]. If you are not
  /// planning to re-flush, you should consider also resetting [buffer].
  void clear() {
    int allRows = _getTerminalRowsFromLines(content);
    // If the cursor is currently on a new line, deleting *all* the lines will
    // go so far back as to delete the original prompt
    _clearLines(_isCursorOnNewline ? allRows : allRows - 1);
  }
}
