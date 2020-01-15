// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

import 'package:fuchsia_scenic_flutter/child_view_connection.dart';
import 'package:tiler/tiler.dart' show TilerModel, TileModel;

import 'ermine_shell.dart';
import 'ermine_story.dart';

// The minimum size of a tile we support.
const _kMinTileSize = Size(320, 240);

/// Defines a collection of [ClusterModel] instances. Calls [notifyListeners] when
/// a cluster is added or deleted.
class ClustersModel extends ChangeNotifier implements ErmineShell {
  /// The list of [ClusterModel] initialized to one cluster.
  final List<ClusterModel> clusters = [ClusterModel()];

  /// The cluster currently in view.
  final ValueNotifier<ClusterModel> currentCluster =
      ValueNotifier<ClusterModel>(null);

  final _storyToCluster = <String, ClusterModel>{};

  /// Change notifier when fullscreen is toggled for a story.
  ValueNotifier<ErmineStory> fullscreenStoryNotifier = ValueNotifier(null);

  /// Get the current story that is fullscreen.
  ErmineStory get fullscreenStory => fullscreenStoryNotifier.value;

  /// Change notifier when focus is toggled for a story.
  ValueNotifier<ErmineStory> focusedStoryNotifier = ValueNotifier(null);

  /// Get the story that has focus.
  ErmineStory get focusedStory => focusedStoryNotifier.value;

  /// Returns [true] if currentCluster is the first cluster.
  bool get isFirst =>
      currentCluster.value == null || currentCluster.value == clusters.first;

  /// Returns [true] if currentCluster is the last cluster.
  bool get isLast =>
      currentCluster.value == null || currentCluster.value == clusters.last;

  /// Returns [true] is there are stories running.
  bool get hasStories => _storyToCluster.isNotEmpty;

  /// Returns a iterable of all [Story] objects.
  Iterable<ErmineStory> get stories => _storyToCluster.keys.map(getStory);

  /// Maximize the story to fullscreen: it's visual state to IMMERSIVE.
  void maximize(String id) {
    if (fullscreenStoryNotifier.value?.id == id) {
      return;
    }
    fullscreenStoryNotifier.value?.fullscreen = false;

    fullscreenStoryNotifier.value = getStory(id)..fullscreen = true;
  }

  /// Restore the story to non-fullscreen mode: it's visual state to DEFAULT.
  void restore(String id) {
    if (fullscreenStoryNotifier.value?.id != id) {
      return;
    }
    fullscreenStoryNotifier.value?.fullscreen = false;
    fullscreenStoryNotifier.value = null;
  }

  /// Returns the story given it's id.
  ErmineStory getStory(String id) => _storyToCluster[id]?.getStory(id);

  /// Creates and adds a [Story] to the current cluster given it's [StoryInfo],
  /// [SessionShell] and [StoryController]. Returns the created instance.
  @override
  ErmineStory storyStarted(
    String id,
    String name,
    ChildViewConnection childViewConnection,
  ) {
    assert(!_storyToCluster.containsKey(id));
    final story = ErmineStory(
      id: id,
      name: name,
      childViewConnection: childViewConnection,
      onDelete: storyDeleted,
      onChange: storyChanged,
    );

    // Add the story to the current cluster
    currentCluster.value ??= clusters.first;
    _storyToCluster[story.id] = currentCluster.value..add(story);

    // If current cluster is the last cluster, add one more to allow user
    // to navigate to it.
    if (currentCluster.value == clusters.last) {
      clusters.add(ClusterModel());
    }
    notifyListeners();

    story.focus();
    return story;
  }

  /// Removes the [story] from the current cluster. If this is the last story
  /// in the cluster, the cluster is also removed unless it is the last cluster.
  @override
  void storyDeleted(ErmineStory story) {
    assert(_storyToCluster.containsKey(story.id));
    final cluster = _storyToCluster[story.id]..remove(story);
    int index = clusters.indexOf(cluster);

    // If this story was fullscreen, first restore it.
    if (story == fullscreenStory) {
      restore(story.id);
    }

    _storyToCluster.remove(story.id);

    // If there are no more stories in this cluster, remove it. We keep two
    // clusters alive at all times.
    if (!_storyToCluster.values.contains(cluster) && clusters.length > 1) {
      // Current cluster was removed, switch to another.
      if (cluster == currentCluster.value) {
        if (cluster == clusters.last) {
          currentCluster.value = clusters[index - 1];
        } else {
          currentCluster.value = clusters[index + 1];
        }
      }
      clusters.remove(cluster);

      notifyListeners();
    }
  }

  /// Called when any story attribute changes.
  @override
  void storyChanged(ErmineStory story) {
    if (story != null) {
      final storyCluster = _storyToCluster[story.id];
      if (story.focused) {
        focusedStoryNotifier.value?.focused = false;
        focusedStoryNotifier.value = story;
        // If this story has focus, bring its cluster on screen.
        if (storyCluster != currentCluster.value) {
          currentCluster.value = storyCluster;
        }
        // Update fullscreen story.
        if (story.fullscreen) {
          maximize(story.id);
        } else {
          restore(story.id);
        }
      }
      notifyListeners();
    }
  }

  /// Set's the [currentCluster] to the next cluster, if available.
  void nextCluster() {
    if (fullscreenStory == null && currentCluster.value != clusters.last) {
      currentCluster.value =
          clusters[clusters.indexOf(currentCluster.value) + 1];
    }
  }

  /// Set's the [currentCluster] to the previous cluster, if available.
  void previousCluster() {
    if (fullscreenStory == null && currentCluster.value != clusters.first) {
      currentCluster.value =
          clusters[clusters.indexOf(currentCluster.value) - 1];
    }
  }
}

/// Defines a collection of [Story] instances, internally held in TilerModel.
/// Calls [notifyListeners] when a story is added or removed from the cluster.
class ClusterModel extends ChangeNotifier {
  /// The title of the cluster.
  String title = '';

  /// The [TilerModel] that holds the [Tile] per [Story].
  final TilerModel<ErmineStory> tilerModel = TilerModel();

  ClusterModel({this.title});

  final _storyToTile = <String, TileModel<ErmineStory>>{};

  bool get isEmpty => tilerModel.root.isEmpty;

  ErmineStory getStory(String id) => _storyToTile[id]?.content;

  /// Adds a [story] to this cluster.
  void add(ErmineStory story) {
    assert(!_storyToTile.containsKey(story.id));
    final tile = tilerModel.add(content: story, minSize: _kMinTileSize);
    _storyToTile[story.id] = tile;

    notifyListeners();
  }

  /// Removes the [story] from this cluster.
  void remove(ErmineStory story) {
    assert(_storyToTile.containsKey(story.id));
    final tile = _storyToTile[story.id];
    tilerModel.remove(tile);
    _storyToTile.remove(story.id);

    notifyListeners();
  }
}
