// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "common_utils.h"

namespace fio2 = llcpp::fuchsia::io2;

zxio_node_protocols_t ToZxioNodeProtocols(fio2::NodeProtocolSet protocols) {
  zxio_node_protocols_t zxio_protocols = ZXIO_NODE_PROTOCOL_NONE;
  if (protocols & fio2::NodeProtocolSet::CONNECTOR) {
    zxio_protocols |= ZXIO_NODE_PROTOCOL_CONNECTOR;
  }
  if (protocols & fio2::NodeProtocolSet::DIRECTORY) {
    zxio_protocols |= ZXIO_NODE_PROTOCOL_DIRECTORY;
  }
  if (protocols & fio2::NodeProtocolSet::FILE) {
    zxio_protocols |= ZXIO_NODE_PROTOCOL_FILE;
  }
  if (protocols & fio2::NodeProtocolSet::MEMORY) {
    zxio_protocols |= ZXIO_NODE_PROTOCOL_MEMORY;
  }
  if (protocols & fio2::NodeProtocolSet::POSIX_SOCKET) {
    zxio_protocols |= ZXIO_NODE_PROTOCOL_POSIX_SOCKET;
  }
  if (protocols & fio2::NodeProtocolSet::PIPE) {
    zxio_protocols |= ZXIO_NODE_PROTOCOL_PIPE;
  }
  if (protocols & fio2::NodeProtocolSet::DEBUGLOG) {
    zxio_protocols |= ZXIO_NODE_PROTOCOL_DEBUGLOG;
  }
  if (protocols & fio2::NodeProtocolSet::DEVICE) {
    zxio_protocols |= ZXIO_NODE_PROTOCOL_DEVICE;
  }
  if (protocols & fio2::NodeProtocolSet::TTY) {
    zxio_protocols |= ZXIO_NODE_PROTOCOL_TTY;
  }
  return zxio_protocols;
}

fio2::NodeProtocolSet ToIo2NodeProtocols(zxio_node_protocols_t zxio_protocols) {
  fio2::NodeProtocolSet protocols = fio2::NodeProtocolSet();
  if (zxio_protocols & ZXIO_NODE_PROTOCOL_CONNECTOR) {
    protocols |= fio2::NodeProtocolSet::CONNECTOR;
  }
  if (zxio_protocols & ZXIO_NODE_PROTOCOL_DIRECTORY) {
    protocols |= fio2::NodeProtocolSet::DIRECTORY;
  }
  if (zxio_protocols & ZXIO_NODE_PROTOCOL_FILE) {
    protocols |= fio2::NodeProtocolSet::FILE;
  }
  if (zxio_protocols & ZXIO_NODE_PROTOCOL_MEMORY) {
    protocols |= fio2::NodeProtocolSet::MEMORY;
  }
  if (zxio_protocols & ZXIO_NODE_PROTOCOL_POSIX_SOCKET) {
    protocols |= fio2::NodeProtocolSet::POSIX_SOCKET;
  }
  if (zxio_protocols & ZXIO_NODE_PROTOCOL_PIPE) {
    protocols |= fio2::NodeProtocolSet::PIPE;
  }
  if (zxio_protocols & ZXIO_NODE_PROTOCOL_DEBUGLOG) {
    protocols |= fio2::NodeProtocolSet::DEBUGLOG;
  }
  if (zxio_protocols & ZXIO_NODE_PROTOCOL_DEVICE) {
    protocols |= fio2::NodeProtocolSet::DEVICE;
  }
  if (zxio_protocols & ZXIO_NODE_PROTOCOL_TTY) {
    protocols |= fio2::NodeProtocolSet::TTY;
  }
  return protocols;
}

zxio_abilities_t ToZxioAbilities(fio2::Operations abilities) {
  zxio_abilities_t zxio_abilities = ZXIO_OPERATION_NONE;
  if (abilities & fio2::Operations::CONNECT) {
    zxio_abilities |= ZXIO_OPERATION_CONNECT;
  }
  if (abilities & fio2::Operations::READ_BYTES) {
    zxio_abilities |= ZXIO_OPERATION_READ_BYTES;
  }
  if (abilities & fio2::Operations::WRITE_BYTES) {
    zxio_abilities |= ZXIO_OPERATION_WRITE_BYTES;
  }
  if (abilities & fio2::Operations::EXECUTE) {
    zxio_abilities |= ZXIO_OPERATION_EXECUTE;
  }
  if (abilities & fio2::Operations::GET_ATTRIBUTES) {
    zxio_abilities |= ZXIO_OPERATION_GET_ATTRIBUTES;
  }
  if (abilities & fio2::Operations::UPDATE_ATTRIBUTES) {
    zxio_abilities |= ZXIO_OPERATION_UPDATE_ATTRIBUTES;
  }
  if (abilities & fio2::Operations::ENUMERATE) {
    zxio_abilities |= ZXIO_OPERATION_ENUMERATE;
  }
  if (abilities & fio2::Operations::TRAVERSE) {
    zxio_abilities |= ZXIO_OPERATION_TRAVERSE;
  }
  if (abilities & fio2::Operations::MODIFY_DIRECTORY) {
    zxio_abilities |= ZXIO_OPERATION_MODIFY_DIRECTORY;
  }
  if (abilities & fio2::Operations::ADMIN) {
    zxio_abilities |= ZXIO_OPERATION_ADMIN;
  }
  return zxio_abilities;
}

fio2::Operations ToIo2Abilities(zxio_abilities_t zxio_abilities) {
  fio2::Operations abilities = fio2::Operations();
  if (zxio_abilities & ZXIO_OPERATION_CONNECT) {
    abilities |= fio2::Operations::CONNECT;
  }
  if (zxio_abilities & ZXIO_OPERATION_READ_BYTES) {
    abilities |= fio2::Operations::READ_BYTES;
  }
  if (zxio_abilities & ZXIO_OPERATION_WRITE_BYTES) {
    abilities |= fio2::Operations::WRITE_BYTES;
  }
  if (zxio_abilities & ZXIO_OPERATION_EXECUTE) {
    abilities |= fio2::Operations::EXECUTE;
  }
  if (zxio_abilities & ZXIO_OPERATION_GET_ATTRIBUTES) {
    abilities |= fio2::Operations::GET_ATTRIBUTES;
  }
  if (zxio_abilities & ZXIO_OPERATION_UPDATE_ATTRIBUTES) {
    abilities |= fio2::Operations::UPDATE_ATTRIBUTES;
  }
  if (zxio_abilities & ZXIO_OPERATION_ENUMERATE) {
    abilities |= fio2::Operations::ENUMERATE;
  }
  if (zxio_abilities & ZXIO_OPERATION_TRAVERSE) {
    abilities |= fio2::Operations::TRAVERSE;
  }
  if (zxio_abilities & ZXIO_OPERATION_MODIFY_DIRECTORY) {
    abilities |= fio2::Operations::MODIFY_DIRECTORY;
  }
  if (zxio_abilities & ZXIO_OPERATION_ADMIN) {
    abilities |= fio2::Operations::ADMIN;
  }
  return abilities;
}
