// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:modular_flutter/flutter_module.dart';

const String kSpeechInputLabel = 'speech-input';
const String kSessionIdLabel = 'session-id';
const String kTitleLabel = 'title';

// A story represents a sub session started by the user or shared with the user.
class Story {
  // Session id corresponding to the story.
  final String sessionId;

  // All the voice inputs by the user when this story is active on the screen.
  final List<String> speechInput = <String>[];

  // Title to display for this Story, if available.
  String _title;

  // Node corresponding to the story in the state graph. If its the shared story
  // then we don't need update anything in that node.
  // TODO(ksimbili) : We may want to add speech inputs to the share node too.
  SemanticNode _node;

  Story(this.sessionId, {SemanticNode storyNode, String title})
      : _title = title {
    assert(sessionId != null);
    if (storyNode != null) {
      node = storyNode;
    }
  }

  SemanticNode get node => _node;
  set node(final SemanticNode newNode) {
    assert(_node == null && newNode != null);
    // Add the session id of the story to the story node.
    _node = newNode;
    _node.getOrDefault(<String>[kSessionIdLabel]).value = sessionId;
    if (_title != null) {
      node.getOrDefault(<String>[kTitleLabel]).value = _title;
    } else {
      _title = node.get(<String>[kTitleLabel])?.value;
    }
  }

  // Returns the title for the story.
  // TODO(ksimbili): Right now, the first speech input with which the story was
  // started with is returned as title. This should be fixed with something
  // better.
  String get title => _title ?? (speechInput.isEmpty ? '' : speechInput[0]);

  void addSpeechInput(final String input) {
    if (node != null) {
      final SemanticNode speechInputNode =
          node.create(<String>[kSpeechInputLabel]);
      speechInputNode.value = input;
    }
    speechInput.add(input);
  }
}
