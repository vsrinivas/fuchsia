// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_NVME_COMMANDS_H_
#define SRC_DEVICES_BLOCK_DRIVERS_NVME_COMMANDS_H_

#include <hwreg/bitfields.h>

namespace nvme {

// NVM Express base specification 2.0, section 3.3.3.1, "Submission Queue Entry"
struct Submission {
  template <typename U>
  constexpr U& GetSubmission() {
    static_assert(std::is_base_of<Submission, U>::value);
    static_assert(sizeof(U) == sizeof(Submission));
    return *reinterpret_cast<U*>(this);
  }

  uint32_t command_dword0;
  uint32_t namespace_id;
  uint32_t command_dword2;
  uint32_t command_dword3;
  uint64_t metadata_pointer;
  uint64_t data_pointer[2];
  // The spec refers to them as "dwordN", so we name them like that too.
  uint32_t dword10;
  uint32_t dword11;
  uint32_t dword12;
  uint32_t dword13;
  uint32_t dword14;
  uint32_t dword15;

  DEF_SUBFIELD(command_dword0, 31, 16, cid);
  DEF_SUBFIELD(command_dword0, 15, 14, data_transfer_mode);
  DEF_SUBFIELD(command_dword0, 9, 8, fused);
  DEF_SUBFIELD(command_dword0, 7, 0, opcode);

  explicit Submission(uint8_t opcode) {
    memset(this, 0, sizeof(*this));
    set_opcode(opcode);
  }
};
static_assert(sizeof(Submission) == 64, "submission struct must be 64 bytes");

enum StatusCodeType {
  kGeneric = 0,
  kCommandSpecific = 1,
  kIntegrityErrors = 2,
  kPathRelated = 3,
  kVendorSpecific = 7,
};
enum GenericStatus {
  kSuccess = 0x0,
  kInvalidOpcode = 0x1,
  kInvalidField = 0x2,
  kCommandIdConflict = 0x3,
  kDataTransferError = 0x4,
  kAbortedDueToPowerLossNotification = 0x5,
  kInternalError = 0x6,
  kAbortRequest = 0x7,
  kSubmissionQueueDeleted = 0x8,
  kFailedFusedCommand = 0x9,
  kMissingFusedCommand = 0xa,
  kInvalidNamespaceOrFormat = 0xb,
  kCommandSequenceError = 0xc,
  kInvalidSglSegmentDescriptor = 0xd,
  kInvalidSglDescriptorCount = 0xe,
  kDataSglLengthInvalid = 0xf,
  kMetadataSglLengthInvalid = 0x10,
  kSglDescriptorTypeInvalid = 0x11,
  kInvalidControllerMemoryUse = 0x12,
  kPrpOffsetInvalid = 0x13,
  kAtomicWriteUnitExceeded = 0x14,
  kOperationDenied = 0x15,
  kSglOffsetInvalid = 0x16,
  kHostIdentifierInconsistentFormat = 0x18,
  kKeepAliveExpired = 0x19,
  kKeepAliveInvalid = 0x1a,
  kAbortedDueToPreemptAndAbort = 0x1b,
  kSanitizeFailed = 0x1c,
  kSanitizeInProgress = 0x1d,
  kSglDataBlockGranularityInvalid = 0x1e,
  kCommandNotSupportedInCmb = 0x1f,
  kNamespaceWriteProtected = 0x20,
  kCommandInterrupted = 0x21,
  kTransientTransportError = 0x22,
  kProhibitedByLockdown = 0x23,
  kMediaNotReady = 0x24,
  kLbaOutOfRange = 0x80,
  kCapacityExceeded = 0x81,
  kNamespaceNotReady = 0x82,
  kReservationConflict = 0x83,
  kFormatInProgress = 0x84,
  kInvalidValueSize = 0x85,
  kInvalidKeySize = 0x86,
  kKeyNotExist = 0x87,
  kUnrecoveredError = 0x88,
  kKeyExists = 0x89,
};

// NVM Express base specification 2.0, section 3.3.3.2, "Common Completion Queue Entry"
struct Completion {
  uint32_t command[2];
  uint32_t dwords[2];

  // dword 2
  DEF_SUBFIELD(dwords[0], 31, 16, sq_id);
  DEF_SUBFIELD(dwords[0], 15, 0, sq_head);

  // dword 3
  DEF_SUBBIT(dwords[1], 31, do_not_retry);
  DEF_SUBBIT(dwords[1], 30, more);
  DEF_SUBFIELD(dwords[1], 29, 28, command_retry_delay);
  DEF_ENUM_SUBFIELD(dwords[1], StatusCodeType, 27, 25, status_code_type);
  DEF_SUBFIELD(dwords[1], 24, 17, status_code);
  DEF_SUBBIT(dwords[1], 16, phase);
  DEF_SUBFIELD(dwords[1], 15, 0, command_id);

} __PACKED;
static_assert(sizeof(Completion) == 16, "completion struct must be 16 bytes");

}  // namespace nvme

#endif  // SRC_DEVICES_BLOCK_DRIVERS_NVME_COMMANDS_H_
