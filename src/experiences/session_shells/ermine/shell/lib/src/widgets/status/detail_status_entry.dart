// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_fuchsia_ui_remotewidgets/fidl_async.dart';
import 'package:flutter/material.dart';

import '../../models/status_model.dart';
import '../../utils/styles.dart';
import 'spec_builder.dart';

/// Defines a widget that displays a status entry in the detail view of
/// status shellement.
class DetailStatusEntry extends StatelessWidget {
  final StatusModel model;
  final ValueChanged<Value> onChange;
  final _lastSpec = ValueNotifier<Spec>(null);

  DetailStatusEntry({this.model, this.onChange});

  @override
  Widget build(BuildContext context) {
    return AnimatedBuilder(
      animation: model.detailNotifier,
      builder: (context, _) {
        // Show Offstage if detail stream or last spec is not available.
        if (_lastSpec.value == null && model.detailStream == null) {
          return Offstage();
        }

        final uiStream = model.detailStream;
        final spec = _lastSpec.value ?? uiStream.spec;
        return Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: <Widget>[
            Container(
              decoration: BoxDecoration(
                border: Border(
                  bottom: BorderSide(
                    color: ErmineStyle.kOverlayBorderColor,
                    width: ErmineStyle.kOverlayBorderWidth,
                  ),
                ),
              ),
              child: Row(
                children: <Widget>[
                  IconButton(
                    icon: Icon(Icons.arrow_back),
                    onPressed: () => onChange(Value.withButton(ButtonValue(
                      label: '',
                      action: QuickAction.cancel.$value,
                    ))),
                  ),
                  Padding(padding: EdgeInsets.only(left: 8)),
                  Expanded(
                    child: Text(spec.title.toUpperCase()),
                  ),
                ],
              ),
            ),
            Padding(padding: EdgeInsets.only(bottom: 8)),
            Flexible(
              child: SingleChildScrollView(
                child: model.detailStream == null
                    ? buildFromSpec(spec, onChange)
                    : StreamBuilder<Spec>(
                        stream: uiStream.stream,
                        initialData: uiStream.spec,
                        builder: (context, snapshot) {
                          if (!snapshot.hasData) {
                            return Offstage();
                          }
                          _lastSpec.value = snapshot.data;
                          return buildFromSpec(_lastSpec.value, onChange);
                        },
                      ),
              ),
            ),
          ],
        );
      },
    );
  }
}
