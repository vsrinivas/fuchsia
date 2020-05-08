// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:developer';
import 'dart:ui' as ui;

import 'package:fidl_fuchsia_ui_input/fidl_async.dart';
import 'package:fidl_fuchsia_ui_policy/fidl_async.dart';
import 'package:fidl_fuchsia_ui_views/fidl_async.dart';

import 'package:flutter/scheduler.dart';

/// The default sampling offset.
///
/// Sampling offset is relative to presentation time. If we produce frames
/// 16.667 ms before presentation and input rate is ~60hz, worst case latency
/// is 33.334 ms. This however assumes zero latency from the input driver.
/// 4.666 ms margin is added for this.
const _defaultSamplingOffset = Duration(milliseconds: -38);

class _Event {
  PointerEvent p;
  Flow flow1;
  Flow flow2;
  _Event(this.p, this.flow1, this.flow2);
}

class _DownPointer {
  _Event last;
  _Event next;
  _DownPointer(this.last, this.next);
}

/// Listens for pointer events through the updated API and injects them into Flutter input pipeline.
class PointerEventsListener2 extends PointerCaptureListener {
  // Holds the fidl binding to receive pointer events.
  final PointerCaptureListenerBinding _pointerCaptureListenerBinding =
      PointerCaptureListenerBinding();

  PointerEventsListener _pointerEventsListener;

  PointerEventsListener2({
    SchedulerBinding scheduler,
    ui.PointerDataPacketCallback callback,
    Duration samplingOffset,
  }) {
    _pointerEventsListener = PointerEventsListener(
        scheduler: scheduler,
        callback: callback,
        samplingOffset: samplingOffset);
  }

  /// Starts listening to pointer events. Also overrides the original
  /// [ui.window.onPointerDataPacket] callback to a NOP since we
  /// inject the pointer events received from the [Scenic] service.
  void listen(PointerCaptureListenerRegistryProxy registry, ViewRef viewRef) {
    _pointerEventsListener.setCallbackState();

    registry.registerListener(
        _pointerCaptureListenerBinding.wrap(this), viewRef);
  }

  /// |PointerCaptureListener|.
  @override
  Future<void> onPointerEvent(PointerEvent event) async {
    await _pointerEventsListener.onPointerEvent(event);
  }

  void stop() {
    _pointerEventsListener.stop();
    _pointerCaptureListenerBinding.close();
  }
}

/// [DEPRECATED] Use PointerEventsListener2 instead.
/// Listens for pointer events and injects them into Flutter input pipeline.
class PointerEventsListener extends PointerCaptureListenerHack {
  // Holds the fidl binding to receive pointer events.
  final PointerCaptureListenerHackBinding _pointerCaptureListenerBinding =
      PointerCaptureListenerHackBinding();

  // Holds the last [PointerEvent] mapped to its pointer id. This is used to
  // determine the correct [PointerDataPacket] to generate at boundary condition
  // of the screen rect.
  final _lastPointerEvent = <int, PointerEvent>{};

  // Flag to remember that a down event was seen before a move event.
  // TODO(sanjayc): Should really convert to a FSM for PointerEvent.
  final _downEvent = <int, bool>{};

  // Scheduler used for frame callbacks.
  SchedulerBinding _scheduler;

  // [onPointerDataCallback] used to dispatch pointer data callbacks.
  ui.PointerDataPacketCallback _callback;

  // Offset used for re-sampling of move events.
  final Duration _samplingOffset;

  final _queuedEvents = <_Event>[];
  var _frameCallbackScheduled = false;

  var _sampleTimeNs = 0;
  final _downPointers = <int, _DownPointer>{};

  PointerEventsListener({
    SchedulerBinding scheduler,
    ui.PointerDataPacketCallback callback,
    Duration samplingOffset,
  })  : _scheduler = scheduler,
        _samplingOffset = samplingOffset ?? _defaultSamplingOffset {
    _callback = callback;
  }

  /// Starts listening to pointer events. Also overrides the original
  /// [ui.window.onPointerDataPacket] callback to a NOP since we
  /// inject the pointer events received from the [Presentation] service.
  void listen(PresentationProxy presentation) {
    setCallbackState();

    presentation
        .capturePointerEventsHack(_pointerCaptureListenerBinding.wrap(this));
  }

  void setCallbackState() {
    _scheduler ??= SchedulerBinding.instance;
    _callback = ui.window.onPointerDataPacket;
    ui.window.onPointerDataPacket = (ui.PointerDataPacket packet) {};
  }

  /// Stops listening to pointer events. Also restores the
  /// [ui.window.onPointerDataPacket] callback.
  void stop() {
    if (_callback != null) {
      _cleanupPointerEvents();
      _pointerCaptureListenerBinding.close();

      // Restore the original pointer events callback on the window.
      ui.window.onPointerDataPacket = _callback;
      _callback = null;
      _lastPointerEvent.clear();
      _downEvent.clear();
    }
  }

  void _cleanupPointerEvents() {
    for (final lastEvent in _lastPointerEvent.values.toList()) {
      if (lastEvent.phase != PointerEventPhase.remove &&
          lastEvent.type != PointerEventType.mouse) {
        onPointerEvent(_clone(lastEvent, PointerEventPhase.remove, lastEvent.x,
            lastEvent.y, lastEvent.eventTime));
      }
    }
  }

  PointerEvent _clone(PointerEvent event, PointerEventPhase phase, double x,
      double y, int eventTime) {
    return PointerEvent(
        buttons: event.buttons,
        deviceId: event.deviceId,
        eventTime: eventTime,
        phase: phase,
        pointerId: event.pointerId,
        radiusMajor: event.radiusMajor,
        radiusMinor: event.radiusMinor,
        type: event.type,
        x: x,
        y: y);
  }

  /// |PointerCaptureListener|.
  @override
  Future<void> onPointerEvent(PointerEvent event) async {
    _onPointerEvent(event);
  }

  void _onPointerEvent(PointerEvent event) {
    if (_callback == null) {
      return;
    }

    final eventArguments = <String, int>{
      'eventTimeUs': event.eventTime ~/ 1000,
    };
    final flow1 = Flow.begin();
    Timeline.timeSync('PointerEventsListener.onPointerEvent', () {
      final flow2 = Flow.begin();
      Timeline.timeSync('PointerEventsListener.queueEvent', () {
        final frameTime =
            _scheduler.currentSystemFrameTimeStamp.inMicroseconds * 1000;
        // Sanity check event time by clamping to frameTime.
        final eventTime =
            event.eventTime < frameTime ? event.eventTime : frameTime;
        _queuedEvents.add(_Event(
            _clone(event, event.phase, event.x, event.y, eventTime),
            flow1,
            flow2));
      }, flow: flow2);
      _dispatchQueuedEvents();
    }, arguments: eventArguments, flow: flow1);
  }

  void _dispatchQueuedEvents() {
    Timeline.timeSync('PointerEventsListener.dispatchQueuedEvents', () {
      _consumePendingEvents();
      _updateDownPointers();
      _dispatchMoveChanges();

      // Schedule frame callback if down pointers exists or events are
      // still queued. We need the frame callback to determine sample
      // time. This however makes us produce frames whenever touch points
      // are present. Probably OK as we'll likely receive an update to
      // the touch point location each frame that will result in us
      // actually having to produce a frame.
      if (_queuedEvents.isNotEmpty || _downPointers.isNotEmpty) {
        _scheduleFrameCallback();
      }
    });
  }

  void _consumePendingEvents() {
    final packets = <ui.PointerData>[];

    while (_queuedEvents.isNotEmpty) {
      final event = _queuedEvents.first;

      // Dispatch non-touch changes directly.
      var dispatchEvent = true;

      if (event.p.type == PointerEventType.touch) {
        // Stop consuming touch events if more recent than current sample time.
        if (event.p.eventTime > _sampleTimeNs) {
          break;
        }

        switch (event.p.phase) {
          case PointerEventPhase.down:
            _downPointers[event.p.pointerId] = _DownPointer(event, event);
            break;
          case PointerEventPhase.move:
            // Create a _DownPointer event if it does not already exist, i.e.
            // user is touching screen on boot.
            if (!_downPointers.containsKey(event.p.pointerId)) {
              _downPointers[event.p.pointerId] = _DownPointer(event, event);
            }

            // Move changes will be re-sampled instead of dispatched directly.
            _downPointers[event.p.pointerId].next = event;
            dispatchEvent = false;
            break;
          case PointerEventPhase.up:
          case PointerEventPhase.cancel:
            _downPointers.remove(event.p.pointerId);
            break;
          case PointerEventPhase.add:
          case PointerEventPhase.remove:
          case PointerEventPhase.hover:
            break;
        }
      }

      if (dispatchEvent) {
        final eventArguments = <String, int>{
          'eventTimeUs': event.p.eventTime ~/ 1000,
        };
        Timeline.timeSync('PointerEventsListener.consumePendingEvent', () {
          final packet = _getPacket(event.p);
          if (packet != null) {
            packets.add(packet);
          }
        }, arguments: eventArguments, flow: Flow.end(event.flow1.id));
      }

      _queuedEvents.removeAt(0);
    }

    if (packets.isNotEmpty) {
      Timeline.timeSync('PointerEventsListener.dispatchPackets', () {
        _callback(ui.PointerDataPacket(data: packets));
      });
    }
  }

  void _updateDownPointers() {
    // Update [_downPointers] by examining queued changes.
    for (var event in _queuedEvents) {
      final p = _downPointers[event.p.pointerId];
      if (p != null) {
        switch (event.p.phase) {
          case PointerEventPhase.down:
          case PointerEventPhase.move:
          case PointerEventPhase.cancel:
          case PointerEventPhase.up:
            // Update next event if not already passed sample time.
            if (p.next.p.eventTime < _sampleTimeNs) {
              _downPointers[event.p.pointerId].next = event;
            }
            break;
          case PointerEventPhase.add:
          case PointerEventPhase.remove:
          case PointerEventPhase.hover:
            break;
        }
      }
    }
  }

  void _dispatchMoveChanges() {
    final packets = <ui.PointerData>[];

    // Add move changes for [_downPointers].
    for (var v in _downPointers.values) {
      var x = v.next.p.x;
      var y = v.next.p.y;
      var eventTime = v.next.p.eventTime;
      // Re-sample if next time stamp is past sample time.
      if (v.next.p.eventTime > _sampleTimeNs &&
          v.next.p.eventTime > v.last.p.eventTime) {
        var resampleArguments = <String, int>{
          'lastEventTimeUs': v.last.p.eventTime ~/ 1000,
          'nextEventTimeUs': v.next.p.eventTime ~/ 1000
        };
        Timeline.timeSync('PointerEventsListener.resampledEvent', () {
          final interval = (v.next.p.eventTime - v.last.p.eventTime).toDouble();
          final scalar =
              (_sampleTimeNs - v.last.p.eventTime).toDouble() / interval;
          x = v.last.p.x + (v.next.p.x - v.last.p.x) * scalar;
          y = v.last.p.y + (v.next.p.y - v.last.p.y) * scalar;
          eventTime = _sampleTimeNs;
        }, arguments: resampleArguments);
      }

      // Add move change if time stamp is greater than last event.
      if (eventTime > v.last.p.eventTime) {
        final event = _clone(v.next.p, PointerEventPhase.move, x, y, eventTime);
        final eventArguments = <String, int>{
          'eventTimeUs': eventTime ~/ 1000,
        };
        Timeline.timeSync('PointerEventsListener.addMoveEvent', () {
          final packet = _getPacket(event);
          if (packet != null) {
            packets.add(packet);
          }
        }, arguments: eventArguments, flow: Flow.end(v.last.flow2.id));

        Timeline.timeSync('PointerEventsListener.updateLastEvent', () {
          v.last.p = event;
          v.last.flow2 = v.next.flow2;
        }, flow: Flow.end(v.next.flow1.id));
      }
    }

    if (packets.isNotEmpty) {
      Timeline.timeSync('PointerEventsListener.dispatchPackets', () {
        _callback(ui.PointerDataPacket(data: packets));
      });
    }
  }

  void _scheduleFrameCallback() {
    if (_frameCallbackScheduled) {
      return;
    }
    _frameCallbackScheduled = true;
    _scheduler.scheduleFrameCallback((_) {
      _frameCallbackScheduled = false;
      _sampleTimeNs = (_scheduler.currentSystemFrameTimeStamp + _samplingOffset)
              .inMicroseconds *
          1000;
      var frameArguments = <String, int>{
        'frameTimeUs': _scheduler.currentSystemFrameTimeStamp.inMicroseconds,
      };
      if (_queuedEvents.isNotEmpty) {
        final lastEventTime = _queuedEvents.last.p.eventTime;
        frameArguments['lastEventTimeUs'] = lastEventTime ~/ 1000;
        frameArguments['eventTimeMarginUs'] =
            (lastEventTime - _sampleTimeNs) ~/ 1000;
      }
      Timeline.timeSync(
        'PointerEventsListener.onFrameCallback',
        _dispatchQueuedEvents,
        arguments: frameArguments,
      );
    });
    Timeline.timeSync('PointerEventsListener.scheduleFrameCallback', () {
      _scheduler.scheduleFrame();
    });
  }

  ui.PointerChange _changeFromPointerEvent(PointerEvent event) {
    final lastEvent = _lastPointerEvent[event.pointerId] ?? event;

    switch (event.phase) {
      case PointerEventPhase.add:
        return ui.PointerChange.add;
      case PointerEventPhase.hover:
        return ui.PointerChange.hover;
      case PointerEventPhase.down:
        _downEvent[event.pointerId] = true;
        return ui.PointerChange.down;
      case PointerEventPhase.move:
        // If move is the first event, convert to `add` event. Otherwise,
        // flutter pointer state machine throws an exception.
        if (event.type != PointerEventType.mouse &&
            _lastPointerEvent[event.pointerId] == null) {
          return ui.PointerChange.add;
        }

        // If move event was seen before down event, convert to `down` event.
        if (event.type != PointerEventType.mouse &&
            _downEvent[event.pointerId] != true) {
          _downEvent[event.pointerId] = true;
          return ui.PointerChange.down;
        }

        // For mouse, return a hover event if no buttons were pressed.
        if (event.type == PointerEventType.mouse && event.buttons == 0) {
          return ui.PointerChange.hover;
        }

        // Check if this is a boundary condition and convert to up/down event.
        if (lastEvent?.phase == PointerEventPhase.move) {
          if (_outside(lastEvent) && _inside(event)) {
            return ui.PointerChange.down;
          }
          if (_inside(lastEvent) && _outside(event)) {
            return ui.PointerChange.cancel;
          }
        }

        return ui.PointerChange.move;
      case PointerEventPhase.up:
        _downEvent[event.pointerId] = false;
        return ui.PointerChange.up;
      case PointerEventPhase.remove:
        return ui.PointerChange.remove;
      case PointerEventPhase.cancel:
      default:
        return ui.PointerChange.cancel;
    }
  }

  ui.PointerDeviceKind _kindFromPointerEvent(PointerEvent event) {
    switch (event.type) {
      case PointerEventType.mouse:
        return ui.PointerDeviceKind.mouse;
      case PointerEventType.stylus:
        return ui.PointerDeviceKind.stylus;
      case PointerEventType.invertedStylus:
        return ui.PointerDeviceKind.invertedStylus;
      case PointerEventType.touch:
      default:
        return ui.PointerDeviceKind.touch;
    }
  }

  ui.PointerData _getPacket(PointerEvent event) {
    final lastEvent = _lastPointerEvent[event.pointerId];

    // Only allow add and remove pointer events from outside the window bounds.
    // For other events, we drop them if the last two were outside the window
    // bounds. If any of current event or last event lies inside the window,
    // we generate a synthetic down or up event.
    if (event.phase != PointerEventPhase.add &&
        event.phase != PointerEventPhase.remove &&
        _outside(event) &&
        _outside(lastEvent ?? event)) {
      _lastPointerEvent[event.pointerId] = event;
      return null;
    }

    // Calculate the offset between two events.
    final delta = lastEvent != null
        ? ui.Offset(event.x - lastEvent.x, event.y - lastEvent.y)
        : ui.Offset(0, 0);

    // Convert from PointerEvent to PointerData.
    final data = ui.PointerData(
      buttons: event.buttons,
      device: event.pointerId,
      timeStamp: Duration(microseconds: event.eventTime ~/ 1000),
      change: _changeFromPointerEvent(event),
      kind: _kindFromPointerEvent(event),
      physicalDeltaX: delta.dx * ui.window.devicePixelRatio,
      physicalDeltaY: delta.dy * ui.window.devicePixelRatio,
      physicalX: event.x * ui.window.devicePixelRatio,
      physicalY: event.y * ui.window.devicePixelRatio,
      pointerIdentifier: event.pointerId,
      synthesized: false,
    );

    // Remember this event for checking boundary condition on the next event.
    _lastPointerEvent[event.pointerId] = event;

    return data;
  }

  bool _inside(PointerEvent event) {
    return event != null &&
        event.x * ui.window.devicePixelRatio >= 0 &&
        event.x * ui.window.devicePixelRatio < ui.window.physicalSize.width &&
        event.y * ui.window.devicePixelRatio >= 0 &&
        event.y * ui.window.devicePixelRatio < ui.window.physicalSize.height;
  }

  bool _outside(PointerEvent event) => !_inside(event);
}
