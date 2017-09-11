// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert' show JSON;
import 'package:lib.app.dart/app.dart';
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
  static final _emailHeadline = "Open Email";
  static final _youtubeHeadline = "Open YouTube";
  static final _terminalHeadline = "Open Terminal";
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
          appUrl: 'dashboard',
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
          appUrl: 'chat_conversation_list',
          headline: _chatHeadline,
          color: 0xFF9C27B0, // Material Purple 500
        ),
      );
    }
    if ((query.text?.isNotEmpty ?? false) &&
        _emailHeadline.toLowerCase().contains(query.text.toLowerCase())) {
      proposals.add(
        _createProposal(
          id: 'open email',
          appUrl: 'email/nav',
          headline: _emailHeadline,
          color: 0xFF4285F4,
        ),
      );
    }
    if ((query.text?.isNotEmpty ?? false) &&
        _youtubeHeadline.toLowerCase().contains(query.text.toLowerCase())) {
      proposals.add(
        _createProposal(
          id: 'open youtube',
          appUrl: 'youtube_story',
          headline: _youtubeHeadline,
          color: 0xFFE52D27 /* YouTube red from color spec */,
        ),
      );
    }
    if ((query.text?.isNotEmpty ?? false) &&
        _terminalHeadline.toLowerCase().contains(query.text.toLowerCase())) {
      proposals.add(
        _createProposal(
          id: 'open terminal',
          appUrl: 'moterm',
          headline: _terminalHeadline,
          color: 0xFFE52D27 /* YouTube red from color spec */,
        ),
      );
    }

    if (query.text?.contains(_musicPatternKanye) ?? false) {
      proposals.add(
        _createProposal(
          id: 'Listen to Kanye',
          appUrl: 'music_artist',
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
          appUrl: 'music_artist',
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

    if (query.text?.startsWith('test s') ?? false) {
      proposals.addAll(_kDummyProposals);
      proposals.addAll(_kDummyInterruptions);
    }

    callback(proposals);
  }
}

final List<Proposal> _kDummyProposals = <Proposal>[
  _createUniqueDummyProposal(
    headline: 'Headline only',
  ),
  _createUniqueDummyProposal(
    headline: 'Headline with a longer text string',
  ),
  _createUniqueDummyProposal(
    headline:
        'Headline with a longer text string that needs more space plus some',
  ),
  _createUniqueDummyProposal(
    headline:
        'Headline using an even longer text string that needs to wrap to a third line just like this',
  ),
  _createUniqueDummyProposal(
    headline:
        'Headline using an even longer text string that needs to wrap to a third line and exceeds the three line limit by adding a few more words',
  ),
  _createUniqueDummyProposal(
    headline: 'Headline small',
    subheadline: 'With a sub-headline',
  ),
  _createUniqueDummyProposal(
    headline: 'Headline small',
    subheadline: 'With a longer sub-headline that wraps to two lines',
  ),
  _createUniqueDummyProposal(
    headline: 'Headline small',
    subheadline:
        'With a longer sub-headline that wraps to two lines and then a third line just like this',
  ),
  _createUniqueDummyProposal(
    headline: 'Headline small',
    subheadline:
        'With a longer sub-headline that needs to wrap to multiple lines exceeding the maximum available lines just like this one here',
  ),
  _createUniqueDummyProposal(
    headline: 'Headline with a longer text string that needs more space',
    subheadline:
        'With a longer sub-headline that wraps to two lines and then a third line just like this',
  ),
  _createUniqueDummyProposal(
    headline:
        'Headline using an even longer text string that needs to wrap to a third line just like this',
    subheadline:
        'With a longer sub-headline that wraps to two lines and then a third line just like this',
  ),
  _createUniqueDummyProposal(
    headline: 'Headline only',
    imageUrl: 'https://i.redd.it/qh713wbo4r8y.jpg',
  ),
  _createUniqueDummyProposal(
    headline: 'Headline with a longer text string',
    imageUrl: 'https://avatars2.githubusercontent.com/u/12826430?v=3&s=200',
  ),
  _createUniqueDummyProposal(
    headline: 'Headline with a longer text string needing more space',
    imageUrl: 'https://i.redd.it/qh713wbo4r8y.jpg',
  ),
  _createUniqueDummyProposal(
    headline:
        'Headline using an even longer text string that needs to wrap to a third line',
    imageUrl: 'https://i.redd.it/qh713wbo4r8y.jpg',
  ),
  _createUniqueDummyProposal(
    headline:
        'Headline using an even longer text string that needs to wrap to a third line and exceeds the three line limit',
    imageUrl: 'https://i.redd.it/qh713wbo4r8y.jpg',
  ),
  _createUniqueDummyProposal(
    headline: 'Headline small',
    subheadline: 'With a sub-headline',
    imageUrl: 'https://i.redd.it/qh713wbo4r8y.jpg',
  ),
  _createUniqueDummyProposal(
    headline: 'Headline small',
    subheadline: 'With a longer sub-headline that wraps to two lines',
    imageUrl: 'https://i.redd.it/qh713wbo4r8y.jpg',
  ),
  _createUniqueDummyProposal(
    headline: 'Headline small',
    subheadline:
        'With a longer sub-headline that wraps to two lines and then a third line',
    imageUrl: 'https://i.redd.it/qh713wbo4r8y.jpg',
  ),
  _createUniqueDummyProposal(
    headline: 'Headline small',
    subheadline:
        'With a longer sub-headline that needs to wrap to multiple lines exceeding the maximum available lines',
    imageUrl: 'https://i.redd.it/qh713wbo4r8y.jpg',
  ),
  _createUniqueDummyProposal(
    headline: 'Headline with a longer text string needing more space',
    subheadline:
        'With a longer sub-headline that wraps to two lines and then a third line',
    imageUrl: 'https://i.redd.it/qh713wbo4r8y.jpg',
  ),
  _createUniqueDummyProposal(
    headline:
        'Headline using an even longer text string that needs to wrap to a third line',
    subheadline:
        'With a longer sub-headline that wraps to two lines and then a third line',
    imageUrl: 'https://i.redd.it/qh713wbo4r8y.jpg',
  ),
  _createUniqueDummyProposal(
    headline: 'Headline only',
    imageUrl: 'https://i.redd.it/qh713wbo4r8y.jpg',
    imageType: SuggestionImageType.person,
  ),
  _createUniqueDummyProposal(
    headline: 'Headline with a longer text string',
    imageUrl: 'https://i.redd.it/qh713wbo4r8y.jpg',
    imageType: SuggestionImageType.person,
  ),
  _createUniqueDummyProposal(
    headline: 'Headline with a longer text string that needs more space',
    imageUrl: 'https://i.redd.it/qh713wbo4r8y.jpg',
    imageType: SuggestionImageType.person,
  ),
  _createUniqueDummyProposal(
    headline:
        'Headline using an even longer text string that needs to wrap to a third line',
    imageUrl: 'https://i.redd.it/qh713wbo4r8y.jpg',
    imageType: SuggestionImageType.person,
  ),
  _createUniqueDummyProposal(
    headline:
        'Headline using an even longer text string that needs to wrap to a third line and exceeds the three line limit',
    imageUrl: 'https://i.redd.it/qh713wbo4r8y.jpg',
    imageType: SuggestionImageType.person,
  ),
  _createUniqueDummyProposal(
    headline: 'Headline small',
    subheadline: 'With a sub-headline',
    imageUrl: 'https://avatars2.githubusercontent.com/u/12826430?v=3&s=200',
    imageType: SuggestionImageType.person,
  ),
  _createUniqueDummyProposal(
    headline: 'Headline small',
    subheadline: 'With a longer sub-headline that wraps to two lines',
    imageUrl: 'https://i.redd.it/qh713wbo4r8y.jpg',
    imageType: SuggestionImageType.person,
  ),
  _createUniqueDummyProposal(
    headline: 'Headline small',
    subheadline:
        'With a longer sub-headline that wraps to two lines and then a third line',
    imageUrl: 'https://i.redd.it/qh713wbo4r8y.jpg',
    imageType: SuggestionImageType.person,
  ),
  _createUniqueDummyProposal(
    headline: 'Headline small',
    subheadline:
        'With a longer sub-headline that needs to wrap to multiple lines exceeding the maximum available lines',
    imageUrl: 'https://i.redd.it/qh713wbo4r8y.jpg',
    imageType: SuggestionImageType.person,
  ),
  _createUniqueDummyProposal(
    headline: 'Headline with a longer text string that needs more space',
    subheadline:
        'With a longer sub-headline that wraps to two lines and then a third line',
    imageUrl: 'https://avatars2.githubusercontent.com/u/12826430?v=3&s=200',
    imageType: SuggestionImageType.person,
  ),
  _createUniqueDummyProposal(
    headline:
        'Headline using an even longer text string that needs to wrap to a third line',
    subheadline:
        'With a longer sub-headline that wraps to two lines and then a third line',
    imageUrl: 'https://i.redd.it/qh713wbo4r8y.jpg',
    imageType: SuggestionImageType.person,
  ),
  _createUniqueDummyProposal(
    headline: 'Headline only',
    iconUrl: '/data/test.png',
  ),
  _createUniqueDummyProposal(
    headline: 'Headline with a longer text string',
    iconUrl: '/data/test.png',
  ),
  _createUniqueDummyProposal(
    headline: 'Headline with a longer text string that needs more space',
    iconUrl: '/data/test.png',
  ),
  _createUniqueDummyProposal(
    headline:
        'Headline using an even longer text string that needs to wrap to a third line',
    iconUrl: '/data/test.png',
  ),
  _createUniqueDummyProposal(
    headline:
        'Headline using an even longer text string that needs to wrap to a third line and exceeds the three line limit',
    iconUrl: '/data/test.png',
  ),
  _createUniqueDummyProposal(
    headline: 'Headline small',
    subheadline: 'With a sub-headline',
    iconUrl: '/data/test.png',
  ),
  _createUniqueDummyProposal(
    headline: 'Headline small',
    subheadline: 'With a longer sub-headline that wraps to two lines',
    iconUrl: '/data/test.png',
  ),
  _createUniqueDummyProposal(
    headline: 'Headline small',
    subheadline:
        'With a longer sub-headline that wraps to two lines and then a third line',
    iconUrl: '/data/test.png',
  ),
  _createUniqueDummyProposal(
    headline: 'Headline small',
    subheadline:
        'With a longer sub-headline that needs to wrap to multiple lines exceeding the maximum available lines',
    iconUrl: '/data/test.png',
  ),
  _createUniqueDummyProposal(
    headline: 'Headline with a longer text string that needs more space',
    subheadline:
        'With a longer sub-headline that wraps to two lines and then a third line',
    iconUrl: '/data/test.png',
  ),
  _createUniqueDummyProposal(
    headline:
        'Headline using an even longer text string that needs to wrap to a third line',
    subheadline:
        'With a longer sub-headline that wraps to two lines and then a third line',
    iconUrl: '/data/test.png',
  ),
];

final List<Proposal> _kDummyInterruptions = <Proposal>[
  _createUniqueDummyProposal(
    headline: 'Headline only',
    annoyanceType: AnnoyanceType.interrupt,
  ),
  _createUniqueDummyProposal(
    headline: 'Headline with a longer text string',
    annoyanceType: AnnoyanceType.interrupt,
  ),
  _createUniqueDummyProposal(
    headline:
        'Headline with a longer text string that needs more space plus some',
    annoyanceType: AnnoyanceType.interrupt,
  ),
  _createUniqueDummyProposal(
    headline:
        'Headline using an even longer text string that needs to wrap to a third line just like this',
    annoyanceType: AnnoyanceType.interrupt,
  ),
  _createUniqueDummyProposal(
    headline:
        'Headline using an even longer text string that needs to wrap to a third line and exceeds the three line limit by adding a few more words',
    annoyanceType: AnnoyanceType.interrupt,
  ),
  _createUniqueDummyProposal(
    headline: 'Headline small',
    subheadline: 'With a sub-headline',
    annoyanceType: AnnoyanceType.interrupt,
  ),
  _createUniqueDummyProposal(
    headline: 'Headline small',
    subheadline: 'With a longer sub-headline that wraps to two lines',
    annoyanceType: AnnoyanceType.interrupt,
  ),
  _createUniqueDummyProposal(
    headline: 'Headline small',
    subheadline:
        'With a longer sub-headline that wraps to two lines and then a third line just like this',
    annoyanceType: AnnoyanceType.interrupt,
  ),
  _createUniqueDummyProposal(
    headline: 'Headline small',
    subheadline:
        'With a longer sub-headline that needs to wrap to multiple lines exceeding the maximum available lines just like this one here',
    annoyanceType: AnnoyanceType.interrupt,
  ),
  _createUniqueDummyProposal(
    headline: 'Headline with a longer text string that needs more space',
    subheadline:
        'With a longer sub-headline that wraps to two lines and then a third line just like this',
    annoyanceType: AnnoyanceType.interrupt,
  ),
  _createUniqueDummyProposal(
    headline:
        'Headline using an even longer text string that needs to wrap to a third line just like this',
    subheadline:
        'With a longer sub-headline that wraps to two lines and then a third line just like this',
    annoyanceType: AnnoyanceType.interrupt,
  ),
  _createUniqueDummyProposal(
    headline: 'Headline only',
    imageUrl: 'https://i.redd.it/qh713wbo4r8y.jpg',
    annoyanceType: AnnoyanceType.interrupt,
  ),
  _createUniqueDummyProposal(
    headline: 'Headline with a longer text string',
    imageUrl: 'https://avatars2.githubusercontent.com/u/12826430?v=3&s=200',
    annoyanceType: AnnoyanceType.interrupt,
  ),
  _createUniqueDummyProposal(
    headline: 'Headline with a longer text string needing more space',
    imageUrl: 'https://i.redd.it/qh713wbo4r8y.jpg',
    annoyanceType: AnnoyanceType.interrupt,
  ),
  _createUniqueDummyProposal(
    headline:
        'Headline using an even longer text string that needs to wrap to a third line',
    imageUrl: 'https://i.redd.it/qh713wbo4r8y.jpg',
    annoyanceType: AnnoyanceType.interrupt,
  ),
  _createUniqueDummyProposal(
    headline:
        'Headline using an even longer text string that needs to wrap to a third line and exceeds the three line limit',
    imageUrl: 'https://i.redd.it/qh713wbo4r8y.jpg',
    annoyanceType: AnnoyanceType.interrupt,
  ),
  _createUniqueDummyProposal(
    headline: 'Headline small',
    subheadline: 'With a sub-headline',
    imageUrl: 'https://i.redd.it/qh713wbo4r8y.jpg',
    annoyanceType: AnnoyanceType.interrupt,
  ),
  _createUniqueDummyProposal(
    headline: 'Headline small',
    subheadline: 'With a longer sub-headline that wraps to two lines',
    imageUrl: 'https://i.redd.it/qh713wbo4r8y.jpg',
    annoyanceType: AnnoyanceType.interrupt,
  ),
  _createUniqueDummyProposal(
    headline: 'Headline small',
    subheadline:
        'With a longer sub-headline that wraps to two lines and then a third line',
    imageUrl: 'https://i.redd.it/qh713wbo4r8y.jpg',
    annoyanceType: AnnoyanceType.interrupt,
  ),
  _createUniqueDummyProposal(
    headline: 'Headline small',
    subheadline:
        'With a longer sub-headline that needs to wrap to multiple lines exceeding the maximum available lines',
    imageUrl: 'https://i.redd.it/qh713wbo4r8y.jpg',
    annoyanceType: AnnoyanceType.interrupt,
  ),
  _createUniqueDummyProposal(
    headline: 'Headline with a longer text string needing more space',
    subheadline:
        'With a longer sub-headline that wraps to two lines and then a third line',
    imageUrl: 'https://i.redd.it/qh713wbo4r8y.jpg',
    annoyanceType: AnnoyanceType.interrupt,
  ),
  _createUniqueDummyProposal(
    headline:
        'Headline using an even longer text string that needs to wrap to a third line',
    subheadline:
        'With a longer sub-headline that wraps to two lines and then a third line',
    imageUrl: 'https://i.redd.it/qh713wbo4r8y.jpg',
    annoyanceType: AnnoyanceType.interrupt,
  ),
  _createUniqueDummyProposal(
    headline: 'Headline only',
    imageUrl: 'https://i.redd.it/qh713wbo4r8y.jpg',
    imageType: SuggestionImageType.person,
    annoyanceType: AnnoyanceType.interrupt,
  ),
  _createUniqueDummyProposal(
    headline: 'Headline with a longer text string',
    imageUrl: 'https://i.redd.it/qh713wbo4r8y.jpg',
    imageType: SuggestionImageType.person,
    annoyanceType: AnnoyanceType.interrupt,
  ),
  _createUniqueDummyProposal(
    headline: 'Headline with a longer text string that needs more space',
    imageUrl: 'https://i.redd.it/qh713wbo4r8y.jpg',
    imageType: SuggestionImageType.person,
    annoyanceType: AnnoyanceType.interrupt,
  ),
  _createUniqueDummyProposal(
    headline:
        'Headline using an even longer text string that needs to wrap to a third line',
    imageUrl: 'https://i.redd.it/qh713wbo4r8y.jpg',
    imageType: SuggestionImageType.person,
    annoyanceType: AnnoyanceType.interrupt,
  ),
  _createUniqueDummyProposal(
    headline:
        'Headline using an even longer text string that needs to wrap to a third line and exceeds the three line limit',
    imageUrl: 'https://i.redd.it/qh713wbo4r8y.jpg',
    imageType: SuggestionImageType.person,
    annoyanceType: AnnoyanceType.interrupt,
  ),
  _createUniqueDummyProposal(
    headline: 'Headline small',
    subheadline: 'With a sub-headline',
    imageUrl: 'https://avatars2.githubusercontent.com/u/12826430?v=3&s=200',
    imageType: SuggestionImageType.person,
    annoyanceType: AnnoyanceType.interrupt,
  ),
  _createUniqueDummyProposal(
    headline: 'Headline small',
    subheadline: 'With a longer sub-headline that wraps to two lines',
    imageUrl: 'https://i.redd.it/qh713wbo4r8y.jpg',
    imageType: SuggestionImageType.person,
    annoyanceType: AnnoyanceType.interrupt,
  ),
  _createUniqueDummyProposal(
    headline: 'Headline small',
    subheadline:
        'With a longer sub-headline that wraps to two lines and then a third line',
    imageUrl: 'https://i.redd.it/qh713wbo4r8y.jpg',
    imageType: SuggestionImageType.person,
    annoyanceType: AnnoyanceType.interrupt,
  ),
  _createUniqueDummyProposal(
    headline: 'Headline small',
    subheadline:
        'With a longer sub-headline that needs to wrap to multiple lines exceeding the maximum available lines',
    imageUrl: 'https://i.redd.it/qh713wbo4r8y.jpg',
    imageType: SuggestionImageType.person,
    annoyanceType: AnnoyanceType.interrupt,
  ),
  _createUniqueDummyProposal(
    headline: 'Headline with a longer text string that needs more space',
    subheadline:
        'With a longer sub-headline that wraps to two lines and then a third line',
    imageUrl: 'https://avatars2.githubusercontent.com/u/12826430?v=3&s=200',
    imageType: SuggestionImageType.person,
    annoyanceType: AnnoyanceType.interrupt,
  ),
  _createUniqueDummyProposal(
    headline:
        'Headline using an even longer text string that needs to wrap to a third line',
    subheadline:
        'With a longer sub-headline that wraps to two lines and then a third line',
    imageUrl: 'https://i.redd.it/qh713wbo4r8y.jpg',
    imageType: SuggestionImageType.person,
    annoyanceType: AnnoyanceType.interrupt,
  ),
  _createUniqueDummyProposal(
    headline: 'Headline only',
    iconUrl: '/data/test.png',
    annoyanceType: AnnoyanceType.interrupt,
  ),
  _createUniqueDummyProposal(
    headline: 'Headline with a longer text string',
    iconUrl: '/data/test.png',
    annoyanceType: AnnoyanceType.interrupt,
  ),
  _createUniqueDummyProposal(
    headline: 'Headline with a longer text string that needs more space',
    iconUrl: '/data/test.png',
    annoyanceType: AnnoyanceType.interrupt,
  ),
  _createUniqueDummyProposal(
    headline:
        'Headline using an even longer text string that needs to wrap to a third line',
    iconUrl: '/data/test.png',
    annoyanceType: AnnoyanceType.interrupt,
  ),
  _createUniqueDummyProposal(
    headline:
        'Headline using an even longer text string that needs to wrap to a third line and exceeds the three line limit',
    iconUrl: '/data/test.png',
    annoyanceType: AnnoyanceType.interrupt,
  ),
  _createUniqueDummyProposal(
    headline: 'Headline small',
    subheadline: 'With a sub-headline',
    iconUrl: '/data/test.png',
    annoyanceType: AnnoyanceType.interrupt,
  ),
  _createUniqueDummyProposal(
    headline: 'Headline small',
    subheadline: 'With a longer sub-headline that wraps to two lines',
    iconUrl: '/data/test.png',
    annoyanceType: AnnoyanceType.interrupt,
  ),
  _createUniqueDummyProposal(
    headline: 'Headline small',
    subheadline:
        'With a longer sub-headline that wraps to two lines and then a third line',
    iconUrl: '/data/test.png',
    annoyanceType: AnnoyanceType.interrupt,
  ),
  _createUniqueDummyProposal(
    headline: 'Headline small',
    subheadline:
        'With a longer sub-headline that needs to wrap to multiple lines exceeding the maximum available lines',
    iconUrl: '/data/test.png',
    annoyanceType: AnnoyanceType.interrupt,
  ),
  _createUniqueDummyProposal(
    headline: 'Headline with a longer text string that needs more space',
    subheadline:
        'With a longer sub-headline that wraps to two lines and then a third line',
    iconUrl: '/data/test.png',
    annoyanceType: AnnoyanceType.interrupt,
  ),
  _createUniqueDummyProposal(
    headline:
        'Headline using an even longer text string that needs to wrap to a third line',
    subheadline:
        'With a longer sub-headline that wraps to two lines and then a third line',
    iconUrl: '/data/test.png',
    annoyanceType: AnnoyanceType.interrupt,
  ),
];
int _id = 0;
Proposal _createUniqueDummyProposal({
  String headline,
  String subheadline: '',
  String iconUrl,
  String imageUrl: '',
  SuggestionImageType imageType: SuggestionImageType.other,
  AnnoyanceType annoyanceType: AnnoyanceType.none,
}) =>
    _createProposal(
      id: 'dummy ${_id++}',
      appUrl: 'file:///foo/bar',
      headline: headline,
      subheadline: subheadline,
      color: 0xFF000000,
      iconUrls: iconUrl != null ? <String>[iconUrl] : <String>[],
      imageType: imageType,
      imageUrl: imageUrl,
      annoyanceType: annoyanceType,
    );

Proposal _createProposal({
  String id,
  String appUrl,
  String headline,
  String subheadline,
  String imageUrl: '',
  String initialData,
  SuggestionImageType imageType: SuggestionImageType.other,
  List<String> iconUrls = const <String>[],
  int color,
  AnnoyanceType annoyanceType: AnnoyanceType.none,
}) =>
    new Proposal()
      ..id = id
      ..display = (new SuggestionDisplay()
        ..headline = headline
        ..subheadline = subheadline ?? ''
        ..details = ''
        ..color = color
        ..iconUrls = iconUrls
        ..imageType = imageType
        ..imageUrl = imageUrl
        ..annoyance = annoyanceType)
      ..onSelected = <Action>[
        new Action()
          ..createStory = (new CreateStory()
            ..moduleId = appUrl
            ..initialData = initialData)
      ];
