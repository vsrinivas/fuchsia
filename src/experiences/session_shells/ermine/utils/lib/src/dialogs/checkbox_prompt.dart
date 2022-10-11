// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(http://fxbug.dev/107480): Resolve lint issues and reenable analysis for file
// ignore_for_file: deprecated_member_use

import 'package:ermine_utils/ermine_utils.dart';
import 'package:flutter/material.dart';
import 'package:flutter_mobx/flutter_mobx.dart';

/// Defines a widget that provides a text description and a checkbox as a 
/// content of an [AlertDialog].
class CheckboxPrompt extends StatelessWidget {
  final CheckboxDialogInfo info;

  const CheckboxPrompt(this.info);

  @override
  Widget build(BuildContext context) {
    return Observer(builder: (context) {
      return Column(
        mainAxisSize: MainAxisSize.min,
        mainAxisAlignment: MainAxisAlignment.center,
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          if (info.body != null)
            Column(children: [
              SizedBox(height: 40),
              Text(info.body!),
            ]),
          SizedBox(height: 24),
          FormField<bool>(
            initialValue: false,
            onSaved: info.onSubmit,
            builder: (state) => Row(
              crossAxisAlignment: CrossAxisAlignment.center,
              children: [
                SizedBox(
                  width: 24,
                  child: Checkbox(
                    checkColor: Theme.of(context).bottomAppBarColor,
                    onChanged: state.didChange,
                    value: state.value,
                  ),
                ),
                SizedBox(width: 24),
                Text(info.checkboxLabel,
                    style: Theme.of(context).textTheme.bodyText1)
              ],
            ),
          ),
        ],
      );
    });
  }
}
