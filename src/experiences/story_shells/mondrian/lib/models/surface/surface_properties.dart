// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_fuchsia_modular/fidl_async.dart';

/// Inherent properties of a surface
class SurfaceProperties {
  /// Const constructor
  SurfaceProperties({this.containerLabel, this.source});

  SurfaceProperties.fromJson(Map<String, dynamic> json) {
    containerLabel = json['containerLabel'];
    containerMembership = json['containerMembership'];
    source = moduleSourceFromString(json['source']);
  }

  /// Belongs to a container with label containerLabel
  String containerLabel;

  /// List of the containers this Surface is a member of
  /// (To be able to support container-to-container transitions)
  /// The container this Surface is currently participating in is
  /// end of list. If this Surface is focused, that is the container that
  /// will be laid out.
  List<String> containerMembership;

  /// Was the module producing this surface launched from inside the current
  /// story - e.g. by a parent module in the story, or externally e.g. via a
  /// suggestion
  ModuleSource source;

  Map<String, dynamic> toJson() => {
        'containerLabel': containerLabel,
        'containerMembership': containerMembership,
        'source': source.toString(),
      };

  ModuleSource moduleSourceFromString(String str) {
    if (str == ModuleSource.internal.toString()) {
      return ModuleSource.internal;
    } else if (str == ModuleSource.external.toString()) {
      return ModuleSource.external;
    } else if (str == 'null') {
      return null;
    } else {
      throw ArgumentError.value(str);
    }
  }
}
