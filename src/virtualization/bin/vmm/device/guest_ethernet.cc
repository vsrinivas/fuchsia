// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "guest_ethernet.h"

#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/fifo.h>

static constexpr uint32_t kMtu = 1500;

zx_status_t GuestEthernet::Send(void* data, size_t length) {
  if (!io_vmo_) {
    FX_LOGS(ERROR) << "Send called before IO buffer was set up";
    return ZX_ERR_BAD_STATE;
  }

  if (rx_fifo_wait_.is_pending()) {
    return ZX_ERR_SHOULD_WAIT;
  }

  if (rx_entries_count_ == 0) {
    size_t count;
    zx_status_t status =
        rx_fifo_.read(sizeof(eth_fifo_entry_t), rx_entries_.data(), rx_entries_.size(), &count);
    if (status == ZX_ERR_SHOULD_WAIT) {
      status = rx_fifo_wait_.Begin(async_get_default_dispatcher());
      FX_CHECK(status == ZX_OK) << "Failed to wait on rx fifo: " << status;
      return ZX_ERR_SHOULD_WAIT;
    } else if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to read from rx fifo: " << status;
      return status;
    }
    rx_entries_count_ = count;
  }

  rx_entries_count_--;
  eth_fifo_entry_t entry = rx_entries_[rx_entries_count_];
  if (entry.offset >= io_size_ || entry.length > (io_size_ - entry.offset) ||
      length > entry.length) {
    FX_LOGS(ERROR) << "Invalid fifo entry for packet";
    entry.length = 0;
    entry.flags = ETH_FIFO_INVALID;
  } else {
    memcpy(reinterpret_cast<void*>(io_addr_ + entry.offset), data, length);
    entry.length = length;
    entry.flags = ETH_FIFO_RX_OK;
  }

  zx_status_t status =
      rx_fifo_.write(sizeof(eth_fifo_entry_t), &entry, 1, nullptr /* actual count */);

  // There are a fixed number of entries in the system and if we read an entry out of the fifo then
  // there should be enough space to write it back. However, if some transient error causes the fifo
  // to not be writable then we should block here to avoid losing track of the fifo entry. Assuming
  // that the netstack is behaving correctly then this should not deadlock.
  if (status == ZX_ERR_SHOULD_WAIT) {
    FX_LOGS(WARNING) << "Rx fifo is not writable. Guest ethernet will block.";
    zx_signals_t pending;
    rx_fifo_.wait_one(ZX_FIFO_WRITABLE, zx::time::infinite(), &pending);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to wait on rx fifo: " << status;
      return status;
    }
    status = rx_fifo_.write(sizeof(eth_fifo_entry_t), &entry, 1, nullptr /* actual count */);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to write to rx fifo after waiting: " << status;
      return status;
    }
  }

  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to write to rx fifo " << status;
    return status;
  }
  return ZX_OK;
}

void GuestEthernet::OnRxFifoReadable(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                     zx_status_t status, const zx_packet_signal_t* signal) {
  FX_CHECK(status == ZX_OK) << "Wait for rx fifo readable failed " << status;
  device_->ReadyToSend();
}

void GuestEthernet::OnTxFifoReadable(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                     zx_status_t status, const zx_packet_signal_t* signal) {
  FX_CHECK(status == ZX_OK) << "Wait for tx fifo readable failed " << status;
  std::vector<eth_fifo_entry_t> entries(kVirtioNetQueueSize / 2);
  size_t count;
  while (true) {
    status = tx_fifo_.read(sizeof(eth_fifo_entry_t), entries.data(), entries.size(), &count);
    if (status == ZX_ERR_SHOULD_WAIT) {
      status = tx_fifo_wait_.Begin(async_get_default_dispatcher());
      FX_CHECK(status == ZX_OK) << "Failed to wait on tx fifo";
      return;
    }
    FX_CHECK(status == ZX_OK) << "Failed to read tx fifo";
    for (size_t i = 0; i != count; ++i) {
      device_->Receive(io_addr_ + entries[i].offset, entries[i].length, entries[i]);
    }
  }
}

void GuestEthernet::Complete(const eth_fifo_entry_t& entry) {
  size_t count;
  zx_status_t status = tx_fifo_.write(sizeof(eth_fifo_entry_t), &entry, 1, &count);
  FX_CHECK(status == ZX_OK);
  FX_CHECK(count == 1);
}

void GuestEthernet::GetInfo(GetInfoCallback callback) {
  fuchsia::hardware::ethernet::Info info;
  info.features = fuchsia::hardware::ethernet::Features::SYNTHETIC;
  info.mtu = kMtu;
  fuchsia::hardware::ethernet::MacAddress mac_address = device_->GetMacAddress();
  memcpy(&info.mac, mac_address.octets.data(), mac_address.octets.size());
  callback(info);
}

void GuestEthernet::GetFifos(GetFifosCallback callback) {
  auto fifos = std::make_unique<fuchsia::hardware::ethernet::Fifos>();
  zx_status_t status = zx::fifo::create(kVirtioNetQueueSize, sizeof(eth_fifo_entry_t),
                                        /* options */ 0u, &fifos->rx, &rx_fifo_);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to create fifo";
    callback(status, nullptr);
    return;
  }
  status = zx::fifo::create(kVirtioNetQueueSize, sizeof(eth_fifo_entry_t),
                            /* options */ 0u, &fifos->tx, &tx_fifo_);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to create fifo";
    FX_CHECK(rx_fifo_.release() == ZX_OK) << "Failed to release fifo";
    callback(status, nullptr);
    return;
  }
  fifos->rx_depth = kVirtioNetQueueSize;
  fifos->tx_depth = kVirtioNetQueueSize;
  callback(ZX_OK, std::move(fifos));
}

void GuestEthernet::SetIOBuffer(zx::vmo vmo, SetIOBufferCallback callback) {
  if (io_vmo_) {
    callback(ZX_ERR_ALREADY_BOUND);
    return;
  }
  uint64_t vmo_size;
  zx_status_t status = vmo.get_size(&vmo_size);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to get vmo size";
    callback(status);
    return;
  }
  status = zx::vmar::root_self()->map(
      0, vmo, 0, vmo_size, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_REQUIRE_NON_RESIZABLE,
      &io_addr_);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to map io buffer";
    callback(status);
    return;
  }
  io_vmo_ = std::move(vmo);
  io_size_ = vmo_size;
  callback(ZX_OK);
}

void GuestEthernet::Start(StartCallback callback) {
  if (!io_vmo_) {
    FX_LOGS(ERROR) << "Start called before IO buffer was set up";
    callback(ZX_ERR_BAD_STATE);
    return;
  }

  // Send a signal to netstack so that it knows to bring the link up.
  tx_fifo_.signal(0, ZX_USER_SIGNAL_0);

  tx_fifo_wait_.set_object(tx_fifo_.get());
  tx_fifo_wait_.set_trigger(ZX_FIFO_READABLE);
  zx_status_t status = tx_fifo_wait_.Begin(async_get_default_dispatcher());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to wait on tx fifo";
  }

  rx_fifo_wait_.set_object(rx_fifo_.get());
  rx_fifo_wait_.set_trigger(ZX_FIFO_READABLE);

  callback(status);
}

void GuestEthernet::Stop(StopCallback callback) { callback(); }

void GuestEthernet::ListenStart(ListenStartCallback callback) { callback(ZX_ERR_NOT_SUPPORTED); }

void GuestEthernet::ListenStop(ListenStopCallback callback) { callback(); }

void GuestEthernet::SetClientName(std::string name, SetClientNameCallback callback) {
  FX_LOGS(INFO) << "Guest ethernet client set to " << name;
  callback(ZX_OK);
}

void GuestEthernet::GetStatus(GetStatusCallback callback) {
  callback(fuchsia::hardware::ethernet::DeviceStatus::ONLINE);
}

void GuestEthernet::SetPromiscuousMode(bool enabled, SetPromiscuousModeCallback callback) {
  callback(ZX_OK);
}

void GuestEthernet::ConfigMulticastAddMac(fuchsia::hardware::ethernet::MacAddress addr,
                                          ConfigMulticastAddMacCallback callback) {
  callback(ZX_ERR_NOT_SUPPORTED);
}

void GuestEthernet::ConfigMulticastDeleteMac(fuchsia::hardware::ethernet::MacAddress addr,
                                             ConfigMulticastDeleteMacCallback callback) {
  callback(ZX_ERR_NOT_SUPPORTED);
}

void GuestEthernet::ConfigMulticastSetPromiscuousMode(
    bool enabled, ConfigMulticastSetPromiscuousModeCallback callback) {
  callback(ZX_OK);
}

void GuestEthernet::ConfigMulticastTestFilter(ConfigMulticastTestFilterCallback callback) {
  callback(ZX_ERR_NOT_SUPPORTED);
}

void GuestEthernet::DumpRegisters(DumpRegistersCallback callback) {
  callback(ZX_ERR_NOT_SUPPORTED);
}
