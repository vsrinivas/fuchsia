// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:fidl_fuchsia_ui_remotewidgets/fidl_async.dart';
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
          _ManualStatusEntry(),
          _StatusEntry(model.brightness),
          _StatusEntry(model.memory),
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

    Widget titleRow(String title, List<Widget> children) {
      return Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        textBaseline: TextBaseline.alphabetic,
        children: <Widget>[
          Container(
            width: kTitleWidth,
            child: Text(title.toUpperCase()),
          ),
          Expanded(
            child: Wrap(
              children: children,
              crossAxisAlignment: WrapCrossAlignment.center,
              alignment: WrapAlignment.end,
              spacing: kPadding,
              runSpacing: kPadding,
            ),
          ),
        ],
      );
    }

    Widget valueRow(List<Widget> children) {
      return Wrap(
        children: children,
        crossAxisAlignment: WrapCrossAlignment.center,
        alignment: WrapAlignment.end,
        spacing: kPadding,
        runSpacing: kPadding,
      );
    }

    for (final group in spec.groups) {
      for (final value in group.values) {
        final widget = _buildFromValue(value, update);
        if (value is GridValue) {
          if (result.isEmpty) {
            result.add(titleRow(group.title, widgets.toList()));
          } else {
            result.add(valueRow(widgets.toList()));
          }
          widgets.clear();
          result.add(widget);
        } else {
          widgets.add(widget);
        }
      }
      if (widgets.isNotEmpty) {
        if (result.isEmpty) {
          result.add(titleRow(group.title, widgets.toList()));
        } else {
          result.add(valueRow(widgets.toList()));
        }
      }
    }
    return result;
  }

  Widget _buildFromValue(Value value, void Function(Value) update) {
    if (value.$tag == ValueTag.button) {
      return GestureDetector(
        onTap: () => update(value),
        child: Container(
          height: kItemHeight,
          color: Colors.white,
          padding: EdgeInsets.symmetric(vertical: 0, horizontal: 2),
          child: Text(
            value.button.label.toUpperCase(),
            style: TextStyle(
              color: Colors.black,
              fontWeight: FontWeight.w400,
            ),
          ),
        ),
      );
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
      return Padding(
        padding: EdgeInsets.symmetric(vertical: 12),
        child: GridView.count(
          shrinkWrap: true,
          childAspectRatio: 4,
          physics: NeverScrollableScrollPhysics(),
          crossAxisCount: value.grid.columns,
          children: value.grid.values.map((v) => Text(v.text)).toList(),
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

class _ManualStatusEntry extends StatelessWidget {
  @override
  Widget build(BuildContext context) {
    return _buildSystemButtons();
  }

  Widget _buildSystemButtons() {
    List<Widget> buttons = <Widget>[];
    Widget restart = _buildButton('Restart', null);
    Widget shutdown = _buildButton('Shutdown', null);
    Widget settings = _buildButton('Settings', null);
    buttons
      ..add(restart)
      ..add(shutdown)
      ..add(SizedBox(width: 150))
      ..add(settings);
    return Container(
        constraints: BoxConstraints(minHeight: kRowHeight),
        child: Wrap(
          children: buttons,
          crossAxisAlignment: WrapCrossAlignment.center,
          alignment: WrapAlignment.end,
          spacing: kPadding,
          runSpacing: kPadding,
        ));
  }

  Widget _buildButton(String label, void Function() onTap) {
    return GestureDetector(
      onTap: () => onTap,
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
}
