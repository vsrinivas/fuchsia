// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/trace/utils.h"

#include <errno.h>
#include <lib/syslog/cpp/macros.h>
#include <netdb.h>
#include <stddef.h>
#include <string.h>
#include <sys/socket.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>

#include <fstream>
#include <string>
#include <utility>

#include <fbl/unique_fd.h>
#include <src/lib/files/path.h>
#include <third_party/zlib/contrib/iostream3/zfstream.h>

#include "src/lib/fxl/strings/trim.h"

namespace tracing {

namespace {

const char kTcpPrefix[] = "tcp:";

}  // namespace

bool BeginsWith(const std::string_view& str, const std::string_view& prefix,
                std::string_view* rest) {
  size_t prefix_size = prefix.size();
  if (str.size() < prefix_size)
    return false;
  if (str.substr(0, prefix_size) != prefix)
    return false;
  *rest = str.substr(prefix_size);
  return true;
}

OptionStatus ParseBooleanOption(const fxl::CommandLine& command_line, const char* name,
                                bool* out_value) {
  std::string arg;
  bool have_option = command_line.GetOptionValue(std::string_view(name), &arg);

  if (!have_option) {
    return OptionStatus::NOT_PRESENT;
  }

  if (arg == "" || arg == "true") {
    *out_value = true;
  } else if (arg == "false") {
    *out_value = false;
  } else {
    FX_LOGS(ERROR) << "Bad value for --" << name << " option, pass true or false";
    return OptionStatus::ERROR;
  }

  return OptionStatus::PRESENT;
}

static bool TcpAddrFromString(std::string_view address, std::string_view port, addrinfo* out_addr) {
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;

  addrinfo* addrinfos;
  int errcode =
      getaddrinfo(std::string(address).c_str(), std::string(port).c_str(), &hints, &addrinfos);
  if (errcode != 0) {
    FX_LOGS(ERROR) << "Failed to getaddrinfo for address " << address << ":" << port << ": "
                   << gai_strerror(errcode);
    return false;
  }
  if (addrinfos == nullptr) {
    FX_LOGS(ERROR) << "No matching addresses found for " << address << ":" << port;
    return false;
  }

  *out_addr = *addrinfos;
  freeaddrinfo(addrinfos);
  return true;
}

static std::unique_ptr<std::ostream> ConnectToTraceSaver(const std::string_view& address) {
  FX_LOGS(INFO) << "Connecting to " << address;

  size_t colon = address.rfind(':');
  if (colon == address.npos) {
    FX_LOGS(ERROR) << "TCP address is missing port: " << address;
    return nullptr;
  }

  std::string_view ip_addr_str(address.substr(0, colon));
  std::string_view port_str(address.substr(colon + 1));

  // [::1] -> ::1
  ip_addr_str = fxl::TrimString(ip_addr_str, "[]");

  addrinfo tcp_addr;
  if (!TcpAddrFromString(ip_addr_str, port_str, &tcp_addr)) {
    return nullptr;
  }

  fbl::unique_fd fd(socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP));
  if (!fd.is_valid()) {
    FX_LOGS(ERROR) << "Failed to create socket: " << strerror(errno);
    return nullptr;
  }

  if (connect(fd.get(), tcp_addr.ai_addr, tcp_addr.ai_addrlen) < 0) {
    FX_LOGS(ERROR) << "Failed to connect: " << strerror(errno);
    return nullptr;
  }

  auto ofstream = std::make_unique<std::ofstream>();
  ofstream->__open(fd.release(), std::ios_base::out);
  FX_DCHECK(ofstream->is_open());
  return ofstream;
}

std::unique_ptr<std::ostream> OpenOutputStream(const std::string& output_file_name, bool compress) {
  std::unique_ptr<std::ostream> out_stream;
  std::string_view address;
  if (BeginsWith(output_file_name, kTcpPrefix, &address)) {
    out_stream = ConnectToTraceSaver(address);
  } else if (compress) {
    // TODO(dje): Compressing a network stream is not supported.
    auto gzstream = std::make_unique<gzofstream>(output_file_name.c_str(),
                                                 std::ios_base::out | std::ios_base::trunc);
    if (gzstream->is_open()) {
      out_stream = std::move(gzstream);
    }
  } else {
    auto ofstream = std::make_unique<std::ofstream>(output_file_name,
                                                    std::ios_base::out | std::ios_base::trunc);
    if (ofstream->is_open()) {
      out_stream = std::move(ofstream);
    }
  }
  return out_stream;
}

}  // namespace tracing
