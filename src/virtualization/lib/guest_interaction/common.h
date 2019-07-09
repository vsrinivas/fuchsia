// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_COMMON_H_
#define SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_COMMON_H_

#include <sys/socket.h>

#include "src/virtualization/lib/guest_interaction/proto/guest_interaction.grpc.pb.h"

#define GUEST_INTERACTION_PORT 9999
#define CHUNK_SIZE 1024
#define VMADDR_CID_ANY -1U

class CallData {
 public:
  virtual ~CallData() {}
  virtual void Proceed(bool ok) = 0;
};

// Defined in Linux:include/uapi/linux/vm_sockets.h
typedef struct sockaddr_vm {
  sa_family_t svm_family;
  unsigned short svm_reserved1;
  unsigned int svm_port;
  unsigned int svm_cid;
  unsigned char svm_zero[sizeof(struct sockaddr) - sizeof(sa_family_t) - sizeof(unsigned short) -
                         sizeof(unsigned int) - sizeof(unsigned int)];
} sockaddr_vm;

#endif  // SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_COMMON_H_
