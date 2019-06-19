// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

import '../models/cluster_model.dart';
import 'cluster.dart';

/// Defines a [PageView] to hold a [Cluster] widget per screen.
class Clusters extends StatelessWidget {
  final ClustersModel model;

  const Clusters({this.model});

  @override
  Widget build(BuildContext context) {
    final pageController = PageController();
    model.currentCluster.addListener(() {
      if (pageController.hasClients) {
        int index = model.clusters.indexOf(model.currentCluster.value);
        if (index != pageController.page) {
          if (model.clusters[index].isEmpty &&
              model.clusters[pageController.page.toInt()].isEmpty) {
            pageController.jumpToPage(index);
          } else {
            pageController.animateToPage(
              index,
              duration: Duration(milliseconds: 200),
              curve: Curves.easeOut,
            );
          }
        }
      }
    });
    return AnimatedBuilder(
      animation: model.fullscreenStoryNotifier,
      child: AnimatedBuilder(
        animation: model,
        builder: (context, child) => Stack(
              children: <Widget>[
                Positioned.fill(
                  child: PageView.builder(
                    controller: pageController,
                    scrollDirection: Axis.horizontal,
                    itemCount: model.clusters.length,
                    onPageChanged: (page) {
                      final cluster = model.clusters[page];
                      model.currentCluster.value = cluster;
                    },
                    itemBuilder: (context, index) {
                      final cluster = model.clusters[index];
                      return Padding(
                        padding: EdgeInsets.all(40),
                        child: Cluster(model: cluster),
                      );
                    },
                  ),
                ),
                AnimatedBuilder(
                  animation: model.currentCluster,
                  child: Align(
                    alignment: Alignment.centerLeft,
                    child: GestureDetector(
                      child: Icon(
                        Icons.chevron_left,
                        size: 40,
                        color: Colors.white,
                      ),
                      onTap: model.previousCluster,
                    ),
                  ),
                  builder: (context, child) =>
                      !model.isFirst ? child : Offstage(),
                ),
                AnimatedBuilder(
                  animation: model.currentCluster,
                  child: Align(
                    alignment: Alignment.centerRight,
                    child: GestureDetector(
                      child: Icon(
                        Icons.chevron_right,
                        size: 40,
                        color: Colors.white,
                      ),
                      onTap: model.nextCluster,
                    ),
                  ),
                  builder: (context, child) =>
                      !model.isLast ? child : Offstage(),
                ),
              ],
            ),
      ),
      builder: (context, child) =>
          model.fullscreenStory == null ? child : Offstage(),
    );
  }
}
