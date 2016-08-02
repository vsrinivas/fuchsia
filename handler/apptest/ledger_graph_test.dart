// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:test/test.dart';

import 'package:modular_services/ledger2/ledger2.mojom.dart'
    show LedgerProxy, LedgerStub;

import 'package:mojo/core.dart' show MojoMessagePipe;

import '../mojo/in_memory_ledger.dart';

void testLedgerGraph() {
  LedgerProxy _ledger;
  InMemoryLedger _ledgerImpl;
  LedgerStub _stub;

  setUp(() {
    final MojoMessagePipe pipe = new MojoMessagePipe();
    _ledgerImpl = new InMemoryLedger();
    _ledger = new LedgerProxy.fromEndpoint(pipe.endpoints[1]);
    _stub = new LedgerStub.fromEndpoint(pipe.endpoints[0], _ledgerImpl);
  });

  tearDown(() {
    _ledger.close();
    _stub.close();
  });

  // TODO(jjosh): write tests
}
