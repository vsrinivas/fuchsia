// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'package:tools.dart-strict-deps.dart_strict_deps_proto/protos/models.pb.dart';

/// Returns BuildInfo from json representation
BuildInfo buildInfoFromJson(String contents) {
  final jsonList = json.decode(contents).toList();
  final buildTargets = jsonList
      .map((buildTargetJson) =>
          BuildTarget()..mergeFromProto3Json(buildTargetJson))
      .toList(growable: false)
      .cast<BuildTarget>();
  return BuildInfo()..buildTargets.addAll(buildTargets);
}
