// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_fuchsia_ui_remotewidgets/fidl_async.dart';
import 'package:flutter/material.dart';
import 'package:quickui/uistream.dart';

import 'spec_builder.dart';

/// Defines a widget to represent a status entry in the Status shellement.
class StatusEntry extends StatelessWidget {
  final UiStream uiStream;
  final ValueChanged<Value> onChange;
  final ValueNotifier<UiStream> detailNotifier;
  final _lastSpec = ValueNotifier<Spec>(null);

  StatusEntry({this.uiStream, this.onChange, this.detailNotifier}) {
    uiStream.listen();
  }

  @override
  Widget build(BuildContext context) {
    return AnimatedBuilder(
      animation: detailNotifier,
      builder: (context, child) {
        // If a [DetailStatusEntry] is displaying for this stream, freeze the
        // UI for this stream until its shown.
        return detailNotifier.value == uiStream
            ? buildFromSpec(_lastSpec.value, onChange)
            : StreamBuilder<Spec>(
                stream: uiStream.stream,
                initialData: uiStream.spec,
                builder: (context, snapshot) {
                  if (!snapshot.hasData || snapshot.data == UiStream.nullSpec) {
                    return Offstage();
                  }
                  _lastSpec.value = snapshot.data;
                  return buildFromSpec(_lastSpec.value, onChange);
                },
              );
      },
    );
  }
}
