// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_NVME_COMMANDS_FEATURES_H_
#define SRC_DEVICES_BLOCK_DRIVERS_NVME_COMMANDS_FEATURES_H_

#include <hwreg/bitfields.h>

#include "src/devices/block/drivers/nvme/commands.h"

namespace nvme {

// NVM Express Base Specification 2.0, section 5.27 "Set Features command".
enum Feature {
  kFeatureArbitration = 0x1,
  kFeaturePowerManagement = 0x2,
  kFeatureLbaRangeType = 0x3,
  kFeatureTemperatureThreshold = 0x4,
  kFeatureVolatileWriteCache = 0x6,
  kFeatureNumberOfQueues = 0x7,
  kFeatureInterruptCoalescing = 0x8,
  kFeatureInterruptVectorConfiguration = 0x9,
  kFeatureAsynchronousEventConfiguration = 0xb,
  kFeatureAutonomousPowerStateTransition = 0xc,
  kFeatureHostMemoryBuffer = 0xd,
  kFeatureTimestamp = 0xe,
  kFeatureKeepAliveTimer = 0xf,
  kFeatureHostControlledThermalManagement = 0x10,
  kFeatureNonOperationalPowerStateConfig = 0x11,
  kFeatureReadRecoveryLevelConfig = 0x12,
  kFeaturePredictableLatencyModeConfig = 0x13,
  kFeaturePredictableLatencyModeWindow = 0x14,
  kFeatureHostBehaviorSupport = 0x16,
  kFeatureSanitizeConfig = 0x17,
  kFeatureEnduranceGroupEventConfig = 0x18,
  kFeatureIoCommandSetProfile = 0x19,
  kFeatureSpinupControl = 0x1a,
  kFeatureEnhancedControllerMetadata = 0x7d,
  kFeatureControllerMetadata = 0x7e,
  kFeatureNamespaceMetadata = 0x7f,
  kFeatureSoftwareProgressMarker = 0x80,
  kFeatureHostIdentifier = 0x81,
  kFeatureReservationNotificationMask = 0x82,
  kFeatureReservationPersistance = 0x83,
  kFeatureNamespaceWriteProtectionConfig = 0x84,
};

// |SetFeaturesSubmission| is the base class for all "set feature" submissions.
class SetFeaturesSubmission : public Submission {
 public:
  static constexpr uint8_t kOpcode = 0x09;

  DEF_SUBBIT(dword10, 31, save);
  DEF_ENUM_SUBFIELD(dword10, Feature, 7, 0, feature_id);

  DEF_SUBFIELD(dword14, 6, 0, uuid_index);

  explicit SetFeaturesSubmission(Feature feature) : Submission(kOpcode) { set_feature_id(feature); }
};

// NVM Express Base Specification 2.0, section 5.27.1.5 "Number of Queues".
class SetIoQueueCountSubmission : public SetFeaturesSubmission {
 public:
  SetIoQueueCountSubmission() : SetFeaturesSubmission(Feature::kFeatureNumberOfQueues) {}

 private:
  // These two fields are 0-based (a value of zero indicates one queue).
  DEF_SUBFIELD(dword11, 31, 16, num_completion_queues_minus_one);
  DEF_SUBFIELD(dword11, 15, 0, num_submission_queues_minus_one);

 public:
  SetIoQueueCountSubmission& set_num_completion_queues(uint16_t count) {
    ZX_DEBUG_ASSERT(count > 0);
    set_num_completion_queues_minus_one(count - 1);
    return *this;
  }

  uint32_t num_completion_queues() const { return num_completion_queues_minus_one() + 1; }

  SetIoQueueCountSubmission& set_num_submission_queues(uint16_t count) {
    ZX_DEBUG_ASSERT(count > 0);
    set_num_submission_queues_minus_one(count - 1);
    return *this;
  }

  uint32_t num_submission_queues() const { return num_submission_queues_minus_one() + 1; }
};

class SetIoQueueCountCompletion : public Completion {
 private:
  // These two fields are 0-based (a value of zero indicates one queue).
  DEF_SUBFIELD(command[0], 31, 16, num_completion_queues_minus_one);
  DEF_SUBFIELD(command[0], 15, 0, num_submission_queues_minus_one);

 public:
  void set_num_completion_queues(uint16_t count) {
    ZX_DEBUG_ASSERT(count > 0);
    set_num_completion_queues_minus_one(count - 1);
  }

  uint32_t num_completion_queues() const { return num_completion_queues_minus_one() + 1; }

  void set_num_submission_queues(uint16_t count) {
    ZX_DEBUG_ASSERT(count > 0);
    set_num_submission_queues_minus_one(count - 1);
  }

  uint32_t num_submission_queues() const { return num_submission_queues_minus_one() + 1; }
};

}  // namespace nvme

#endif  // SRC_DEVICES_BLOCK_DRIVERS_NVME_COMMANDS_FEATURES_H_
