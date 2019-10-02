// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:ui';

import 'package:fidl_fuchsia_ui_remotewidgets/fidl_async.dart';
import 'package:flutter/material.dart';
import 'package:internationalization/strings.dart';
import 'package:quickui/uistream.dart';

import '../../models/status_model.dart';
import 'status_graph.dart';
import 'status_progress.dart';

const kPadding = 12.0;
const kTitleWidth = 100.0;
const kRowHeight = 28.0;
const kItemHeight = 16.0;
const kIconHeight = 18.0;
const kProgressBarWidth = 87.0;
const kStatusBackgroundColor = Color(0xFF0C0C0C);
const kStatusBorderColor = Color(0xFF262626);
const kDefaultTextStyle = TextStyle(
  fontFamily: 'Roboto Mono',
  fontSize: 11,
  letterSpacing: 0,
  fontWeight: FontWeight.w400,
  color: Colors.white,
);

class Status extends StatelessWidget {
  final StatusModel model;

  const Status({@required this.model});

  @override
  Widget build(BuildContext context) {
    return SingleChildScrollView(
      padding: EdgeInsets.all(kPadding),
      child: Column(
        children: <Widget>[
          _ManualStatusEntry(model),
          _StatusEntry(model.brightness),
          _StatusEntry(model.volume),
          _StatusEntry(model.battery),
          _StatusEntry(model.memory),
          _StatusEntry(model.weather),
        ],
      ),
    );
  }
}

class _StatusEntry extends StatelessWidget {
  final UiStream uiStream;

  _StatusEntry(this.uiStream) {
    uiStream.listen();
  }

  @override
  Widget build(BuildContext context) {
    return StreamBuilder<Spec>(
      stream: uiStream.stream,
      initialData: uiStream.spec,
      builder: (context, snapshot) {
        if (!snapshot.hasData) {
          return Offstage();
        }
        final spec = snapshot.data;
        final widgets = _buildFromSpec(spec, uiStream.update);
        return Container(
          constraints: BoxConstraints(minHeight: kRowHeight),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.end,
            children: widgets,
          ),
        );
      },
    );
  }

// Returns a list of [Row] widgets from the given [Spec].
// The first row would include title [Text] and may be followed by widgets
// associated with the [Value] type. A [GridValue] widget is returned in its
// own row.
  List<Widget> _buildFromSpec(Spec spec, void Function(Value) update) {
    // Split the values into lists separated by [GridValue].
    List<Widget> result = <Widget>[];
    List<Widget> widgets = <Widget>[];

    for (final group in spec.groups) {
      for (final value in group.values) {
        final widget = _buildFromValue(value, update);
        if (value.$tag == ValueTag.grid) {
          if (result.isEmpty) {
            result.add(_buildTitleRow(group.title, widgets.toList()));
          } else {
            result.add(_buildValueRow(widgets.toList()));
          }
          widgets.clear();
        }
        widgets.add(widget);
      }

      if (widgets.isNotEmpty) {
        if (result.isEmpty) {
          result.add(_buildTitleRow(group.title, widgets.toList()));
        } else {
          result.add(_buildValueRow(widgets.toList()));
        }
      }
    }
    return result;
  }

  Widget _buildFromValue(Value value, void Function(Value) update) {
    if (value.$tag == ValueTag.button) {
      return _buildButton(value.button.label, () => update(value));
    }
    if (value.$tag == ValueTag.text) {
      return Text(value.text.text.toUpperCase());
    }
    if (value.$tag == ValueTag.progress) {
      return SizedBox(
        height: kItemHeight,
        width: kProgressBarWidth,
        child: ProgressBar(
          value: value.progress.value,
          onChange: (v) => update(Value.withProgress(
              ProgressValue(value: v, action: value.progress.action))),
        ),
      );
    }

    if (value.$tag == ValueTag.grid) {
      int columns = value.grid.columns;
      int rows = value.grid.values.length ~/ columns;
      return Container(
        padding: EdgeInsets.only(left: 8),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: List<Widget>.generate(rows, (row) {
            return Padding(
              padding: EdgeInsets.symmetric(vertical: 4),
              child: Row(
                mainAxisAlignment: MainAxisAlignment.spaceBetween,
                children: List<Widget>.generate(value.grid.columns, (column) {
                  final index = row * value.grid.columns + column;
                  return Expanded(
                    flex: column == 0 ? 2 : 1,
                    child: Text(
                      value.grid.values[index].text,
                      textAlign: column == 0 ? TextAlign.start : TextAlign.end,
                    ),
                  );
                }),
              ),
            );
          }),
        ),
      );
    }
    if (value.$tag == ValueTag.icon) {
      return GestureDetector(
        child: Icon(
          IconData(
            value.icon.codePoint,
            fontFamily: value.icon.fontFamily ?? 'MaterialIcons',
          ),
          size: kIconHeight,
        ),
        onTap: () => update(value),
      );
    }
    if (value.$tag == ValueTag.graph) {
      return QuickGraph(value: value.graph.value, step: value.graph.step);
    }
    return Offstage();
  }
}

Widget _buildValueRow(List<Widget> children) {
  return Wrap(
    children: children,
    alignment: WrapAlignment.end,
    spacing: kPadding,
    runSpacing: kPadding,
  );
}

Widget _buildTitleRow(String title, List<Widget> children) {
  return Row(
    crossAxisAlignment: CrossAxisAlignment.start,
    textBaseline: TextBaseline.alphabetic,
    children: <Widget>[
      Container(
        width: kTitleWidth,
        child: Text(title.toUpperCase()),
      ),
      Expanded(
        child: _buildValueRow(children),
      ),
    ],
  );
}

Widget _buildButton(String label, void Function() onTap) {
  return GestureDetector(
    onTap: onTap,
    child: Container(
      height: kItemHeight,
      color: Colors.white,
      padding: EdgeInsets.symmetric(vertical: 0, horizontal: 2),
      child: Text(
        label.toUpperCase(),
        style: TextStyle(
          color: Colors.black,
          fontWeight: FontWeight.w400,
        ),
      ),
    ),
  );
}

class _ManualStatusEntry extends StatelessWidget {
  final StatusModel model;

  const _ManualStatusEntry(this.model);

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: EdgeInsets.only(bottom: kPadding),
      height: kRowHeight,
      child: Row(
        children: <Widget>[
          _buildButton(Strings.restart, model.restartDevice),
          Padding(padding: EdgeInsets.only(right: kPadding)),
          _buildButton(Strings.shutdown, model.shutdownDevice),
          Spacer(),
          _buildButton(Strings.settings, model.launchSettings),
        ],
      ),
    );
  }
}
