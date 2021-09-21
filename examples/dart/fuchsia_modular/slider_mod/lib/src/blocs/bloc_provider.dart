// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:flutter/material.dart';

// This implementation of the BLoC pattern was inspired by
// https://www.didierboelens.com/2018/08/reactive-programming---streams---bloc/

/// Generic Interface for all BLoCs
//ignore: one_member_abstracts
abstract class BlocBase {
  void dispose();
}

/// Generic BLoC provider
class BlocProvider<T extends BlocBase> extends StatefulWidget {
  /// The bloc object
  final T bloc;

  /// A child widget to insert into the widget tree
  final Widget child;

  /// the default constructor for the provider
  const BlocProvider({
    @required this.child,
    @required this.bloc,
    Key key,
  }) : super(key: key);

  @override
  _BlocProviderState<T> createState() => _BlocProviderState<T>();

  /// Returns a Bloc of the given type in the context.
  static T of<T extends BlocBase>(BuildContext context) {
    BlocProvider<T> provider =
        context.findAncestorWidgetOfExactType<BlocProvider<T>>();
    return provider.bloc;
  }
}

class _BlocProviderState<T> extends State<BlocProvider<BlocBase>> {
  @override
  Widget build(BuildContext context) {
    return widget.child;
  }

  @override
  void dispose() {
    widget.bloc.dispose();
    super.dispose();
  }
}
