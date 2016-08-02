// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:flutter/widgets.dart';

import 'enter_exit_transition_fading_child.dart';
import 'resizing_stack.dart';

/// A widget that resizes to fit its current child.  New children fade in.  Old
/// children fade out.
class EnterExitTransition extends StatefulWidget {
  EnterExitTransition({Key key}) : super(key: key);

  @override
  EnterExitTransitionState createState() => new EnterExitTransitionState();
}

/// [State] for [EnterExitTransition].
class EnterExitTransitionState extends State<EnterExitTransition> {
  final List<EnterExitTransitionFadingChild> _children =
      <EnterExitTransitionFadingChild>[];

  @override
  Widget build(_) => new ResizingStack(children: _children);

  /// Sets the new child to [newChild].  All other children that haven't
  /// begun or finished exiting will start or continue their exiting and this
  /// [Widget] will resize to the new child.
  /// Setting [newChild] to null effectively begins the resizing of the child
  /// to 0.
  set child(Widget newChild) => setState(() {
        _children.toList().forEach((EnterExitTransitionFadingChild child) {
          final GlobalKey childKey = child.key;
          final EnterExitTransitionFadingChildState childState =
              childKey.currentState;
          if (childState == null) {
            _children.remove(child);
          } else {
            Future<Null> future = childState.exit();
            if (future == null) {
              _children.remove(child);
            } else {
              future.then((_) {
                if (!mounted) {
                  return;
                }
                setState(() => _children.remove(child));
              });
            }
          }
        });
        _children.add(new EnterExitTransitionFadingChild(
            key: new GlobalKey(),
            child: newChild ?? new Container(height: 0.0, width: 1.0)));
      });
}
