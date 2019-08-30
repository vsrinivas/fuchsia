// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:args/args.dart';

import 'collector/collector.dart';
import 'collector/args_gn_collector.dart';
import 'collector/environment_collector.dart';
import 'collector/git_collector.dart';

class StatusCmd {
  bool all;
  List<Collector> collectors = [
    new ArgsGnCollector(),
    new EnvironmentCollector(),
    new GitCollector()
  ];
  Map<String, Category> aggregatedInfo = new Map();

  StatusCmd(this.all);

  void _aggregate(List<Item> data) {
    for (Item item in data) {
      String categoryName = item.categoryType.toString();
      Category category = aggregatedInfo[categoryName];
      if (category == null) {
        category = new Category(item.categoryType);
        aggregatedInfo[categoryName] = category;
      }
      category.add(item);
    }
  }

  Future<void> aggregate() async {
    List<Future> allFutures = [];
    for (var collector in collectors) {
      var result = collector.collect(this.all);
      if (result is Future) {
        var future = result as Future<List<Item>>;
        future.then(_aggregate);
        allFutures.add(future);
      } else {
        _aggregate(result);
      }
    }
    return Future.wait(allFutures);
  }

  Map<String, dynamic> toJson() => aggregatedInfo;

  @override
  String toString() {
    StringBuffer sb = new StringBuffer();
    for (Category category in aggregatedInfo.values) {
      sb..writeln(category.toString());
    }
    return sb.toString();
  }
}

void usage(ArgParser parser) {
  stdout
    ..writeln('Usage: fx status [--format=text|json]')
    ..writeln()
    ..writeln('Options:')
    ..writeln(parser.usage);
}

Future main(List<String> args) async {
  final parser = new ArgParser()
    ..addFlag('help', abbr: 'h', help: 'Show this help')
    ..addFlag('all',
        abbr: 'a',
        defaultsTo: false,
        help: 'Also show information that can take longer to collect')
    ..addOption('format',
        abbr: 'f',
        defaultsTo: 'text',
        allowed: ['text', 'json'],
        help: 'Format of the output');

  ArgResults argResults = null;
  try {
    argResults = parser.parse(args);
  } catch (ex) {
    stderr.writeln('Invalid syntax. ${ex.message}');
    usage(parser);
    return;
  }

  if (argResults['help']) {
    usage(parser);
    return;
  }

  bool all = argResults['all'];
  StatusCmd cmd = new StatusCmd(all);
  await cmd.aggregate();

  if (argResults['format'] == 'json') {
    print(jsonEncode(cmd));
  } else if (argResults['format'] == 'text') {
    print(cmd);
  }
}
