// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_LIB_NETWORK_NETDUMP_H_
#define SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_LIB_NETWORK_NETDUMP_H_

#include <fuchsia/netemul/network/cpp/fidl.h>
#include <src/lib/fxl/macros.h>

#include <iostream>
#include <sstream>

namespace netemul {

class NetworkDump {
 public:
  NetworkDump() : out_(nullptr) {}
  NetworkDump(std::ostream* out) : out_(out) { WriteHeaders(); }
  virtual ~NetworkDump() = default;
  void WritePacket(const void* data, size_t len, uint32_t interface = 0);
  uint32_t AddInterface(const std::string& name);
  size_t packet_count() const { return packet_count_; }

 protected:
  void SetOut(std::ostream* out) {
    ZX_DEBUG_ASSERT(out_ == nullptr);
    ZX_DEBUG_ASSERT(out != nullptr);
    out_ = out;
    WriteHeaders();
  }

 private:
  void Write(const void* data, size_t len);
  void WriteOption(uint16_t type, const void* data, uint16_t len);
  void WriteHeaders();
  std::ostream* out_;
  uint32_t interface_counter_ = 0;
  size_t packet_count_ = 0;

  FXL_DISALLOW_COPY_AND_ASSIGN(NetworkDump);
};

class InMemoryDump : public NetworkDump {
 public:
  InMemoryDump() : NetworkDump(), mem_() { SetOut(&mem_); }

  void DumpHex(std::ostream* out) const;
  std::vector<uint8_t> CopyBytes() const;

 private:
  std::stringstream mem_;

  FXL_DISALLOW_COPY_AND_ASSIGN(InMemoryDump);
};

template <typename D>
class NetWatcher {
 public:
  template <typename... Args>
  explicit NetWatcher(Args... args) : dump_(args...), got_data_(false) {}

  void Watch(const std::string& name,
             fidl::InterfacePtr<fuchsia::netemul::network::FakeEndpoint> ep) {
    auto id = dump_.AddInterface(name);
    ep.events().OnData = [this, id](std::vector<uint8_t> data) {
      got_data_ = true;
      dump_.WritePacket(&data[0], data.size(), id);
    };
    fake_eps_.push_back(std::move(ep));
  }

  bool HasData() const { return got_data_; }

  const D& dump() const { return dump_; }

 private:
  std::vector<fidl::InterfacePtr<fuchsia::netemul::network::FakeEndpoint>>
      fake_eps_;
  D dump_;
  bool got_data_;

  FXL_DISALLOW_COPY_AND_ASSIGN(NetWatcher);
};

}  // namespace netemul

#endif  // SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_LIB_NETWORK_NETDUMP_H_
