// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:ui' as ui;

/// Class for pointer data resampling.
class PointerDataResampler {
  // Data enqueued for processing.
  final _queuedData = <ui.PointerData>[];

  /// Pointer state required for resampling.
  ui.PointerData _last;
  ui.PointerData _next;
  var _position = ui.Offset(0.0, 0.0);
  var _isTracked = false;
  var _isDown = false;
  var _pointerIdentifier = 0;

  ui.PointerData _clone(
      ui.PointerData data,
      ui.PointerChange change,
      ui.Offset position,
      ui.Offset delta,
      int pointerIdentifier,
      Duration timeStamp) {
    return ui.PointerData(
        buttons: data.buttons,
        device: data.device,
        timeStamp: timeStamp,
        change: change,
        kind: data.kind,
        physicalDeltaX: delta.dx,
        physicalDeltaY: delta.dy,
        physicalX: position.dx,
        physicalY: position.dy,
        pointerIdentifier: pointerIdentifier,
        synthesized: data.synthesized);
  }

  ui.Offset _positionAt(Duration sampleTime) {
    // Use `next` position by default.
    var x = _next.physicalX;
    var y = _next.physicalY;

    // Resample if `next` time stamp is past `sampleTime`.
    if (_next.timeStamp > sampleTime && _next.timeStamp > _last.timeStamp) {
      final interval =
          (_next.timeStamp - _last.timeStamp).inMicroseconds.toDouble();
      final scalar =
          (sampleTime - _last.timeStamp).inMicroseconds.toDouble() / interval;
      x = _last.physicalX + (_next.physicalX - _last.physicalX) * scalar;
      y = _last.physicalY + (_next.physicalY - _last.physicalY) * scalar;
    }

    return ui.Offset(x, y);
  }

  void _processPointerData(Duration sampleTime) {
    for (var i = 0; i < _queuedData.length; i++) {
      final data = _queuedData[i];

      // Update both `last` and `next` pointer data if time stamp is older
      // or equal to `sampleTime`.
      if (data.timeStamp <= sampleTime || _last == null) {
        _last = data;
        _next = data;
        continue;
      }

      // Update only `next` pointer data if time stamp is more recent than
      // `sampleTime` and next data is not already more recent.
      if (_next.timeStamp < sampleTime) {
        _next = data;
      }
    }
  }

  List<ui.PointerData> _dequeueAndSamplePointerDataUntil(Duration sampleTime) {
    final dequeuedData = <ui.PointerData>[];

    while (_queuedData.isNotEmpty) {
      final data = _queuedData.first;

      // Potentially stop dequeuing data if more recent than `sampleTime`.
      if (data.timeStamp > sampleTime) {
        // Stop if change is not `up` or `remove`, which are allowed to be
        // processed early as this improves resampling of these changes,
        // which is important for fling animations.
        if (data.change != ui.PointerChange.up &&
            data.change != ui.PointerChange.remove) {
          break;
        }

        // Time stamp must match `next` data for early processing to be
        // allowed.
        if (data.timeStamp != _next.timeStamp) {
          break;
        }
      }

      final wasTracked = _isTracked;

      // Update `_Pointer` state.
      switch (data.change) {
        case ui.PointerChange.down:
        case ui.PointerChange.move:
          _isDown = true;
          _isTracked = true;
          break;
        case ui.PointerChange.up:
        case ui.PointerChange.cancel:
          _isDown = false;
          _isTracked = true;
          break;
        case ui.PointerChange.add:
        case ui.PointerChange.hover:
          _isTracked = true;
          break;
        case ui.PointerChange.remove:
          _isTracked = false;
          break;
      }

      // Position at `sampleTime`.
      var position = _positionAt(sampleTime);

      // Initialize `position` if we are starting to track this pointer.
      if (_isTracked && !wasTracked) {
        _position = position;
      }

      // Skip `move` and `hover` changes as they are automatically
      // generated when the position has changed.
      if (data.change != ui.PointerChange.move &&
          data.change != ui.PointerChange.hover) {
        dequeuedData.add(_clone(data, data.change, position,
            position - _position, data.pointerIdentifier, sampleTime));
        _position = position;
      }
      _pointerIdentifier = data.pointerIdentifier;

      _queuedData.removeAt(0);
    }

    return dequeuedData;
  }

  List<ui.PointerData> _samplePointerPositions(Duration sampleTime) {
    var sampledData = <ui.PointerData>[];

    // Position at `sampleTime`.
    var position = _positionAt(sampleTime);

    // Add `move` or `hover` data if position changed.
    if (position != _position) {
      sampledData.add(_clone(
          _next,
          _isDown ? ui.PointerChange.move : ui.PointerChange.hover,
          position,
          position - _position,
          _pointerIdentifier,
          sampleTime));
      _position = position;
    }

    return sampledData;
  }

  /// Enqueue pointer `data` for resampling.
  void addData(ui.PointerData data) {
    _queuedData.add(data);
  }

  /// Dequeue resampled pointer `data` for the specified `sampleTime`.
  List<ui.PointerData> sample(Duration sampleTime) {
    // Process data for `sampleTime`.
    _processPointerData(sampleTime);

    // Dequeue and sample pointer data until `sampleTime`.
    var data = _dequeueAndSamplePointerDataUntil(sampleTime);

    // Add resampled pointer location data if tracked.
    if (_isTracked) {
      data.addAll(_samplePointerPositions(sampleTime));
    }

    return data;
  }

  /// Returns `true` if a call to [dequeueAndSample] can return
  /// more data.
  bool hasPendingData() => _queuedData.isNotEmpty;

  /// Returns `true` if pointer is currently tracked.
  bool isTracked() => _isTracked;
}
