// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:math';

/// Helper to generate random todo actions.
class Generator {
  static const List<String> _actions = const [
    "acquire",
    "cancel",
    "consider",
    "draw",
    "evaluate",
    "celebrate",
    "find",
    "identify",
    "meet with",
    "plan",
    "solve",
    "study",
    "talk to",
    "think about",
    "write an article about",
    "check out",
    "order",
    "write a spec for",
    "order",
    "track down",
    "memorize",
    "git checkout",
  ];

  static const List<String> _objects = const [
    "Christopher Columbus",
    "PHP",
    "a better way forward",
    "a glass of wine",
    "a good book on C++",
    "a huge simulation we are all part of",
    "a nice dinner out",
    "a sheep",
    "an AZERTY keyboard",
    "hipster bars south of Pigalle",
    "kittens",
    "manganese",
    "more cheese",
    "some bugs",
    "staticly-typed programming languages",
    "the cryptographic primitives",
    "the espresso machine",
    "the law of gravity",
    "the neighbor",
    "the pyramids",
    "the society",
    "velocity",
  ];

  final Random _random = new Random();

  String makeContent() {
    String action = _actions[_random.nextInt(_actions.length)];
    String object = _objects[_random.nextInt(_objects.length)];
    return "$action $object";
  }
}
