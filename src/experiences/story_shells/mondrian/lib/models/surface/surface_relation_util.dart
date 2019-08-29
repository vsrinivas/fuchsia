// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_fuchsia_modular/fidl_async.dart';

// Contains methods for converting between SurfaceRelation and strings.
class SurfaceRelationUtil {
  static Map<String, String> toMap(SurfaceRelation relation) {
    String dependency = relation.dependency.toString();
    String arrangement = relation.arrangement.toString();
    double emphasis = relation.emphasis;
    return {
      'arrangement': arrangement,
      'dependency': dependency,
      'emphasis': emphasis.toString(),
    };
  }

  static SurfaceRelation decode(Map<String, String> encoded) {
    return SurfaceRelation(
      emphasis: double.parse(encoded['emphasis']),
      arrangement: arrangementFromString(encoded['arrangement']),
      dependency: dependencyFromString(encoded['dependency']),
    );
  }

  static SurfaceArrangement arrangementFromString(String arrangement) {
    if (arrangement == SurfaceArrangement.copresent.toString()) {
      return SurfaceArrangement.copresent;
    } else if (arrangement == SurfaceArrangement.sequential.toString()) {
      return SurfaceArrangement.sequential;
    } else if (arrangement == SurfaceArrangement.ontop.toString()) {
      return SurfaceArrangement.ontop;
    } else {
      return SurfaceArrangement.none;
    }
  }

  static SurfaceDependency dependencyFromString(String dependency) {
    if (dependency == SurfaceDependency.dependent.toString()) {
      return SurfaceDependency.dependent;
    } else {
      return SurfaceDependency.none;
    }
  }
}
