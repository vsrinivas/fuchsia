// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "common_utils.h"

namespace fio = fuchsia_io;

using fio::wire::NodeProtocols;
using fio::wire::Operations;

zxio_node_protocols_t ToZxioNodeProtocols(NodeProtocols protocols) {
  zxio_node_protocols_t zxio_protocols = ZXIO_NODE_PROTOCOL_NONE;
  if (protocols & NodeProtocols::kConnector) {
    zxio_protocols |= ZXIO_NODE_PROTOCOL_CONNECTOR;
  }
  if (protocols & NodeProtocols::kDirectory) {
    zxio_protocols |= ZXIO_NODE_PROTOCOL_DIRECTORY;
  }
  if (protocols & NodeProtocols::kFile) {
    zxio_protocols |= ZXIO_NODE_PROTOCOL_FILE;
  }
  if (protocols & NodeProtocols::kMemory) {
    zxio_protocols |= ZXIO_NODE_PROTOCOL_MEMORY;
  }
  if (protocols & NodeProtocols::kPipe) {
    zxio_protocols |= ZXIO_NODE_PROTOCOL_PIPE;
  }
  if (protocols & NodeProtocols::kDatagramSocket) {
    zxio_protocols |= ZXIO_NODE_PROTOCOL_DATAGRAM_SOCKET;
  }
  if (protocols & NodeProtocols::kStreamSocket) {
    zxio_protocols |= ZXIO_NODE_PROTOCOL_STREAM_SOCKET;
  }
  if (protocols & NodeProtocols::kRawSocket) {
    zxio_protocols |= ZXIO_NODE_PROTOCOL_RAW_SOCKET;
  }

  if (protocols & NodeProtocols::kDevice) {
    zxio_protocols |= ZXIO_NODE_PROTOCOL_DEVICE;
  }
  if (protocols & NodeProtocols::kTty) {
    zxio_protocols |= ZXIO_NODE_PROTOCOL_TTY;
  }
  return zxio_protocols;
}

NodeProtocols ToIo2NodeProtocols(zxio_node_protocols_t zxio_protocols) {
  NodeProtocols protocols = NodeProtocols();
  if (zxio_protocols & ZXIO_NODE_PROTOCOL_CONNECTOR) {
    protocols |= NodeProtocols::kConnector;
  }
  if (zxio_protocols & ZXIO_NODE_PROTOCOL_DIRECTORY) {
    protocols |= NodeProtocols::kDirectory;
  }
  if (zxio_protocols & ZXIO_NODE_PROTOCOL_FILE) {
    protocols |= NodeProtocols::kFile;
  }
  if (zxio_protocols & ZXIO_NODE_PROTOCOL_MEMORY) {
    protocols |= NodeProtocols::kMemory;
  }
  if (zxio_protocols & ZXIO_NODE_PROTOCOL_PIPE) {
    protocols |= NodeProtocols::kPipe;
  }
  if (zxio_protocols & ZXIO_NODE_PROTOCOL_DATAGRAM_SOCKET) {
    protocols |= NodeProtocols::kDatagramSocket;
  }
  if (zxio_protocols & ZXIO_NODE_PROTOCOL_STREAM_SOCKET) {
    protocols |= NodeProtocols::kStreamSocket;
  }
  if (zxio_protocols & ZXIO_NODE_PROTOCOL_RAW_SOCKET) {
    protocols |= NodeProtocols::kRawSocket;
  }

  if (zxio_protocols & ZXIO_NODE_PROTOCOL_DEVICE) {
    protocols |= NodeProtocols::kDevice;
  }
  if (zxio_protocols & ZXIO_NODE_PROTOCOL_TTY) {
    protocols |= NodeProtocols::kTty;
  }
  return protocols;
}

zxio_abilities_t ToZxioAbilities(Operations abilities) {
  zxio_abilities_t zxio_abilities = ZXIO_OPERATION_NONE;
  if (abilities & Operations::kConnect) {
    zxio_abilities |= ZXIO_OPERATION_CONNECT;
  }
  if (abilities & Operations::kReadBytes) {
    zxio_abilities |= ZXIO_OPERATION_READ_BYTES;
  }
  if (abilities & Operations::kWriteBytes) {
    zxio_abilities |= ZXIO_OPERATION_WRITE_BYTES;
  }
  if (abilities & Operations::kExecute) {
    zxio_abilities |= ZXIO_OPERATION_EXECUTE;
  }
  if (abilities & Operations::kGetAttributes) {
    zxio_abilities |= ZXIO_OPERATION_GET_ATTRIBUTES;
  }
  if (abilities & Operations::kUpdateAttributes) {
    zxio_abilities |= ZXIO_OPERATION_UPDATE_ATTRIBUTES;
  }
  if (abilities & Operations::kEnumerate) {
    zxio_abilities |= ZXIO_OPERATION_ENUMERATE;
  }
  if (abilities & Operations::kTraverse) {
    zxio_abilities |= ZXIO_OPERATION_TRAVERSE;
  }
  if (abilities & Operations::kModifyDirectory) {
    zxio_abilities |= ZXIO_OPERATION_MODIFY_DIRECTORY;
  }
  return zxio_abilities;
}

Operations ToIo2Abilities(zxio_abilities_t zxio_abilities) {
  Operations abilities = Operations();
  if (zxio_abilities & ZXIO_OPERATION_CONNECT) {
    abilities |= Operations::kConnect;
  }
  if (zxio_abilities & ZXIO_OPERATION_READ_BYTES) {
    abilities |= Operations::kReadBytes;
  }
  if (zxio_abilities & ZXIO_OPERATION_WRITE_BYTES) {
    abilities |= Operations::kWriteBytes;
  }
  if (zxio_abilities & ZXIO_OPERATION_EXECUTE) {
    abilities |= Operations::kExecute;
  }
  if (zxio_abilities & ZXIO_OPERATION_GET_ATTRIBUTES) {
    abilities |= Operations::kGetAttributes;
  }
  if (zxio_abilities & ZXIO_OPERATION_UPDATE_ATTRIBUTES) {
    abilities |= Operations::kUpdateAttributes;
  }
  if (zxio_abilities & ZXIO_OPERATION_ENUMERATE) {
    abilities |= Operations::kEnumerate;
  }
  if (zxio_abilities & ZXIO_OPERATION_TRAVERSE) {
    abilities |= Operations::kTraverse;
  }
  if (zxio_abilities & ZXIO_OPERATION_MODIFY_DIRECTORY) {
    abilities |= Operations::kModifyDirectory;
  }
  return abilities;
}
