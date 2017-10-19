// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_LEDGER_CLIENT_CONSTANTS_H_
#define PERIDOT_LIB_LEDGER_CLIENT_CONSTANTS_H_

namespace modular {

constexpr char kCloudProviderFirebaseAppUrl[] =
    "file:///system/apps/cloud_provider_firebase";
constexpr char kLedgerAppUrl[] = "file:///system/apps/ledger";
constexpr char kLedgerNoMinfsWaitFlag[] = "--no_minfs_wait";
constexpr char kLedgerDataBaseDir[] = "/data/ledger/";
constexpr uint8_t kLedgerRootPageId[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

// Hard-coded communal Ledger instance.
const char kFirebaseServerId[] = "fuchsia-ledger";
const char kFirebaseApiKey[] = "AIzaSyDzzuJILOn6riFPTXC36HlH6CEdliLapDA";

}  // namespace modular

#endif  // PERIDOT_LIB_LEDGER_CLIENT_CONSTANTS_H_
