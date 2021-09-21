// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:flutter/material.dart';

import '../blocs/bloc_provider.dart';
import '../blocs/slider_bloc.dart';
import 'value_slider.dart';

class SliderScaffold extends StatelessWidget {
  const SliderScaffold();

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: BlocProvider<SliderBloc>(
        bloc: SliderBloc(),
        child: Center(
          child: ValueSlider(),
        ),
      ),
    );
  }
}
