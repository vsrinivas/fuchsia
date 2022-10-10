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

    iwl_mvm_vif* mvmvif = iwl_mvm_sta_->mvmvif;

    for (auto& key_conf : ieee80211_key_confs_) {
      if (key_conf == nullptr) {
        continue;
      }
      if ((status = iwl_mvm_mac_remove_key(mvmvif, iwl_mvm_sta_.get(), key_conf.get())) != ZX_OK) {
        IWL_ERR(iwl_mvm_vif_, "iwl_mvm_mac_remove_key() failed for keyidx %d: %s\n",
                key_conf->keyidx, zx_status_get_string(status));
      }
      key_conf.reset();
    }

    if ((status = ChangeState(iwl_sta_state::IWL_STA_NOTEXIST)) != ZX_OK) {
      IWL_ERR(iwl_mvm_vif_, "ChangeState() failed: %s\n", zx_status_get_string(status));
    }
    if ((mvmvif) && (mvmvif->ap_sta_id != IWL_MVM_INVALID_STA)) {
      // STA is still in a connected state, clean it up.
      if (mac_clear_assoc(mvmvif, mvmvif->addr) != ZX_OK) {
        IWL_ERR(mvmvif, "Unable to clear assoc during sta delete");
      }
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

zx_status_t MvmSta::SetKey(const fuchsia_wlan_softmac::wire::WlanKeyConfig* key_config) {
  zx_status_t status = ZX_OK;
  struct iwl_mvm* const mvm = iwl_mvm_sta_->mvmvif->mvm;

  if (!(key_config->has_cipher_oui() && key_config->has_key_type() && key_config->has_key() &&
        key_config->has_cipher_type() && key_config->has_key_idx() && key_config->has_rsc())) {
    IWL_ERR(mvmvif, "WlanKeyConfig missing fields: %s %s %s %s %s %s.",
            key_config->has_cipher_oui() ? "" : "cipher_oui",
            key_config->has_key_type() ? "" : "key_type", key_config->has_key() ? "" : "key",
            key_config->has_cipher_type() ? "" : "cipher_type",
            key_config->has_key_idx() ? "" : "key_idx", key_config->has_rsc() ? "" : "rsc");
    return ZX_ERR_INVALID_ARGS;
  }

  uint8_t key_type = static_cast<uint8_t>(key_config->key_type());
  if (mvm->trans->cfg->gen2 || iwl_mvm_has_new_tx_api(mvm)) {
    // The new firmwares (for starting with the 22000 series) have different packet generation
    // requirements than mentioned below.
    return ZX_ERR_NOT_SUPPORTED;
  }

  if (!std::equal(key_config->cipher_oui().begin(),
                  key_config->cipher_oui().begin() + key_config->cipher_oui().size(), kIeeeOui,
                  kIeeeOui + std::size(kIeeeOui))) {
    // IEEE 802.11-2016 9.4.2.25.2
    // The standard ciphers all live in the IEEE space.
    IWL_ERR(mvmvif, "Cipher OUI must be %02X:%02X:%02X. OUI %02X:%02X:%02X is not supported.",
            kIeeeOui[0], kIeeeOui[1], kIeeeOui[2], key_config->cipher_oui()[0],
            key_config->cipher_oui()[1], key_config->cipher_oui()[2]);
    return ZX_ERR_NOT_SUPPORTED;
  }

  if (key_type >= ieee80211_key_confs_.size()) {
    IWL_ERR(mvmvif, "Unknown key type: %hhu.", key_type);
    return ZX_ERR_INVALID_ARGS;
  }

  // Remove any existing key in this slot.
  if (ieee80211_key_confs_[key_type] != nullptr) {
    if ((status = iwl_mvm_mac_remove_key(iwl_mvm_sta_->mvmvif, iwl_mvm_sta_.get(),
                                         ieee80211_key_confs_[key_type].get())) != ZX_OK) {
      IWL_ERR(mvmvif, "iwl_mvm_mac_remove_key() failed: %s\n", zx_status_get_string(status));
      return status;
    }
    ieee80211_key_confs_[key_type].reset();
  }

  unique_free_ptr<struct ieee80211_key_conf> key_conf(reinterpret_cast<struct ieee80211_key_conf*>(
      calloc(1, sizeof(ieee80211_key_conf) + key_config->key().count())));
  key_conf->key_type = key_type;
  key_conf->cipher = key_config->cipher_type();
  key_conf->keyidx = key_config->key_idx();
  key_conf->keylen = key_config->key().count();
  key_conf->rx_seq = key_config->rsc();

  IWL_INFO(mvm, "Setting a new crypto key (type: %d, cipher: %d) request.\n",
           key_conf->key_type,  // WLAN_KEY_TYPE_*
           key_conf->cipher);   // CIPHER_SUITE_TYPE_*

  if (key_conf->cipher == CIPHER_SUITE_TYPE_TKIP) {
    // A special trick for TKIP group key: Swap the latest 2 8-byte (MIC-KEY-TX and MIC-KEY-RX).
    //
    // MLME copies the whole 32-byte from the AP packet which the "TX" means "AP TX". So when we
    // write to the firmware, it is acually the "firmware RX".
    //
    // IEEE 802.11-2007 8.6.2 Mapping GTK to TKIP keys
    //
    // See 8.5.1.3 for the definition of the EAPOL temporal key derived from GTK.
    // A STA shall use bits 0–127 of the temporal key as the input to the TKIP Phase 1 and Phase 2
    // mixing functions.
    // A STA shall use bits 128–191 of the temporal key as the Michael key for MSDUs from the
    // Authenticator’s STA to the Supplicant’s STA.
    // A STA shall use bits 192–255 of the temporal key as the Michael key for MSDUs from the
    // Supplicant’s STA to the Authenticator’s STA.
    //
    ZX_ASSERT_MSG(key_config->key().count() == 32, "TKIP key length must be 32. Found %zu",
                  key_config->key().count());
    memcpy(&key_conf->key[0], &key_config->key().begin()[0], 16);   // TK (Temporal Key)
    memcpy(&key_conf->key[16], &key_config->key().begin()[24], 8);  // AP TX ==> FW RX
    memcpy(&key_conf->key[24], &key_config->key().begin()[16], 8);  // AP RX ==> FW TX, which is not
                                                                    // used in TKIP group key.
  } else {
    memcpy(key_conf->key, key_config->key().begin(), key_conf->keylen);
  }

  if ((status = iwl_mvm_mac_add_key(iwl_mvm_sta_->mvmvif, iwl_mvm_sta_.get(), key_conf.get())) !=
      ZX_OK) {
    IWL_ERR(mvmvif, "iwl_mvm_mac_add_key(key_type %hhu, cipher_type %hhu, key_idx %hhu) failed:%s",
            key_type, key_config->cipher_type(), key_config->key_idx(),
            zx_status_get_string(status));
    return status;
  }

  ieee80211_key_confs_[key_type] = std::move(key_conf);
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

enum iwl_sta_state MvmSta::GetState() const { return iwl_mvm_sta_.get()->sta_state; }

zx_status_t MvmSta::ChangeState(enum iwl_sta_state state) {
  zx_status_t status = ZX_OK;
  while (state > GetState()) {
    if ((status = ChangeStateUp()) != ZX_OK) {
      return status;
    }
  }
  while (state < GetState()) {
    if ((status = ChangeStateDown()) != ZX_OK) {
      return status;
    }
  }
  return ZX_OK;
}

struct iwl_mvm_sta* MvmSta::iwl_mvm_sta() { return iwl_mvm_sta_.get(); }

const struct iwl_mvm_sta* MvmSta::iwl_mvm_sta() const { return iwl_mvm_sta_.get(); }

zx_status_t MvmSta::ChangeStateUp() {
  zx_status_t status = ZX_OK;
  iwl_sta_state new_state = iwl_sta_state::IWL_STA_NOTEXIST;
  switch (GetState()) {
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
      IWL_ERR(iwl_mvm_vif_, "ChangeStateUp() in invalid state %d\n", GetState());
      return ZX_ERR_BAD_STATE;
    }
  }

  if ((status = iwl_mvm_mac_sta_state(iwl_mvm_vif_, iwl_mvm_sta_.get(), GetState(), new_state)) !=
      ZX_OK) {
    IWL_ERR(iwl_mvm_vif_, "iwl_mvm_mac_sta_state() failed for %d -> %d: %s\n", GetState(),
            new_state, zx_status_get_string(status));
    return status;
  }

  return ZX_OK;
}

zx_status_t MvmSta::ChangeStateDown() {
  zx_status_t status = ZX_OK;
  iwl_sta_state new_state = iwl_sta_state::IWL_STA_NOTEXIST;
  switch (GetState()) {
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
      IWL_ERR(iwl_mvm_vif_, "ChangeStateDown() in invalid state %d\n", GetState());
      return ZX_ERR_BAD_STATE;
    }
  }

  if ((status = iwl_mvm_mac_sta_state(iwl_mvm_vif_, iwl_mvm_sta_.get(), GetState(), new_state)) !=
      ZX_OK) {
    IWL_ERR(iwl_mvm_vif_, "iwl_mvm_mac_sta_state() failed for %d -> %d: %s\n", GetState(),
            new_state, zx_status_get_string(status));
    return status;
  }

  return ZX_OK;
}

}  // namespace wlan::iwlwifi
