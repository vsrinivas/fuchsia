// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FVM_HOST_FVM_RESERVATION_H_
#define SRC_STORAGE_FVM_HOST_FVM_RESERVATION_H_

#include <stdint.h>
#include <stdio.h>

#include <optional>

// Reservation is a request that may or may not be approved. Request for reservation may fail the
// AddPartition or request may be rejected silently. Only way to verify is to check both return
// value and "reserved" field.
struct fvm_reserve_t {
  // How many bytes/inodes needs to be reserved. Serves as input to AddPartition.
  std::optional<uint64_t> request;

  // How many bytes/inodes were reserved. Serves as output of AddPartition. Depending on
  // filesystems, more than request may be reserved.
  uint64_t reserved;
};

class FvmReservation {
 public:
  FvmReservation() {}
  FvmReservation(std::optional<uint64_t> inode_count, std::optional<uint64_t> data,
                 std::optional<uint64_t> total_bytes) {
    nodes_.request = inode_count;
    data_.request = data;
    total_bytes_.request = total_bytes;
  }

  // Returns true if all parts of the request are approved.
  bool Approved() const;

  void Dump(FILE* stream) const;

  fvm_reserve_t inodes() const { return nodes_; }

  fvm_reserve_t total_bytes() const { return total_bytes_; }

  fvm_reserve_t data() const { return data_; }

  void set_inodes_reserved(uint64_t reserved) { nodes_.reserved = reserved; }

  void set_data_reserved(uint64_t reserved) { data_.reserved = reserved; }

  void set_total_bytes_reserved(uint64_t reserved) { total_bytes_.reserved = reserved; }

 private:
  // Reserve number of files/directory that can be created.
  fvm_reserve_t nodes_ = {};

  // Raw bytes for "data" that needs to be reserved.
  fvm_reserve_t data_ = {};

  // Byte limit on the reservation. Zero value implies limitless. If set,
  // over-committing will fail. Return value contains total bytes reserved.
  fvm_reserve_t total_bytes_ = {};
};

#endif  // SRC_STORAGE_FVM_HOST_FVM_RESERVATION_H_
