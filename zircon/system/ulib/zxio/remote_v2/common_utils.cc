// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "common_utils.h"

namespace fio2 = fuchsia_io2;

using fio2::wire::NodeProtocols;
using fio2::wire::Operations;

zxio_node_protocols_t ToZxioNodeProtocols(NodeProtocols protocols) {
  zxio_node_protocols_t zxio_protocols = ZXIO_NODE_PROTOCOL_NONE;
  if (protocols & NodeProtocols::CONNECTOR) {
    zxio_protocols |= ZXIO_NODE_PROTOCOL_CONNECTOR;
  }
  if (protocols & NodeProtocols::DIRECTORY) {
    zxio_protocols |= ZXIO_NODE_PROTOCOL_DIRECTORY;
  }
  if (protocols & NodeProtocols::FILE) {
    zxio_protocols |= ZXIO_NODE_PROTOCOL_FILE;
  }
  if (protocols & NodeProtocols::MEMORY) {
    zxio_protocols |= ZXIO_NODE_PROTOCOL_MEMORY;
  }
  if (protocols & NodeProtocols::POSIX_SOCKET) {
    zxio_protocols |= ZXIO_NODE_PROTOCOL_POSIX_SOCKET;
  }
  if (protocols & NodeProtocols::PIPE) {
    zxio_protocols |= ZXIO_NODE_PROTOCOL_PIPE;
  }
  if (protocols & NodeProtocols::DEBUGLOG) {
    zxio_protocols |= ZXIO_NODE_PROTOCOL_DEBUGLOG;
  }
  if (protocols & NodeProtocols::DEVICE) {
    zxio_protocols |= ZXIO_NODE_PROTOCOL_DEVICE;
  }
  if (protocols & NodeProtocols::TTY) {
    zxio_protocols |= ZXIO_NODE_PROTOCOL_TTY;
  }
  return zxio_protocols;
}

NodeProtocols ToIo2NodeProtocols(zxio_node_protocols_t zxio_protocols) {
  NodeProtocols protocols = NodeProtocols();
  if (zxio_protocols & ZXIO_NODE_PROTOCOL_CONNECTOR) {
    protocols |= NodeProtocols::CONNECTOR;
  }
  if (zxio_protocols & ZXIO_NODE_PROTOCOL_DIRECTORY) {
    protocols |= NodeProtocols::DIRECTORY;
  }
  if (zxio_protocols & ZXIO_NODE_PROTOCOL_FILE) {
    protocols |= NodeProtocols::FILE;
  }
  if (zxio_protocols & ZXIO_NODE_PROTOCOL_MEMORY) {
    protocols |= NodeProtocols::MEMORY;
  }
  if (zxio_protocols & ZXIO_NODE_PROTOCOL_POSIX_SOCKET) {
    protocols |= NodeProtocols::POSIX_SOCKET;
  }
  if (zxio_protocols & ZXIO_NODE_PROTOCOL_PIPE) {
    protocols |= NodeProtocols::PIPE;
  }
  if (zxio_protocols & ZXIO_NODE_PROTOCOL_DEBUGLOG) {
    protocols |= NodeProtocols::DEBUGLOG;
  }

  if (zxio_protocols & ZXIO_NODE_PROTOCOL_DEVICE) {
    protocols |= NodeProtocols::DEVICE;
  }
  if (zxio_protocols & ZXIO_NODE_PROTOCOL_TTY) {
    protocols |= NodeProtocols::TTY;
  }
  return protocols;
}

zxio_abilities_t ToZxioAbilities(Operations abilities) {
  zxio_abilities_t zxio_abilities = ZXIO_OPERATION_NONE;
  if (abilities & Operations::CONNECT) {
    zxio_abilities |= ZXIO_OPERATION_CONNECT;
  }
  if (abilities & Operations::READ_BYTES) {
    zxio_abilities |= ZXIO_OPERATION_READ_BYTES;
  }
  if (abilities & Operations::WRITE_BYTES) {
    zxio_abilities |= ZXIO_OPERATION_WRITE_BYTES;
  }
  if (abilities & Operations::EXECUTE) {
    zxio_abilities |= ZXIO_OPERATION_EXECUTE;
  }
  if (abilities & Operations::GET_ATTRIBUTES) {
    zxio_abilities |= ZXIO_OPERATION_GET_ATTRIBUTES;
  }
  if (abilities & Operations::UPDATE_ATTRIBUTES) {
    zxio_abilities |= ZXIO_OPERATION_UPDATE_ATTRIBUTES;
  }
  if (abilities & Operations::ENUMERATE) {
    zxio_abilities |= ZXIO_OPERATION_ENUMERATE;
  }
  if (abilities & Operations::TRAVERSE) {
    zxio_abilities |= ZXIO_OPERATION_TRAVERSE;
  }
  if (abilities & Operations::MODIFY_DIRECTORY) {
    zxio_abilities |= ZXIO_OPERATION_MODIFY_DIRECTORY;
  }
  if (abilities & Operations::ADMIN) {
    zxio_abilities |= ZXIO_OPERATION_ADMIN;
  }
  return zxio_abilities;
}

Operations ToIo2Abilities(zxio_abilities_t zxio_abilities) {
  Operations abilities = Operations();
  if (zxio_abilities & ZXIO_OPERATION_CONNECT) {
    abilities |= Operations::CONNECT;
  }
  if (zxio_abilities & ZXIO_OPERATION_READ_BYTES) {
    abilities |= Operations::READ_BYTES;
  }
  if (zxio_abilities & ZXIO_OPERATION_WRITE_BYTES) {
    abilities |= Operations::WRITE_BYTES;
  }
  if (zxio_abilities & ZXIO_OPERATION_EXECUTE) {
    abilities |= Operations::EXECUTE;
  }
  if (zxio_abilities & ZXIO_OPERATION_GET_ATTRIBUTES) {
    abilities |= Operations::GET_ATTRIBUTES;
  }
  if (zxio_abilities & ZXIO_OPERATION_UPDATE_ATTRIBUTES) {
    abilities |= Operations::UPDATE_ATTRIBUTES;
  }
  if (zxio_abilities & ZXIO_OPERATION_ENUMERATE) {
    abilities |= Operations::ENUMERATE;
  }
  if (zxio_abilities & ZXIO_OPERATION_TRAVERSE) {
    abilities |= Operations::TRAVERSE;
  }
  if (zxio_abilities & ZXIO_OPERATION_MODIFY_DIRECTORY) {
    abilities |= Operations::MODIFY_DIRECTORY;
  }
  if (zxio_abilities & ZXIO_OPERATION_ADMIN) {
    abilities |= Operations::ADMIN;
  }
  return abilities;
}
