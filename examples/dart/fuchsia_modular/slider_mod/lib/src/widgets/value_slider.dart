// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:fidl_fuchsia_fibonacci/fidl_async.dart' as fidl_fib;
import 'package:flutter/material.dart';
import 'package:fuchsia_modular/service_connection.dart';

import '../blocs/bloc_provider.dart';
import '../blocs/fibonacci_bloc.dart';
import '../blocs/slider_bloc.dart';

class ValueSlider extends StatelessWidget {
  final _fibBloc = FibonacciBloc();

  @override
  Widget build(BuildContext context) {
    final sliderBloc = BlocProvider.of<SliderBloc>(context);

    return Column(
      children: <Widget>[
        StreamBuilder<double>(
            stream: sliderBloc.valueStream,
            initialData: sliderBloc.currentValue,
            builder: (BuildContext context, AsyncSnapshot<double> snapshot) {
              return Column(
                children: <Widget>[
                  Slider(
                    max: sliderBloc.maxValue,
                    min: sliderBloc.minValue,
                    value: snapshot.data,
                    onChanged: sliderBloc.updateValue,
                  ),
                  Container(
                    alignment: Alignment.center,
                    child: Text('Value: ${snapshot.data.toInt()}',
                        style: TextStyle(fontSize: 34.0)),
                  ),
                ],
              );
            }),
        ElevatedButton(
          child: Text('Calc Fibonacci'),
          onPressed: () {
            _onCalcFibBtnPressed(sliderBloc);
          },
        ),
        Container(
          alignment: Alignment.center,
          child: _buildFibResultWidget(_fibBloc),
        ),
      ],
    );
  }

  StreamBuilder<int> _buildFibResultWidget(FibonacciBloc fibBloc) {
    return StreamBuilder<int>(
        stream: fibBloc.valueStream,
        initialData: fibBloc.currentValue,
        builder: (BuildContext context, AsyncSnapshot<int> snapshot) {
          if (snapshot.data == 0) {
            // don't display anything
            return Offstage();
          } else {
            return Container(
              alignment: Alignment.center,
              child: Text('Result: ${snapshot.data}',
                  style: TextStyle(fontSize: 34.0)),
              key: Key('fib-result-widget-key'),
            );
          }
        });
  }

  void _onCalcFibBtnPressed(SliderBloc sliderBloc) {
    // recreating the proxy for every button press just to illustrate
    final _proxy = fidl_fib.FibonacciServiceProxy();
    deprecatedConnectToAgentService(
        'fuchsia-pkg://fuchsia.com/fibonacci_agent#meta/fibonacci_agent.cmx',
        _proxy);

    _proxy
        .calcFibonacci(sliderBloc.currentValue.toInt())
        .then(_fibBloc.updateValue)
        .catchError(
      (e, s) {
        print('Something went wrong: $e $s');
        throw e;
      },
    );
  }
}
