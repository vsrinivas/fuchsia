// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:flutter/widgets.dart';
import 'package:meta/meta.dart';

/// A Builder which is invoked when the [FutureWidget] needs to display a
/// child which represents an error state.
typedef ErrorBuilder = Widget Function(BuildContext context, Error error);

/// A widget that provides a [child] widget when it's [Future] completes.
class FutureWidget extends StatefulWidget {
  /// Holds the [Future] or [Widget] instance.
  final FutureOr<Widget> child;

  /// Holds the [Widget] that is a place holder until [child] future completes.
  final Widget loadingWidget;

  /// A builder which is invoked when the [Future] results in an error and
  /// the Widget needs to display an error widget.
  final ErrorBuilder errorBuilder;

  /// Constructor.
  const FutureWidget({
    @required this.child,
    this.loadingWidget = const Offstage(),
    this.errorBuilder = _emptyErrorBuilder,
  }) : assert(child != null);

  @override
  _FutureWidgetState createState() => _FutureWidgetState();
}

class _FutureWidgetState extends State<FutureWidget> {
  Widget _child;
  Error _error;

  @override
  void initState() {
    super.initState();

    _initWidget();
  }

  @override
  void didUpdateWidget(FutureWidget oldWidget) {
    super.didUpdateWidget(oldWidget);

    _initWidget();
  }

  @override
  Widget build(BuildContext context) {
    return _error != null ? widget.errorBuilder(context, _error) : _child;
  }

  void _initWidget() {
    if (widget.child is Widget) {
      _child = widget.child;
    } else {
      // In case this state instance is reparented to another widget when the
      // future completes, cache the current parent widget.
      FutureWidget parentWidget = widget;
      _loadWidget(parentWidget.child, (Widget child) {
        if (parentWidget == widget && mounted) {
          setState(() => _child = child);
        }
      });
      _child = widget.loadingWidget;
    }
  }

  void _loadWidget(Future<Widget> child, void callback(Widget widget)) {
    child.then(callback, onError: (Error error) {
      // Since this future can fire at any time we need to ensure that we are
      // mounted before calling setState since it is considered an error to call
      // setState when mounted is false
      if (mounted) {
        setState(() => _error = error);
      } else {
        _error = error;
      }
    });
  }
}

Widget _emptyErrorBuilder(BuildContext context, Error error) {
  return const Offstage();
}
