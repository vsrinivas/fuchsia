// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "common_utils.h"

namespace fio = fuchsia_io;

using fio::wire::NodeProtocolKinds;
using fio::wire::Operations;

zxio_node_protocols_t ToZxioNodeProtocolKinds(NodeProtocolKinds protocols) {
  zxio_node_protocols_t zxio_protocols = ZXIO_NODE_PROTOCOL_NONE;
  if (protocols & NodeProtocolKinds::kConnector) {
    zxio_protocols |= ZXIO_NODE_PROTOCOL_CONNECTOR;
  }
  if (protocols & NodeProtocolKinds::kDirectory) {
    zxio_protocols |= ZXIO_NODE_PROTOCOL_DIRECTORY;
  }
  if (protocols & NodeProtocolKinds::kFile) {
    zxio_protocols |= ZXIO_NODE_PROTOCOL_FILE;
  }

  return zxio_protocols;
}

NodeProtocolKinds ToIo2NodeProtocolKinds(zxio_node_protocols_t zxio_protocols) {
  NodeProtocolKinds protocols = NodeProtocolKinds();
  if (zxio_protocols & ZXIO_NODE_PROTOCOL_CONNECTOR) {
    protocols |= NodeProtocolKinds::kConnector;
  }
  if (zxio_protocols & ZXIO_NODE_PROTOCOL_DIRECTORY) {
    protocols |= NodeProtocolKinds::kDirectory;
  }
  if (zxio_protocols & ZXIO_NODE_PROTOCOL_FILE) {
    protocols |= NodeProtocolKinds::kFile;
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
