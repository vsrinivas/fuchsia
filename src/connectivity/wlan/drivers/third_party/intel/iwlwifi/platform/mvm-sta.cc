// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/mvm-sta.h"

#include <fidl/fuchsia.wlan.ieee80211/cpp/wire.h>
#include <threads.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <algorithm>
#include <cstring>

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/sta.h"
}  // extern "C"

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/ieee80211.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/mvm-mlme.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/rcu.h"

namespace wlan::iwlwifi {
namespace {

// IEEE 802.11-2016 3.2 (c.f. "vendor organizationally unique identifier")
constexpr uint8_t kIeeeOui[] = {0x00, 0x0F, 0xAC};

}  // namespace

MvmSta::MvmSta(struct iwl_mvm_vif* iwl_mvm_vif, std::unique_ptr<struct iwl_mvm_sta> iwl_mvm_sta)
    : iwl_mvm_vif_(iwl_mvm_vif), iwl_mvm_sta_(std::move(iwl_mvm_sta)) {}

MvmSta::~MvmSta() {
  if (iwl_mvm_sta_ != nullptr) {
    zx_status_t status = ZX_OK;

    for (auto& key_conf : ieee80211_key_confs_) {
      if (key_conf == nullptr) {
        continue;
      }
      if ((status = iwl_mvm_mac_remove_key(iwl_mvm_sta_->mvmvif, iwl_mvm_sta_.get(),
                                           key_conf.get())) != ZX_OK) {
        IWL_ERR(iwl_mvm_vif_, "iwl_mvm_mac_remove_key() failed for keyidx %d: %s\n",
                key_conf->keyidx, zx_status_get_string(status));
      }
      key_conf.reset();
    }

    if ((status = ChangeState(iwl_sta_state::IWL_STA_NOTEXIST)) != ZX_OK) {
      IWL_ERR(iwl_mvm_vif_, "ChangeState() failed: %s\n", zx_status_get_string(status));
    }

    iwl_rcu_call_sync(
        iwl_mvm_vif_->mvm->dev,
        [](void* data) {
          auto mvm_sta = reinterpret_cast<struct iwl_mvm_sta*>(data);
          for (auto txq : mvm_sta->txq) {
            delete txq;
          }
          delete mvm_sta;
        },
        iwl_mvm_sta_.release());
  }
}

// static
zx_status_t MvmSta::Create(struct iwl_mvm_vif* iwl_mvm_vif, const uint8_t bssid[ETH_ALEN],
                           std::unique_ptr<MvmSta>* mvm_sta_out) {
  zx_status_t status = ZX_OK;

  // Initialize the iwl_mvm_sta instance.
  auto iwl_mvm_sta = std::make_unique<struct iwl_mvm_sta>();
  for (auto& txq_ref : iwl_mvm_sta->txq) {
    txq_ref = new struct iwl_mvm_txq();
  }
  static_assert(sizeof(iwl_mvm_sta->addr) == sizeof(*bssid) * ETH_ALEN);
  std::memcpy(iwl_mvm_sta->addr, bssid, sizeof(iwl_mvm_sta->addr));
  mtx_init(&iwl_mvm_sta->lock, mtx_plain);

  auto mvm_sta = std::unique_ptr<MvmSta>(new MvmSta(iwl_mvm_vif, std::move(iwl_mvm_sta)));

  if ((status = mvm_sta->ChangeState(iwl_sta_state::IWL_STA_NONE)) != ZX_OK) {
    return status;
  }

  {
    // Allocate a TX queue for this station.
    auto iwl_mvm = mvm_sta->iwl_mvm_vif_->mvm;
    auto lock = std::lock_guard(iwl_mvm->mutex);
    if ((status = iwl_mvm_sta_alloc_queue(iwl_mvm, mvm_sta->iwl_mvm_sta_.get(), IEEE80211_AC_BE,
                                          IWL_MAX_TID_COUNT)) != ZX_OK) {
      mtx_unlock(&iwl_mvm->mutex);
      IWL_ERR(iwl_mvm, "iwl_mvm_sta_alloc_queue() failed: %s\n", zx_status_get_string(status));
      return status;
    }
  }

  *mvm_sta_out = std::move(mvm_sta);
  return status;
}

zx_status_t MvmSta::SetKey(const struct wlan_key_config* key_config) {
  zx_status_t status = ZX_OK;
  struct iwl_mvm* const mvm = iwl_mvm_sta_->mvmvif->mvm;

  if (mvm->trans->cfg->gen2 || iwl_mvm_has_new_tx_api(mvm)) {
    // The new firmwares (for starting with the 22000 series) have different packet generation
    // requirements than mentioned below.
    return ZX_ERR_NOT_SUPPORTED;
  }

  if (!std::equal(key_config->cipher_oui,
                  key_config->cipher_oui + std::size(key_config->cipher_oui), kIeeeOui,
                  kIeeeOui + std::size(kIeeeOui))) {
    // IEEE 802.11-2016 9.4.2.25.2
    // The standard ciphers all live in the IEEE space.
    return ZX_ERR_NOT_SUPPORTED;
  }

  switch (static_cast<fuchsia_wlan_ieee80211::wire::CipherSuiteType>(key_config->cipher_type)) {
    case fuchsia_wlan_ieee80211::wire::CipherSuiteType::kCcmp128:
      // Note: the Linux iwlwifi driver requests IEEE80211_KEY_FLAG_PUT_IV_SPACE from the mac80211
      // stack.  We will apply equivalent functionality manually to Incoming packets from Fuchsia.
      break;
    default:
      // Additional porting required for other types.
      return ZX_ERR_NOT_SUPPORTED;
  }

  if (key_config->key_type < 0 || key_config->key_type >= ieee80211_key_confs_.size()) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Remove any existing key in this slot.
  if (ieee80211_key_confs_[key_config->key_type] != nullptr) {
    if ((status = iwl_mvm_mac_remove_key(iwl_mvm_sta_->mvmvif, iwl_mvm_sta_.get(),
                                         ieee80211_key_confs_[key_config->key_type].get())) !=
        ZX_OK) {
      IWL_ERR(mvmvif, "iwl_mvm_mac_remove_key() failed: %s\n", zx_status_get_string(status));
      return status;
    }
    ieee80211_key_confs_[key_config->key_type].reset();
  }

  unique_free_ptr<struct ieee80211_key_conf> key_conf(reinterpret_cast<struct ieee80211_key_conf*>(
      malloc(sizeof(ieee80211_key_conf) + key_config->key_len)));
  memset(key_conf.get(), 0, sizeof(*key_conf) + key_config->key_len);
  key_conf->cipher = key_config->cipher_type;
  key_conf->keyidx = key_config->key_idx;
  key_conf->keylen = key_config->key_len;
  key_conf->rx_seq = key_config->rsc;
  memcpy(key_conf->key, key_config->key, key_conf->keylen);

  if ((status = iwl_mvm_mac_add_key(iwl_mvm_sta_->mvmvif, iwl_mvm_sta_.get(), key_conf.get())) !=
      ZX_OK) {
    IWL_ERR(mvmvif, "iwl_mvm_mac_add_key(key_type %d, cipher_type %d, key_idx %d) failed: %s\n",
            key_config->key_type, key_config->cipher_type, key_config->key_idx,
            zx_status_get_string(status));
    return status;
  }

  ieee80211_key_confs_[key_config->key_type] = std::move(key_conf);
  return ZX_OK;
}

struct ieee80211_key_conf* MvmSta::GetKey(wlan_key_type_t key_type) {
  if (key_type < 0 || key_type > ieee80211_key_confs_.size()) {
    return nullptr;
  }
  return ieee80211_key_confs_[key_type].get();
}

const struct ieee80211_key_conf* MvmSta::GetKey(wlan_key_type_t key_type) const {
  if (key_type < 0 || key_type > ieee80211_key_confs_.size()) {
    return nullptr;
  }
  return ieee80211_key_confs_[key_type].get();
}

enum iwl_sta_state MvmSta::GetState() const { return sta_state_; }

zx_status_t MvmSta::ChangeState(enum iwl_sta_state state) {
  zx_status_t status = ZX_OK;
  while (state > sta_state_) {
    if ((status = ChangeStateUp()) != ZX_OK) {
      return status;
    }
  }
  while (state < sta_state_) {
    if ((status = ChangeStateDown()) != ZX_OK) {
      return status;
    }
  }
  return ZX_OK;
}

struct iwl_mvm_sta* MvmSta::iwl_mvm_sta() {
  return iwl_mvm_sta_.get();
}

const struct iwl_mvm_sta* MvmSta::iwl_mvm_sta() const { return iwl_mvm_sta_.get(); }

zx_status_t MvmSta::ChangeStateUp() {
  zx_status_t status = ZX_OK;
  iwl_sta_state new_state = iwl_sta_state::IWL_STA_NOTEXIST;
  switch (sta_state_) {
    case iwl_sta_state::IWL_STA_NOTEXIST: {
      new_state = iwl_sta_state::IWL_STA_NONE;
      break;
    }
    case iwl_sta_state::IWL_STA_NONE: {
      new_state = iwl_sta_state::IWL_STA_AUTH;
      break;
    }
    case iwl_sta_state::IWL_STA_AUTH: {
      new_state = iwl_sta_state::IWL_STA_ASSOC;
      break;
    }
    case iwl_sta_state::IWL_STA_ASSOC: {
      new_state = iwl_sta_state::IWL_STA_AUTHORIZED;
      break;
    }
    default: {
      IWL_ERR(iwl_mvm_vif_, "ChangeStateUp() in invalid state %d\n", sta_state_);
      return ZX_ERR_BAD_STATE;
    }
  }

  if ((status = iwl_mvm_mac_sta_state(iwl_mvm_vif_, iwl_mvm_sta_.get(), sta_state_, new_state)) !=
      ZX_OK) {
    IWL_ERR(iwl_mvm_vif_, "iwl_mvm_mac_sta_state() failed for %d -> %d: %s\n", sta_state_,
            new_state, zx_status_get_string(status));
    return status;
  }

  sta_state_ = new_state;
  return ZX_OK;
}

zx_status_t MvmSta::ChangeStateDown() {
  zx_status_t status = ZX_OK;
  iwl_sta_state new_state = iwl_sta_state::IWL_STA_NOTEXIST;
  switch (sta_state_) {
    case iwl_sta_state::IWL_STA_AUTHORIZED: {
      new_state = iwl_sta_state::IWL_STA_ASSOC;
      break;
    }
    case iwl_sta_state::IWL_STA_ASSOC: {
      new_state = iwl_sta_state::IWL_STA_AUTH;
      break;
    }
    case iwl_sta_state::IWL_STA_AUTH: {
      new_state = iwl_sta_state::IWL_STA_NONE;
      break;
    }
    case iwl_sta_state::IWL_STA_NONE: {
      {
        // Tell firmware to flush all packets in the Tx queue. This must be done before we remove
        // the STA (in the NONE->NOTEXIST transition).
        // TODO(79799): understand why we need this.
        auto lock = std::lock_guard(iwl_mvm_vif_->mvm->mutex);
        iwl_mvm_flush_sta(iwl_mvm_vif_->mvm, iwl_mvm_sta_.get(), false, 0);
      }

      new_state = iwl_sta_state::IWL_STA_NOTEXIST;
      break;
    }
    default: {
      IWL_ERR(iwl_mvm_vif_, "ChangeStateDown() in invalid state %d\n", sta_state_);
      return ZX_ERR_BAD_STATE;
    }
  }

  if ((status = iwl_mvm_mac_sta_state(iwl_mvm_vif_, iwl_mvm_sta_.get(), sta_state_, new_state)) !=
      ZX_OK) {
    IWL_ERR(iwl_mvm_vif_, "iwl_mvm_mac_sta_state() failed for %d -> %d: %s\n", sta_state_,
            new_state, zx_status_get_string(status));
    return status;
  }

  sta_state_ = new_state;
  return ZX_OK;
}

}  // namespace wlan::iwlwifi
