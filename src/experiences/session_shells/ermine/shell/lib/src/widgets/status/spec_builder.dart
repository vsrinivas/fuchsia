// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_fuchsia_ui_remotewidgets/fidl_async.dart';
import 'package:flutter/material.dart';

import 'status.dart';
import 'status_button.dart';
import 'status_graph.dart';
import 'status_progress.dart';

/// Returns a [Widget] built from the given [Spec].
///
/// The first row would include title [Text] and may be followed by widgets
/// associated with the [Value] type. A [GridValue] widget is returned in its
/// own row.
Widget buildFromSpec(Spec spec, void Function(Value) update) {
  // Split the values into lists separated by [GridValue].
  List<Widget> result = <Widget>[];
  for (final group in spec.groups) {
    // Handle group with no values, but has a title.
    if (group.values.isEmpty && group.title.isNotEmpty) {
      result.add(_buildTitleRow(group.title, [], group.icon));
      continue;
    }

    // Split values in group by GridValue. Grid is on a row by itself.
    final List<List<Value>> values = [];
    for (final value in group.values) {
      if (values.isEmpty ||
          value.$tag == ValueTag.grid ||
          values.last.last.$tag == ValueTag.grid) {
        values.add([value]);
      } else {
        values.last.add(value);
      }
    }

    for (final groupedValues in values) {
      // Convert [Value] to [Widget]s.
      final widgets =
          groupedValues.map((value) => _buildFromValue(value, update)).toList();
      // Create a title row for first set of values and value row for the rest.
      if (groupedValues == values.first && group.title.isNotEmpty) {
        // For grid, show title and grid in separate rows.
        if (groupedValues.first.$tag == ValueTag.grid) {
          result
            ..add(_buildTitleRow(group.title, [], group.icon))
            ..add(_buildValueRow(widgets));
        } else {
          result.add(_buildTitleRow(group.title, widgets, group.icon));
        }
      } else {
        result.add(_buildValueRow(widgets));
      }
    }
  }

  return Container(
    constraints: BoxConstraints(minHeight: kRowHeight),
    child: Column(
      crossAxisAlignment: CrossAxisAlignment.end,
      children: result,
    ),
  );
}

Widget _buildFromValue(Value value, void Function(Value) update) {
  if (value.$tag == ValueTag.button) {
    return StatusButton(value.button.label, () => update(value));
  }
  if (value.$tag == ValueTag.text) {
    final text = Text(value.text.text.toUpperCase());
    return value.text.action > 0
        ? GestureDetector(
            onTap: () => update(value),
            child: text,
          )
        : text;
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
              // mainAxisAlignment: MainAxisAlignment.spaceEvenly,
              children: List<Widget>.generate(value.grid.columns, (column) {
                final index = row * value.grid.columns + column;
                Value textValue = Value.withText(value.grid.values[index]);
                final text = Text(
                  textValue.text.text,
                  textAlign: column == 0 ? TextAlign.start : TextAlign.end,
                );
                return Expanded(
                  flex: column == 0 ? 3 : 2,
                  child: textValue.text.action > 0
                      ? GestureDetector(
                          onTap: () => update(textValue),
                          child: text,
                        )
                      : text,
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

Widget _buildValueRow(List<Widget> children) {
  return Wrap(
    children: children,
    alignment: WrapAlignment.end,
    spacing: kPadding,
    runSpacing: kPadding,
  );
}

Widget _buildTitleRow(String title, List<Widget> children, IconValue icon) {
  return Row(
    crossAxisAlignment: CrossAxisAlignment.start,
    textBaseline: TextBaseline.alphabetic,
    children: <Widget>[
      if (icon != null)
        Container(
          padding: EdgeInsets.only(right: 8),
          alignment: Alignment.centerLeft,
          child: Icon(
            IconData(
              icon.codePoint,
              fontFamily: icon.fontFamily ?? 'MaterialIcons',
            ),
            size: kIconHeight,
          ),
        ),
      Container(
        padding: EdgeInsets.only(right: 32),
        alignment: Alignment.centerLeft,
        child: Text(title.toUpperCase()),
      ),
      Expanded(
        child: _buildValueRow(children),
      ),
    ],
  );
}
