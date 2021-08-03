// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This class is to provide the common code to give MVM testing cases the ability to mock the
// underlying transportation functions.
//

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_MOCK_TRANS_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_MOCK_TRANS_H_

#include <zircon/assert.h>

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-trans.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/sim-trans.h"

namespace wlan::testing {

typedef decltype(((struct iwl_trans_ops*)(nullptr))->send_cmd) send_cmd_t;
typedef decltype(((struct iwl_trans_ops*)(nullptr))->tx) tx_t;

// Used by the testing case to loop back.
//
// This must be declared in macro form because we need to use 'this' to get the instance of the
// testing case.  Thus, when this macro is called in testing case (usually in the constructor),
// we can assign it to priv->test.
//
#define BIND_TEST(trans)                     \
  do {                                       \
    ZX_ASSERT(trans);                        \
    trans_ = trans;                          \
    priv_ = IWL_TRANS_GET_SIM_TRANS(trans_); \
    priv_->test = this;                      \
  } while (0)

// Used by the wrapper function in the testing case, which is a static function to mock a
// iwl_trans.ops callback function.  Thus, this function must be in macro form.
//
#define GET_TEST(TestClass, trans) \
  reinterpret_cast<TestClass*>(IWL_TRANS_GET_SIM_TRANS(trans)->test)

class MockTrans {
 public:
  MockTrans() = default;
  MockTrans(const MockTrans&) = delete;
  ~MockTrans() = default;

  void bindSendCmd(send_cmd_t new_send_cmd) {
    org_send_cmd = trans_->ops->send_cmd;
    trans_->ops->send_cmd = new_send_cmd;
  }

  void unbindSendCmd() { trans_->ops->send_cmd = org_send_cmd; }

  void bindTx(tx_t new_tx) {
    ZX_ASSERT(trans_);
    ZX_ASSERT(trans_->ops);

    org_tx = trans_->ops->tx;
    trans_->ops->tx = new_tx;
  }

  void unbindTx() { trans_->ops->tx = org_tx; }

 protected:
  struct iwl_trans* trans_;
  struct sim_trans_priv* priv_;

 private:
  send_cmd_t org_send_cmd;
  tx_t org_tx;
};

}  // namespace wlan::testing

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_MOCK_TRANS_H_
