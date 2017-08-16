// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This agent uses Cobalt to record usage data in a privacy preserving way.
// To learn more about Cobalt, see:
// https://fuchsia.googlesource.com/cobalt_client/+/master/README.md
//
// To view the data collected by this agent in Cobalt, read the following
// instructions:
// https://fuchsia.googlesource.com/cobalt_client/+/master/README.md#Report-Client
//
// After downloading the reporting tool, run it with the command line:
// ./report_client -report_master_uri=35.188.119.76:7001 -project_id=101
//
// At the report tool command line, type "run full 1" to see the module URL
// report for the complete set of data.

import 'dart:collection';

import 'package:application.lib.app.dart/app.dart';
import 'package:apps.cobalt_client.services..cobalt/cobalt.fidl.dart';
import 'package:apps.maxwell.lib.context.dart/context_listener_impl.dart';
import 'package:apps.maxwell.services.context/context_reader.fidl.dart';
import 'package:apps.maxwell.services.context/value.fidl.dart';
import 'package:apps.maxwell.services.context/value_type.fidl.dart';

// The project ID of the usage_log registered in Cobalt.
const _cobaltProjectID = 101;

// The IDs of the Cobalt metric and encoding we are using.
// These specify objects within our Cobalt project configuration.
const _cobaltMetricID = 1;
const _cobaltEncodingID = 1;

// connection to context reader
final _contextReader = new ContextReaderProxy();
ContextListenerImpl _contextListener;

// connection to Cobalt
final _encoder = new CobaltEncoderProxy();

// Deduplication Map
var _topicDedupSet = new LinkedHashSet<String>();

// ContextListener callback
void onContextUpdate(ContextUpdate update) {
  update.values["modules"].forEach((ContextValue value) {
    String dedupKey = value.meta.story.id + value.meta.mod.url;
    // To record module launches, we only process each topic once
    if (_topicDedupSet.contains(dedupKey)) {
      return;
    }
    _topicDedupSet.add(dedupKey);

    // print("[USAGE LOG] Recording module url $url");
    _encoder.addStringObservation(
        _cobaltMetricID, _cobaltEncodingID, value.meta.mod.url,
        onAddObservationStatus);
  });
}

void onAddObservationStatus(Status status) {
  // If adding an observation fails, we simply drop it and do not retry.
  // TODO(jwnichols): Perhaps we should do something smarter if we fail
  if (status != Status.ok) {
    print ("[USAGE LOG] Failed to add Cobalt observation.");
  } else {
    _encoder.sendObservations(onSendObservationStatus);
  }
}

void onSendObservationStatus(Status status) {
  // If sending observations fails, we simply drop them and do not retry.
  // TODO(jwnichols): Perhaps we should do something smarter if we fail
  if (status != Status.ok) {
    print ("[USAGE LOG] Failed to send Cobalt observations.");
  }
}

void main(List args) {
  final appContext = new ApplicationContext.fromStartupInfo();

  // Connect to the ContextReader
  _contextListener = new ContextListenerImpl(onContextUpdate);
  connectToService(appContext.environmentServices, _contextReader.ctrl);
  assert(_contextReader.ctrl.isBound);

  // Subscribe to all topics
  ContextSelector selector = new ContextSelector();
  selector.type = ContextValueType.module;
  ContextQuery query = new ContextQuery();
  query.selector = <String, ContextSelector>{"modules": selector};
  _contextReader.subscribe(query, _contextListener.getHandle());

  // Connect to Cobalt
  var encoderFactory = new CobaltEncoderFactoryProxy();
  connectToService(appContext.environmentServices, encoderFactory.ctrl);
  assert(encoderFactory.ctrl.isBound);

  // Get an encoder
  encoderFactory.getEncoder(_cobaltProjectID, _encoder.ctrl.request());

  appContext.close();
  encoderFactory.ctrl.close();
}
