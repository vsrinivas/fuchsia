// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

/// Builds grid to visualize a table of data.
class StatusGridVisualizer extends StatelessWidget {
  // Determines style of text in visualiation.
  final TextStyle textStyle;
  // Title of visualization displayed above grid
  final Text title;
  // Height/spacing at title of visualization
  final double titleHeight;
  // Model to manage data for StatusGridVisualizer.
  final StatusGridVisualizerModel model;

  const StatusGridVisualizer({
    @required this.model,
    @required this.textStyle,
    @required this.title,
    @required this.titleHeight,
  });

  @override
  Widget build(BuildContext context) {
    return AnimatedBuilder(
        animation: model,
        builder: (BuildContext context, Widget child) {
          return Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              SizedBox(
                height: titleHeight / 2,
              ),
              title,
              _buildGrid()
            ],
          );
        });
  }

  // Packages grid with provided title
  Widget _buildGrid() {
    return Container(
        height: model.gridHeight,
        alignment: Alignment.centerRight,
        child: Flex(
          crossAxisAlignment: CrossAxisAlignment.end,
          direction: Axis.horizontal,
          children: _packageGrid(),
        ));
  }

  // Packages grid with appropriate spacing between columns
  List<Widget> _packageGrid() {
    List<Widget> columns = List(model.gridColumns * 2 + 2);
    List<Widget> dataColumns = _packageValues();
    int dataIndex = 0;
    columns[0] = SizedBox.shrink();
    columns[1] = _packageHeaders();
    columns[2] = SizedBox(
      width: model.gridDataOffset,
    );
    for (int x = 3; x < columns.length; x++) {
      if (x % 2 != 0) {
        columns[x] = dataColumns[dataIndex];
        dataIndex++;
      } else {
        columns[x] = SizedBox(
          width: model.gridColumnSpace,
        );
      }
    }
    return columns;
  }

  // Returns title column, which is first column in grid
  Text _packageHeaders() {
    List<String> headerData = model.gridHeaders.split(',');
    String headerString = headerData.map((item) => '  $item\n').join();
    String headerTitleString = model.gridHeaderTitle;
    return Text(
      '$headerTitleString\n$headerString',
      style: textStyle,
    );
  }

  // Returns data column title at index columnIndex
  String _packageTitle(int columnIndex) {
    List<String> titleData = model.gridTitles.split(',');
    String title = titleData[columnIndex];
    return '$title\n';
  }

  // Returns concatenated string of data values belonging within index columnIndex
  String _packageColumn(int columnIndex) {
    List<String> valueData = model.gridValues.split(',');
    List<String> columnData = valueData
        .skip(columnIndex * model.gridRows)
        .take(model.gridRows)
        .toList();
    String columnDataString = columnData.map((item) => '$item\n').join();
    String titleDataString = _packageTitle(columnIndex);
    return titleDataString + columnDataString;
  }

  // Returns list of packaged data columns
  List<Widget> _packageValues() {
    List<Widget> columns = List(model.gridColumns);
    for (int x = 0; x < model.gridColumns; x++) {
      columns[x] = Text(
        _packageColumn(x),
        style: textStyle,
      );
    }
    return columns;
  }
}

class StatusGridVisualizerModel extends ChangeNotifier {
  // Headers indicative of where data was pulled + lead each data row
  final String _gridHeaders;
  // Data values to be mapped in grid
  String _gridValues;
  // Descriptive titles that describe what data is being displayed
  // Title at top of each data column; Default: PID# CPU% MEM%
  final String _gridTitles;
  // Number of data column; Default: 3
  final int _gridColumns;
  // Number of data rows; Default: 3
  final int _gridRows;
  // Descriptive title of gridTitles; Default: 'Name'
  final String _gridHeaderTitle;
  // Offset of first column (title column) from left
  final double _gridIndent;
  // Offset between first column (title column) and consecutive data columns
  final double _gridDataOffset;
  // Offset between consecutive data columns
  final double _gridColumnSpace;
  // Height of grid
  final double _gridHeight;

  set gridValues(String updatedGridValues) {
    _gridValues = updatedGridValues;
    notifyListeners();
  }

  String get gridHeaders => _gridHeaders;

  String get gridValues => _gridValues;

  String get gridTitles => _gridTitles;

  int get gridColumns => _gridColumns;

  int get gridRows => _gridRows;

  String get gridHeaderTitle => _gridHeaderTitle;

  double get gridIndent => _gridIndent;

  double get gridDataOffset => _gridDataOffset;

  double get gridColumnSpace => _gridColumnSpace;

  double get gridHeight => _gridHeight;

  StatusGridVisualizerModel({
    String gridHeaders = 'IDE,Chrome,Music',
    String gridValues = '6007,9646,5782,2.49,1.00,0.50,1.56,3.08,0.46',
    String gridTitles = 'PID#,CPU%,MEM%',
    int gridColumns = 3,
    int gridRows = 3,
    String gridHeaderTitle = 'Name',
    double gridIndent = 17.0,
    double gridDataOffset = 148.0,
    double gridColumnSpace = 30.0,
    double gridHeight = 56.0,
  })  : _gridHeaders = gridHeaders,
        _gridValues = gridValues,
        _gridTitles = gridTitles,
        _gridColumns = gridColumns,
        _gridRows = gridRows,
        _gridHeaderTitle = gridHeaderTitle,
        _gridIndent = gridIndent,
        _gridDataOffset = gridDataOffset,
        _gridColumnSpace = gridColumnSpace,
        _gridHeight = gridHeight;
}
