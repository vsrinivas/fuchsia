// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert' show JSON;
import 'package:application.lib.app.dart/app.dart';
import 'package:apps.maxwell.services.suggestion/ask_handler.fidl.dart';
import 'package:apps.maxwell.services.suggestion/proposal.fidl.dart';
import 'package:apps.maxwell.services.suggestion/proposal_publisher.fidl.dart';
import 'package:apps.maxwell.services.suggestion/suggestion_display.fidl.dart';
import 'package:apps.maxwell.services.suggestion/user_input.fidl.dart';

final _proposalPublisher = new ProposalPublisherProxy();
final _askHandlerBinding = new AskHandlerBinding();

class AskHandlerImpl extends AskHandler {
  final _urlSubPattern = new RegExp(r"\.[a-z]{2}");
  final _dashboardSubPattern = new RegExp(r"^das|^fuc|^bui|^sta");

  @override
  void ask(UserInput query, void callback(List<Proposal> proposals)) {
    List<Proposal> proposals = new List();
    if (query.text?.contains(_urlSubPattern) ?? false) {
      final proposal = new Proposal();
      proposal.id = "launch web_view";

      proposal.display = new SuggestionDisplay();
      proposal.display.headline = "Go to " + query.text;
      proposal.display.subheadline = "";
      proposal.display.details = "";
      proposal.display.color = 0xff8080ff;
      proposal.display.iconUrls = new List<String>();
      proposal.display.imageType = SuggestionImageType.other;
      proposal.display.imageUrl = "";

      final createStory = new CreateStory();
      createStory.moduleId = "file:///system/apps/web_view";
      final String url =
          query.text.startsWith("http") ? query.text : "https://" + query.text;
      createStory.initialData = JSON.encode({"url": url});

      final action = new Action();
      action.createStory = createStory;
      proposal.onSelected = new List<Action>();
      proposal.onSelected.add(action);

      proposals.add(proposal);
    }
    if (query.text?.contains(_dashboardSubPattern) ?? false) {
      final proposal = new Proposal();
      proposal.id = "launch dashboard";

      proposal.display = new SuggestionDisplay();
      proposal.display.headline = "View the Fuchsia Dashboard";
      proposal.display.subheadline = "";
      proposal.display.details = "";
      proposal.display.color = 0xFFFFF8FF;
      proposal.display.iconUrls = new List<String>();
      proposal.display.imageType = SuggestionImageType.other;
      proposal.display.imageUrl =
          "https://avatars2.githubusercontent.com/u/12826430?v=3&s=200";

      final createStory = new CreateStory();
      createStory.moduleId = "file:///system/apps/dashboard";

      final action = new Action();
      action.createStory = createStory;
      proposal.onSelected = new List<Action>();
      proposal.onSelected.add(action);

      proposals.add(proposal);
    }
    callback(proposals);
  }
}

void main(List args) {
  final context = new ApplicationContext.fromStartupInfo();
  connectToService(context.environmentServices, _proposalPublisher.ctrl);
  _proposalPublisher
      .registerAskHandler(_askHandlerBinding.wrap(new AskHandlerImpl()));
  context.close();
}
