// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:fuchsia_scenic_flutter/child_view_connection.dart';

/// Defines a class to represent a story in ermine.
class ErmineStory {
  final String id;
  final ValueChanged<ErmineStory> onDelete;
  final ValueChanged<ErmineStory> onChange;

  ErmineStory({
    this.id,
    String name,
    ChildViewConnection childViewConnection,
    this.onDelete,
    this.onChange,
  })  : nameNotifier = ValueNotifier(name),
        childViewConnectionNotifier = ValueNotifier(childViewConnection);

  final ValueNotifier<String> nameNotifier;
  String get name => nameNotifier.value ?? id;
  set name(String value) => nameNotifier.value = value;

  ValueNotifier<bool> focusedNotifier = ValueNotifier(false);
  bool get focused => focusedNotifier.value;
  set focused(bool value) => focusedNotifier.value = value;

  final ValueNotifier<ChildViewConnection> childViewConnectionNotifier;
  ChildViewConnection get childViewConnection =>
      childViewConnectionNotifier.value;

  ValueNotifier<bool> fullscreenNotifier = ValueNotifier(false);
  bool get fullscreen => fullscreenNotifier.value;
  set fullscreen(bool value) => fullscreenNotifier.value = value;
  bool get isImmersive => fullscreenNotifier.value == true;

  void delete() => onDelete?.call(this);

  void focus() => onChange?.call(this..focused = true);

  void maximize() => onChange?.call(this..fullscreen = true);

  void restore() => onChange?.call(this..fullscreen = false);

  ValueNotifier<bool> editStateNotifier = ValueNotifier(false);
  void edit() => editStateNotifier.value = !editStateNotifier.value;
}
