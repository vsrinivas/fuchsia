// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:math' as math;

import 'package:fidl_fuchsia_modular/fidl_async.dart';
import 'package:flutter/material.dart';
import 'package:flutter/widgets.dart';
import 'package:fuchsia_scenic_flutter/child_view.dart' show ChildView;
import 'package:fuchsia_logger/logger.dart';
import 'package:lib.widgets/model.dart';

import '../models/surface/surface.dart';
import '../models/surface/surface_graph.dart';
import 'isometric_widget.dart';

/// Printable names for relation arrangement
const Map<SurfaceArrangement, String> relName =
    <SurfaceArrangement, String>{
  SurfaceArrangement.none: 'no opinion',
  SurfaceArrangement.copresent: 'co-present',
};

/// Printable names for relation dependency
const Map<SurfaceDependency, String> depName =
    <SurfaceDependency, String>{
  SurfaceDependency.dependent: 'dependent',
  SurfaceDependency.none: 'independent',
};

/// Show overview of all currently active surfaces in a story
/// and their relationships
class Overview extends StatelessWidget {
  /// Constructor
  const Overview({Key key}) : super(key: key);

  /// Build the ListView of Surface views in SurfaceGraph
  Widget buildGraphList(BoxConstraints constraints, SurfaceGraph graph) {
    return ListView.builder(
      itemCount: graph.focusStack.toList().length,
      scrollDirection: Axis.vertical,
      itemExtent: constraints.maxHeight / 3.5,
      itemBuilder: (BuildContext context, int index) {
        Surface s = graph.focusStack.toList().reversed.elementAt(index);
        String arrangement = relName[s.relation.arrangement] ?? 'unknown';
        String dependency = depName[s.relation.dependency] ?? 'unknown';
        return Row(
          children: <Widget>[
            Flexible(
              flex: 1,
              child: Center(
                child: index < graph.focusStack.length - 1
                    ? Text('Presentation: $arrangement'
                        '\nDependency: $dependency'
                        '\nEmphasis: ${s.relation.emphasis}')
                    : null,
              ),
            ),
            Flexible(
              flex: 2,
              child: Container(
                child: Center(
                  child: IsoMetric(
                    child: ChildView(
                        connection: s.connection, hitTestable: false),
                  ),
                ),
              ),
            ),
          ],
        );
      },
    );
  }

  @override
  Widget build(BuildContext context) => LayoutBuilder(
        builder: (BuildContext context, BoxConstraints constraints) {
          return Container(
            alignment: FractionalOffset.center,
            width: math.min(constraints.maxWidth, constraints.maxHeight),
            height: constraints.maxHeight,
            padding: EdgeInsets.symmetric(horizontal: 44.0),
            child: Scrollbar(
              child: ScopedModelDescendant<SurfaceGraph>(
                builder:
                    (BuildContext context, Widget child, SurfaceGraph graph) {
                  if (graph.focusStack.isEmpty) {
                    log.warning('focusedSurfaceHistory is empty');
                    return Container();
                  }
                  return buildGraphList(constraints, graph);
                },
              ),
            ),
          );
        },
      );
}
