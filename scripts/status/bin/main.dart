// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:args/args.dart';
import 'package:pedantic/pedantic.dart';
import 'package:status/status.dart';

class StatusCmd {
  List<Collector> collectors = [
    ArgsGnCollector(),
    EnvironmentCollector(),
    GitCollector(),
  ];
  Map<String, Category> aggregatedInfo = {};

  void _aggregate(List<Item> data) {
    for (Item item in data) {
      String categoryName = item.categoryType.toString();
      Category category = aggregatedInfo[categoryName];
      if (category == null) {
        category = Category(item.categoryType);
        aggregatedInfo[categoryName] = category;
      }
      category.add(item);
    }
  }

  Future<void> aggregate() async {
    List<Future> allFutures = [];
    for (var collector in collectors) {
      Future<List<Item>> result = collector.collect();
      unawaited(result.then(_aggregate));
      allFutures.add(result);
    }
    return Future.wait(allFutures);
  }

  Map<String, dynamic> toJson() => aggregatedInfo;

  @override
  String toString() {
    return aggregatedInfo.values.join('\n');
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
  final parser = ArgParser()
    ..addFlag('help', abbr: 'h', help: 'Show this help')
    ..addOption('format',
        abbr: 'f',
        defaultsTo: 'text',
        allowed: ['text', 'json'],
        help: 'Format of the output');

  ArgResults argResults;
  try {
    argResults = parser.parse(args);
  } on Exception catch (ex) {
    stderr.writeln('Invalid syntax. $ex');
    usage(parser);
    return;
  }

  if (argResults['help']) {
    usage(parser);
    return;
  }

  StatusCmd cmd = StatusCmd();
  await cmd.aggregate();

  if (argResults['format'] == 'json') {
    print(jsonEncode(cmd));
  } else if (argResults['format'] == 'text') {
    print(cmd);
  }
}
