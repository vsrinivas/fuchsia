// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert' show JSON;
import 'dart:io';
import 'package:application.lib.app.dart/app.dart';
import 'package:apps.maxwell.lib.dart/decomposition.dart';
import 'package:apps.maxwell.services.suggestion/ask_handler.fidl.dart';
import 'package:apps.maxwell.services.suggestion/proposal.fidl.dart';
import 'package:apps.maxwell.services.suggestion/proposal_publisher.fidl.dart';
import 'package:apps.maxwell.services.suggestion/suggestion_display.fidl.dart';
import 'package:apps.maxwell.services.suggestion/user_input.fidl.dart';
import 'package:web_view/web_view.dart' as web_view;

final _proposalPublisher = new ProposalPublisherProxy();
final _askHandlerBinding = new AskHandlerBinding();

void main(List args) {
  final context = new ApplicationContext.fromStartupInfo();
  connectToService(context.environmentServices, _proposalPublisher.ctrl);
  _proposalPublisher.registerAskHandler(
    _askHandlerBinding.wrap(new _AskHandlerImpl()),
  );
  context.close();
}

class _AskHandlerImpl extends AskHandler {
  static final _urlSubPattern =
      new RegExp(r"\.[a-z]{2}|(?:\d{1,3}\.){3}\d{1,3}|localhost");
  static final _dashboardSubPattern = new RegExp(r"^das|^fuc|^bui|^sta");
  static final _chatHeadline = "Open Chat";
  static final _musicPatternKanye = new RegExp(r"kanye|yeezus");
  static final _musicPatternPortugal = new RegExp(r"portugal|the man");

  @override
  void ask(UserInput query, void callback(List<Proposal> proposals)) {
    List<Proposal> proposals = new List();
    if (query.text?.contains(_urlSubPattern) ?? false) {
      final String url = query.text.startsWith("http")
          ? query.text
          : query.text.startsWith("localhost")
              ? "http://" + query.text
              : "https://" + query.text;

      proposals.add(
        _createProposal(
          id: 'launch web_view',
          appUrl: web_view.kWebViewURL,
          headline: 'Go to ' + query.text,
          color: 0xff8080ff,
          initialData: JSON.encode({
            "view": {"uri": url}
          }),
        ),
      );
    }
    if (query.text?.contains(_dashboardSubPattern) ?? false) {
      proposals.add(
        _createProposal(
          id: 'launch dashboard',
          appUrl: 'file:///system/apps/dashboard',
          headline: 'View the Fuchsia Dashboard',
          color: 0xFFFF0080, // Fuchsia
          imageUrl:
              'https://avatars2.githubusercontent.com/u/12826430?v=3&s=200',
        ),
      );
    }
    if ((query.text?.isNotEmpty ?? false) &&
        _chatHeadline.toLowerCase().contains(query.text.toLowerCase())) {
      proposals.add(
        _createProposal(
          id: 'open chat',
          appUrl: 'file:///system/apps/chat_conversation_list',
          headline: _chatHeadline,
          color: 0xFF9C27B0, // Material Purple 500
        ),
      );
    }

    if ((query.text?.length ?? 0) >= 4) {
      Directory systemApps = new Directory('/system/apps/');
      systemApps
          .listSync(followLinks: false)
          .map((FileSystemEntity fileSystemEntity) => fileSystemEntity.path)
          .where((String path) =>
              Uri.parse(path).pathSegments.last.contains(query.text))
          .forEach((String path) {
        String name = Uri.parse(path).pathSegments.last;
        proposals.add(
          _createProposal(
            id: 'open $name',
            appUrl: 'file://$path',
            headline: 'Launch $name',
            subheadline: '(This is potentially unsafe)',
            color: 0xFF000000 + (name.hashCode % 0xFFFFFF),
          ),
        );
      });
    }

    if (query.text?.contains(_musicPatternKanye) ?? false) {
      proposals.add(
        _createProposal(
          id: 'Listen to Kanye',
          appUrl: 'file:///system/apps/music_artist',
          headline: 'Listen to Kanye',
          color: 0xFF9C27B0, // Material Purple 500,
          initialData: JSON.encode({
            'view': decomposeUri(new Uri(
                scheme: 'spotify',
                host: 'artist',
                path: '5K4W6rqBFWDnAN6FQUkS6x'))
          }),
        ),
      );
    }

    if (query.text?.contains(_musicPatternPortugal) ?? false) {
      proposals.add(
        _createProposal(
          id: 'Listen to Portugal. The Man',
          appUrl: 'file:///system/apps/music_artist',
          headline: 'Listen to Portugal. The Man',
          color: 0xFF9C27B0, // Material Purple 500,
          initialData: JSON.encode({
            'view': decomposeUri(new Uri(
                scheme: 'spotify',
                host: 'artist',
                path: '4kI8Ie27vjvonwaB2ePh8T'))
          }),
        ),
      );
    }

    callback(proposals);
  }
}

Proposal _createProposal({
  String id,
  String appUrl,
  String headline,
  String subheadline,
  String imageUrl: '',
  String initialData,
  int color,
}) =>
    new Proposal()
      ..id = id
      ..display = (new SuggestionDisplay()
        ..headline = headline + (subheadline == null ? '' : '\n$subheadline')
        ..subheadline = ''
        ..details = ''
        ..color = color
        ..iconUrls = const <String>[]
        ..imageType = SuggestionImageType.other
        ..imageUrl = imageUrl)
      ..onSelected = <Action>[
        new Action()
          ..createStory = (new CreateStory()
            ..moduleId = appUrl
            ..initialData = initialData)
      ];
