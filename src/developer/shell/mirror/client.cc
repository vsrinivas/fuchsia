// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/mirror/client.h"

#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>

#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/developer/shell/mirror/wire_format.h"

namespace shell::mirror::client {

namespace {

// Grabbed from zxdb/client code

// Tries to resolve the host/port. On success populates *addr and returns Err().
Err ResolveAddress(const std::string& host, uint16_t port, addrinfo** addr) {
  std::string port_str = std::to_string(port);

  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  int addr_err = getaddrinfo(host.c_str(), port_str.c_str(), &hints, addr);
  if (addr_err != 0) {
    return Err(kConnection, "Failed to resolve " + host + ": " + gai_strerror(addr_err));
  }

  return Err();
}

Err ConnectToHost(const std::string& host, uint16_t port, fbl::unique_fd* socket_out) {
  struct addrinfo* addr;
  Err err = ResolveAddress(host, port, &addr);
  if (err.code != 0) {
    return err;
  }

  for (struct addrinfo* a = addr; a != nullptr; a = a->ai_next) {
    fbl::unique_fd res_socket;

    res_socket.reset(socket(a->ai_family, SOCK_STREAM, IPPROTO_TCP));
    if (!res_socket.is_valid()) {
      err.msg = std::string("Could not create socket: ") + strerror(errno);
      err.code = kConnection;
      continue;
    }

    if (connect(res_socket.get(), a->ai_addr, a->ai_addrlen)) {
      err.msg = std::string("Could not connect socket: ") + strerror(errno);
      err.code = kConnection;
      continue;
    }

    *socket_out = std::move(res_socket);
    freeaddrinfo(addr);
    return Err();
  }

  freeaddrinfo(addr);
  return err;
}

// grabbed from zxdb/common/inet_util

Err ParseHostPort(const std::string& in_host, const std::string& in_port, std::string* out_host,
                  uint16_t* out_port) {
  if (in_host.empty()) {
    return Err(kConnection, "No host component specified.");
  }
  if (in_port.empty()) {
    return Err(kConnection, "No port component specified.");
  }

  // Trim brackets from the host name for IPv6 addresses.
  if (in_host.front() == '[' && in_host.back() == ']') {
    *out_host = in_host.substr(1, in_host.size() - 2);
  } else {
    *out_host = in_host;
  }

  const char* port_p = in_port.c_str();
  char* port_ep = nullptr;
  unsigned long int res = strtoul(port_p, &port_ep, 10);
  if (*port_ep != '\0') {
    return Err(kConnection, std::string("Invalid port number: ") + in_port);
  }
  if (res > 65535 || res < 0) {
    return Err(kConnection, "Port value out of range.");
  }
  *out_port = static_cast<uint16_t>(res);

  return Err();
}

Err ParseHostPort(const std::string& input, std::string* out_host, uint16_t* out_port) {
  // Separate based on the last colon.
  size_t colon = input.rfind(':');
  if (colon == std::string::npos) {
    return Err(kConnection, "Expected colon to separate host/port.");
  }

  // If the host has a colon in it, it could be an IPv6 address. In this case,
  // require brackets around it to differentiate the case where people
  // supplied an IPv6 address and we just picked out the last component above.
  std::string host = input.substr(0, colon);
  if (host.empty()) {
    return Err(kConnection, "No host component specified.");
  }
  if (host.find(':') != std::string::npos) {
    if (host.front() != '[' || host.back() != ']') {
      return Err(kConnection, "Missing brackets enclosing IPv6 address, e.g., \"[::1]:1234\".");
    }
  }

  std::string port = input.substr(colon + 1);

  return ParseHostPort(host, port, out_host, out_port);
}

bool Ipv6HostPortIsMissingBrackets(const std::string& input) {
  size_t colon = input.rfind(':');
  if (colon == std::string::npos) {
    return false;
  }
  std::string host = input.substr(0, colon);
  if (host.empty()) {
    return false;
  }
  if (host.find(':') == std::string::npos) {
    return false;
  }
  return host.front() != '[' || host.back() != ']';
}

Err GetHostPort(const std::string& host_port, std::string* host, uint16_t* port) {
  if (Ipv6HostPortIsMissingBrackets(host_port)) {
    return Err(kConnection,
               "For IPv6 addresses use either: \"[::1]:1234\"\n"
               "or the two-parameter form: \"::1 1234.");
  }
  return ParseHostPort(host_port, host, port);
}

}  // namespace

Err ClientConnection::Init(const std::string& host_and_port) {
  std::string host;
  uint16_t port = 0;
  Err err = GetHostPort(host_and_port, &host, &port);
  if (err.code != 0) {
    return err;
  }

  err = ConnectToHost(host, port, &socket_);
  if (err.code != 0) {
    return err;
  }

  return Err();
}

Err ClientConnection::KillServer() {
  if (write(socket_.get(), remote_commands::kQuitCommand,
            strlen(remote_commands::kQuitCommand) + 1) == -1) {
    std::string error = "Unable to write to server: " + std::string(strerror(errno));
    return Err(kWrite, error);
  }
  return Err();
}

Err ClientConnection::Load(Files* files, struct timeval* timeout) {
  if (write(socket_.get(), remote_commands::kFilesCommand,
            strlen(remote_commands::kFilesCommand) + 1) == -1) {
    std::string error = "Unable to write to server: " + std::string(strerror(errno));
    return Err(kWrite, error);
  }

  Err error;
  *files = Files::FilesFromFD(socket_.get(), &error, timeout);
  if (!error.ok()) {
    return error;
  }

  return Err();
}

}  // namespace shell::mirror::client
