// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'dart:io';
import 'package:collection/collection.dart';

import 'package:lib.app.dart/app.dart';
import 'package:apps.maxwell.services.suggestion..debug/debug.fidl.dart';

import 'data_handler.dart';

Function listEqual = const ListEquality().equals;

class ProposalSubscribersDataHandler extends AskProposalListener
    with NextProposalListener, InterruptionProposalListener, DataHandler {
  @override
  String get name => "proposal_subscribers";

  AskProposalListenerBinding _askListenerBinding;
  NextProposalListenerBinding _nextListenerBinding;
  InterruptionProposalListenerBinding _interruptionListenerBinding;

  List<ProposalSummary> _currentNextProposals = [];
  List<ProposalSummary> _lastAskProposals = [];
  String _lastQuery = "";
  ProposalSummary _lastSelectedProposal;
  ProposalSummary _lastInterruptionProposal;

  SendWebSocketMessage _sendMessage;

  String makeJsonMessage() {
    return JSON.encode({
      "suggestions": {
        "ask_query": _lastQuery,
        "ask_proposals": _lastAskProposals,
        "selection": _lastSelectedProposal,
        "next_proposals": _currentNextProposals,
        "interruption": _lastInterruptionProposal,
      }
    });
  }

  @override
  void init(ApplicationContext appContext, SendWebSocketMessage sender) {
    this._sendMessage = sender;

    final suggestionDebug = new SuggestionDebugProxy();
    _askListenerBinding = new AskProposalListenerBinding();
    _nextListenerBinding = new NextProposalListenerBinding();
    _interruptionListenerBinding = new InterruptionProposalListenerBinding();
    connectToService(appContext.environmentServices, suggestionDebug.ctrl);
    assert(suggestionDebug.ctrl.isBound);

    // Watch for Ask, Next, and Interruption proposal changes.
    suggestionDebug.watchAskProposals(_askListenerBinding.wrap(this));
    suggestionDebug.watchNextProposals(_nextListenerBinding.wrap(this));
    suggestionDebug
        .watchInterruptionProposals(_interruptionListenerBinding.wrap(this));
    suggestionDebug.ctrl.close();
  }

  @override
  bool handleRequest(String requestString, HttpRequest request) {
    return false;
  }

  @override
  void handleNewWebSocket(WebSocket socket) {
    socket.add(this.makeJsonMessage());
  }

  @override
  void onAskStart(String query, List<ProposalSummary> proposals) {
    if (!listEqual(_lastAskProposals, proposals) || (_lastQuery != query)) {
      _lastAskProposals = proposals;
      _lastQuery = query;
      this._sendMessage(this.makeJsonMessage());
    }
  }

  @override
  void onProposalSelected(ProposalSummary selectedProposal) {
    if (_lastSelectedProposal != selectedProposal) {
      _lastSelectedProposal = selectedProposal;
      this._sendMessage(this.makeJsonMessage());
    }
  }

  @override
  void onNextUpdate(List<ProposalSummary> proposals) {
    if (!listEqual(_currentNextProposals, proposals)) {
      _currentNextProposals = proposals;
      this._sendMessage(this.makeJsonMessage());
    }
  }

  @override
  void onInterrupt(ProposalSummary interruptionProposal) {
    if (_lastInterruptionProposal != interruptionProposal) {
      _lastInterruptionProposal = interruptionProposal;
      this._sendMessage(this.makeJsonMessage());
    }
  }
}
