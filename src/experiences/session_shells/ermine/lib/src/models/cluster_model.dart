// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

import 'package:fidl_fuchsia_modular/fidl_async.dart'
    show StoryInfo, StoryController, StoryVisibilityState;
import 'package:fuchsia_modular_flutter/session_shell.dart'
    show SessionShell, Story;
import 'package:tiler/tiler.dart' show TilerModel, TileModel;

import 'ermine_story.dart';

// The minimum size of a tile we support.
const _kMinTileSize = Size(320, 240);

/// Defines a collection of [ClusterModel] instances. Calls [notifyListeners] when
/// a cluster is added or deleted.
class ClustersModel extends ChangeNotifier {
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

  /// Returns [true] if currentCluster is the first cluster.
  bool get isFirst =>
      currentCluster.value == null || currentCluster.value == clusters.first;

  /// Returns [true] if currentCluster is the last cluster.
  bool get isLast =>
      currentCluster.value == null || currentCluster.value == clusters.last;

  /// Maximize the story to fullscreen: it's visual state to IMMERSIVE.
  void maximize(String id) {
    if (fullscreenStoryNotifier.value?.id == id) {
      return;
    }
    fullscreenStoryNotifier.value?.visibilityState =
        StoryVisibilityState.default$;

    fullscreenStoryNotifier.value = getStory(id)
      ..visibilityState = StoryVisibilityState.immersive;
  }

  /// Restore the story to non-fullscreen mode: it's visual state to DEFAULT.
  void restore(String id) {
    if (fullscreenStoryNotifier.value?.id != id) {
      return;
    }
    fullscreenStoryNotifier.value?.visibilityState =
        StoryVisibilityState.default$;
    fullscreenStoryNotifier.value = null;
  }

  /// Returns the story given it's id.
  ErmineStory getStory(String id) => _storyToCluster[id]?.getStory(id);

  /// Creates and adds a [Story] to the current cluster given it's [StoryInfo],
  /// [SessionShell] and [StoryController]. Returns the created instance.
  Story addStory({
    StoryInfo info,
    SessionShell sessionShell,
    StoryController controller,
  }) {
    assert(!_storyToCluster.containsKey(info.id));
    final story = ErmineStory(
      info: info,
      sessionShell: sessionShell,
      controller: controller,
      clustersModel: this,
    );
    // Add the story to the current cluster
    currentCluster.value ??= clusters.first;
    _storyToCluster[story.id] = currentCluster.value..add(story);

    // If current cluster is the last cluster, add one more to allow user
    // to navigate to it.
    if (currentCluster.value == clusters.last) {
      clusters.add(ClusterModel());
      notifyListeners();
    }
    return story;
  }

  /// Removes the [story] from the current cluster. If this is the last story
  /// in the cluster, the cluster is also removed unless it is the last cluster.
  void removeStory(Story story) {
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
  void add(Story story) {
    assert(!_storyToTile.containsKey(story.id));
    final tile = tilerModel.add(content: story, minSize: _kMinTileSize);
    _storyToTile[story.id] = tile;

    notifyListeners();
  }

  /// Removes the [story] from this cluster.
  void remove(Story story) {
    assert(_storyToTile.containsKey(story.id));
    final tile = _storyToTile[story.id];
    tilerModel.remove(tile);
    _storyToTile.remove(story.id);

    notifyListeners();
  }
}
