import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter/widgets.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:lib.widgets/utils.dart';

/// Key/value store used to display debug state using a [DebugStatusWidget].
class DebugStatus extends ChangeNotifier {
  final ChangeNotifierMap<String, String> _output =
      ChangeNotifierMap<String, String>();
  final ChangeNotifierMap<String, Timer> _timers =
      ChangeNotifierMap<String, Timer>();

  DebugStatus() {
    _output.addListener(notifyListeners);
    _timers.addListener(notifyListeners);
  }

  /// Writes
  void write(String key, String value) {
    log.fine('$key: $value');

    if (_output[key] != value) {
      _timers[key] = Timer(Duration(seconds: 5), () {
        _timers[key] = null;
      });
      _output[key] = value;
    }
  }

  void timestamp(String key) {
    write(key, '${DateTime.now()}');
  }
}

class DebugStatusWidget extends StatelessWidget {
  static final _unchangedStyle = TextStyle(color: Colors.black, fontSize: 10.0);
  static final _changedStyle =
      _unchangedStyle.merge(TextStyle(color: Colors.red));

  final DebugStatus debugStatus;

  const DebugStatusWidget(this.debugStatus);

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: _debugText(),
    );
  }

  List<Widget> _debugText() {
    final output = <Widget>[];

    for (String key in debugStatus._output.keys.toList()..sort()) {
      output.add(debugStatus._timers[key] != null
          ? _changed(key, debugStatus._output[key])
          : _unchanged(key, debugStatus._output[key]));
    }
    return output;
  }

  static Widget _changed(String key, String text) => Text(
        '$key: $text',
        style: _changedStyle,
      );
  static Widget _unchanged(String key, String text) => Text(
        '$key: $text',
        style: _unchangedStyle,
      );
}
