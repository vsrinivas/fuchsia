// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_LOGGING_FLAGS_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_LOGGING_FLAGS_H_

#include <cstdint>

namespace media::audio {

// Render-related logging
//
// Timing and lifetime for AudioRenderers (including timestamps).
inline constexpr bool kLogRendererCtorDtorCalls = false;
inline constexpr bool kLogRendererClockConstruction = false;
inline constexpr bool kLogAudioRendererSetUsageCalls = false;
inline constexpr bool kLogRendererPlayCalls = false;
inline constexpr bool kLogRendererPauseCalls = false;

// Loudness calls and actions for AudioRenderers.
inline constexpr bool kLogRenderUsageVolumeGainActions = true;
inline constexpr bool kLogRendererSetGainMuteRampCalls = false;
inline constexpr bool kLogRendererSetGainMuteRampActions = false;

// In "client-side underflows", we discard data because its start timestamp has already passed. For
// each packet queue, we log the first underflow, plus subsequent occurrences depending on the
// audio_core logging level. We throttle how frequently these are displayed. If log_level is set to
// TRACE or DEBUG, all client-side underflows are logged (at log_level -1: VLOG TRACE), per the
// kPacketQueueUnderflowTraceInterval. If set to INFO, we log less often (at log_level 1: INFO),
// throttling by kPacketQueueUnderflowInfoInterval. If WARNING or higher, we log even less, per
// kPacketQueueUnderflowWarningInterval. By default we set NDEBUG builds to WARNING and DEBUG builds
// to INFO. To disable all client-side underflow logging, set kLogPacketQueueUnderflow to false.
inline constexpr bool kLogPacketQueueUnderflow = true;
inline constexpr uint16_t kPacketQueueUnderflowTraceInterval = 1;
inline constexpr uint16_t kPacketQueueUnderflowInfoInterval = 10;
inline constexpr uint16_t kPacketQueueUnderflowWarningInterval = 100;

// Capture-related logging
//
// In a "client-side overflow", data is discarded because no buffer space is available. For each
// Capturer, we log the first overflow, plus subsequent occurrences depending on audio_core's
// logging level. If log_level is set to TRACE or DEBUG, all client-side overflows are logged.
inline constexpr bool kLogCaptureOverflow = true;
inline constexpr uint16_t kCaptureOverflowInfoInterval = 10;
inline constexpr uint16_t kCaptureOverflowWarningInterval = 100;

// Relevant for both renderers and capturers
inline constexpr bool kLogPresentationDelay = false;

// Device- and driver-related logging
//
inline constexpr bool kLogAudioDevice = false;
inline constexpr bool kLogDevicePlugUnplug = true;
inline constexpr bool kLogSetDeviceGainMuteActions = false;

// Values retrieved from the audio driver related to delay, and associated calculations.
inline constexpr bool kLogDriverDelayProperties = false;

// Formats supported by the driver, and the format chosen when creating a RingBuffer.
inline constexpr bool kLogAudioDriverFormats = false;

// Log driver callbacks received (except position notifications: handled separately below).
inline constexpr bool kLogAudioDriverCallbacks = false;
// For non-zero value N, log every Nth position notification. If 0, don't log any.
inline constexpr uint16_t kDriverPositionNotificationDisplayInterval = 0;

// Mix-related logging
//
inline constexpr bool kLogReconciledTimelineFunctions = false;  // very verbose for ongoing streams
inline constexpr bool kLogInitialPositionSync = false;
inline constexpr bool kLogDestDiscontinuities = true;
inline constexpr int kLogDestDiscontinuitiesStride = 997;  // Prime, to avoid misleading cadences.

// Jam-synchronizations can occur up to 100/sec. We log each MixStage's first occurrence, plus
// subsequent instances depending on our logging level. To disable jam-sync logging for a certain
// log level, set the interval to 0. To disable all jam-sync logging, set kLogJamSyncs to false.
inline constexpr bool kLogJamSyncs = true;
inline constexpr uint16_t kJamSyncWarningInterval = 200;  // Log 1 of every 200 jam-syncs at WARNING
inline constexpr uint16_t kJamSyncInfoInterval = 20;      // Log 1 of every 20 jam-syncs at INFO
inline constexpr uint16_t kJamSyncTraceInterval = 1;      // Log all remaining jam-syncs at TRACE

// Timing and position advance, in pipeline stages.
#ifdef NDEBUG
// These should be false in production builds.
inline constexpr bool kLogReadLocks = false;
inline constexpr bool kLogTrims = false;
#else
// Keep to true in debug builds so we have verbose logs on FX_CHECK failures in tests.
inline constexpr bool kLogReadLocks = true;
inline constexpr bool kLogTrims = true;
#endif

// Effects-related logging
inline constexpr bool kLogThermalEffectEnumeration = false;

// Policy-related logging
//
// Routing-related logging
inline constexpr bool kLogRoutingChanges = false;
// Logging related to idle power-conservation policy/mechanism.
inline constexpr bool kLogIdlePolicyChannelFrequencies = false;
inline constexpr bool kLogIdlePolicyStaticConfigValues = false;
inline constexpr bool kLogIdlePolicyCounts = false;
inline constexpr bool kLogIdleTimers = false;
inline constexpr bool kLogSetActiveChannelsSupport = false;
inline constexpr bool kLogSetActiveChannelsCalls = false;
inline constexpr bool kLogSetActiveChannelsActions = true;
// Logging related to thermal management
inline constexpr bool kLogThermalStateChanges = true;

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_LOGGING_FLAGS_H_
