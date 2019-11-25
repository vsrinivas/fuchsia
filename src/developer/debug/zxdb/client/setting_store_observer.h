// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_SETTING_STORE_OBSERVER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_SETTING_STORE_OBSERVER_H_

#include <string>

namespace zxdb {

class SettingStore;

// Interface for listening to changes to a particular SettingStore. The main use case for this is to
// listen to the System settings for some changes (eg. the BuildIDIndex needs to know when a new
// path has been added in order to index it).
//
// Note that an observer listens for a particular setting:
//
//    store.AddObserver(settings::kSomeSetting, this);
class SettingStoreObserver {
 public:
  // The store is given because an observer could be listening to two stores at the same time. It's
  // the observer's job to correctly identify the setting and call the correct getter/setter
  // functions.
  virtual void OnSettingChanged(const SettingStore&, const std::string& setting_name) = 0;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_SETTING_STORE_OBSERVER_H_
