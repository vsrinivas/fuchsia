// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "secmem-session.h"

#include <zircon/assert.h>

#include <algorithm>
#include <iterator>

#include <fbl/algorithm.h>
#include <safemath/checked_math.h>

#include "log.h"
#include "tee-client-api/tee-client-types.h"

namespace {

// Some secmem-specific marshaling definitions.

enum TeeParamType {
  kTeeParamTypeBuffer,
  kTeeParamTypeUint32,
  kTeeParamTypeUint64,
  kTeeParamTypePvoid,
};

struct TeeCommandParam {
  TeeParamType type;
  union {
    struct {
      uint32_t buffer_length;
      uint32_t pbuf[1];
    } buf;         // kTeeParamTypeBuffer
    uint32_t u32;  // kTeeParamTypeUint32
  } param;
};

// Defined by secmem TA.
enum SecmemCommandIds {
  kSecmemCommandIdAllocateSecureMemory = 101,
  kSecmemCommandIdProtectMemory = 104,
  kSecmemCommandIdUnprotectMemory = 105,
  kSecmemCommandIdGetPadding = 107,
  kSecmemCommandIdGetVp9HeaderSize = 108,
  kSecmemCommandIdGetMemSize = 110,
};

constexpr uint32_t kProtectionRangeGranularity = 64 * 1024;
constexpr uint32_t kProtectionRangeGranularityMask =
    0xFFFFFFFF & ~(kProtectionRangeGranularity - 1);

enum EnableFlags : uint32_t {
  // Which sub-command.
  kEnableFlag_SubCommand_Mask = 0xFu << 0,
  kEnableFlag_SubCommand_Shift = 0,

  // Disable a currently-enabled range.
  kEnableFlag_SubCommand_Disable = 0x0u,

  // Select a free range and enable it.
  kEnableFlag_SubCommand_Enable = 0x1u,

  // For detecting whether a sub-command exists.  If DetectSubCommand itself is the command being
  // detected, the meaning of success and failure (for that one call only) are reversed for legacy
  // reasons.
  kEnableFlag_SubCommand_DetectSubCommand = 0x2u,

  // Adjust a currently-enabled range.  If the range is adjusted to zero size, the range is
  // disabled.
  kEnableFlag_SubCommand_Adjust = 0x3u,

  // This command is equivalent to creating all ranges, then deleting all ranges with
  // SkipDeviceSecureModeUpdate set, then explicitly disabling protected mode for each device.  But
  // with this command, we don't need to allocate 11 * 64KiB of 64KiB-aligned physically-contiguous
  // memory just to get these effects to happen.
  kEnableFlag_SubCommand_InitTvpForAllRanges = 0x4u,

  // This allows us to zero a page-aligned sub-range of a currently-active range, as long as the
  // sub-range does not overlap with any other currently-active range.  In other words the requested
  // zeroing must be fully covered by exactly one active range and not overlap with any other active
  // range.  The extent of the zeroing is conveyed in the startaddr, size parameters, and must be
  // page aligned (in contrast to other commands which must be 64KiB aligned).
  kEnableFlag_SubCommand_ZeroSubRange = 0x5u,

  // This dumps ranges to debug output, if the firmware has debug output enabled.  Else noop.
  kEnableFlag_SubCommand_DumpRanges = 0x6u,

  // Field indicating which command is being checked for.  If checking for DetectSubCommand itself,
  // the meaning of success and failure are reversed for legacy reasons.
  kEnableFlag_DetectSubCommand_CommandNumber_Mask = 0xFu << 28,
  kEnableFlag_DetectSubCommand_CommandNumber_Shift = 28u,

  // Enable/Disable protected memory range without modifying device protected mode configuration,
  // even if the number of enabled ranges is changing from 0 to 1 or 1 to 0.  The Adjust command
  // never modifies device protected mode configuration.
  kEnableFlag_EnableDisable_SkipDeviceSecureModeUpdate = 1u << 31,

  // Adjust the start of the range instead of the end of the range.
  kEnableFlag_Adjust_RangeAtStart = 1u << 31,

  // Adjust the range to be longer instead of shorter.
  kEnableFlag_Adjust_RangeLonger = 1u << 30,

  // The adjustment size is 64 KiB << (value * 2)
  kEnableFlag_Adjust_Size_Mask = 0x3u << 28,
  kEnableFlag_Adjust_Size_Shift = 28u,
  kEnableFlag_Adjust_Size_Coefficient = 64u * 1024,
  kEnableFlag_Adjust_Size_ExponentMultiplier = 2u,

  kEnableFlag_ZeroSubRange_IsCoveringRangeExplicit = 1u << 31,
};

zx::vmo CreateVmo(uint64_t size) {
  zx::vmo vmo;
  zx_status_t status = zx::vmo::create(size, /*options=*/0, &vmo);
  ZX_ASSERT(status == ZX_OK);

  return vmo;
}

fpromise::result<fuchsia::tee::Buffer> CreateCommandBuffer(const std::vector<uint8_t>& contents) {
  zx::vmo vmo = CreateVmo(static_cast<uint64_t>(contents.size()));

  zx_status_t status = vmo.write(contents.data(), /*offset=*/0, contents.size());
  if (status != ZX_OK) {
    LOG(ERROR, "Failed to write to command buffer VMO - status: %d", status);
    return fpromise::error();
  }

  fuchsia::tee::Buffer buffer;
  buffer.set_vmo(std::move(vmo))
      .set_size(static_cast<uint64_t>(contents.size()))
      .set_offset(0)
      .set_direction(fuchsia::tee::Direction::INOUT);
  return fpromise::ok(std::move(buffer));
}

fuchsia::tee::Value CreateReturnCodeParameter() {
  fuchsia::tee::Value value;
  value.set_direction(fuchsia::tee::Direction::OUTPUT);
  return value;
}

fpromise::result<fuchsia::tee::Buffer> GetCommandBuffer(
    std::vector<fuchsia::tee::Parameter>* parameter_set) {
  ZX_DEBUG_ASSERT(parameter_set);
  constexpr size_t kParamBufferIndex = 0;

  if (!parameter_set->at(kParamBufferIndex).is_buffer()) {
    return fpromise::error();
  }

  fuchsia::tee::Buffer& buffer = parameter_set->at(kParamBufferIndex).buffer();
  if (!buffer.has_vmo() || !buffer.has_size() || !buffer.has_offset() || !buffer.has_direction()) {
    return fpromise::error();
  }
  if (buffer.offset() >= buffer.size()) {
    return fpromise::error();
  }

  return fpromise::ok(std::move(buffer));
}

bool IsExpectedSecmemCommandResult(const fuchsia::tee::OpResult& result) {
  return result.has_parameter_set() && result.parameter_set().size() == 4 &&
         result.has_return_code() && result.has_return_origin();
}

}  // namespace

fpromise::result<SecmemSession, fuchsia::tee::ApplicationSyncPtr> SecmemSession::TryOpen(
    fuchsia::tee::ApplicationSyncPtr tee_connection) {
  if (!tee_connection.is_bound()) {
    return fpromise::error(std::move(tee_connection));
  }

  fuchsia::tee::OpResult result;
  uint32_t session_id = 0;
  auto params = std::vector<fuchsia::tee::Parameter>();

  if (zx_status_t status = tee_connection->OpenSession2(std::move(params), &session_id, &result);
      status != ZX_OK) {
    LOG(ERROR, "OpenSession channel call failed - status: %d", status);
    return fpromise::error(std::move(tee_connection));
  }

  if (!result.has_return_code() || !result.has_return_origin()) {
    LOG(ERROR, "OpenSession returned with result codes missing");
    return fpromise::error(std::move(tee_connection));
  }

  if (result.return_code() != TEEC_SUCCESS) {
    LOG(WARNING, "OpenSession to secmem failed - TEEC_Result: %" PRIx64 ", origin: %" PRIu32 ".",
        result.return_code(), static_cast<uint32_t>(result.return_origin()));
    return fpromise::error(std::move(tee_connection));
  }

  return fpromise::ok(SecmemSession{session_id, std::move(tee_connection)});
}

SecmemSession::~SecmemSession() {
  if (tee_connection_.is_bound()) {
    tee_connection_->CloseSession(session_id_);
  }
}

void SecmemSession::PackUint32Parameter(uint32_t value, std::vector<uint8_t>* buffer) {
  ZX_DEBUG_ASSERT(buffer);

  TeeCommandParam param;
  param.type = kTeeParamTypeUint32;
  param.param.u32 = value;

  auto param_begin = reinterpret_cast<const uint8_t*>(&param);
  auto param_end = reinterpret_cast<const uint8_t*>(&param) + (sizeof(param) / sizeof(uint8_t));

  const size_t new_buf_size = fbl::round_up(buffer->size() + sizeof(param), kParameterAlignment);

  buffer->reserve(new_buf_size);
  buffer->insert(buffer->end(), param_begin, param_end);

  if (buffer->size() < new_buf_size) {
    std::fill_n(std::back_inserter(*buffer), new_buf_size - buffer->size(), 0);
  }
}

TEEC_Result SecmemSession::InvokeSecmemCommand(uint32_t command,
                                               std::vector<uint8_t>* cmd_buffer_vec) {
  ZX_DEBUG_ASSERT(cmd_buffer_vec);

  if (!tee_connection_.is_bound()) {
    return TEEC_ERROR_TARGET_DEAD;
  }

  // The first parameter is where all of Amlogic's custom parameters are packed.
  fuchsia::tee::Buffer in_cmd_buffer;
  if (auto in_cmd_buffer_result = CreateCommandBuffer(*cmd_buffer_vec);
      in_cmd_buffer_result.is_ok()) {
    in_cmd_buffer = in_cmd_buffer_result.take_value();
  } else {
    return TEEC_ERROR_COMMUNICATION;
  }

  constexpr size_t kNumParams = 4;
  auto params = std::vector<fuchsia::tee::Parameter>();
  params.reserve(kNumParams);
  params.push_back(fuchsia::tee::Parameter::WithBuffer(std::move(in_cmd_buffer)));
  params.push_back(fuchsia::tee::Parameter::WithNone(fuchsia::tee::None{}));
  params.push_back(fuchsia::tee::Parameter::WithNone(fuchsia::tee::None{}));
  params.push_back(fuchsia::tee::Parameter::WithValue(CreateReturnCodeParameter()));

  fuchsia::tee::OpResult result;
  if (zx_status_t status =
          tee_connection_->InvokeCommand(session_id_, command, std::move(params), &result);
      status != ZX_OK) {
    LOG(ERROR, "InvokeCommand channel call failed - status: %d", status);
    return TEEC_ERROR_COMMUNICATION;
  }

  if (!IsExpectedSecmemCommandResult(result)) {
    LOG(ERROR, "InvokeCommand returned with unexpected OpResult");
    return TEEC_ERROR_COMMUNICATION;
  }

  fuchsia::tee::Buffer out_cmd_buffer;
  if (auto out_cmd_buffer_result = GetCommandBuffer(result.mutable_parameter_set());
      out_cmd_buffer_result.is_ok()) {
    out_cmd_buffer = out_cmd_buffer_result.take_value();
  } else {
    LOG(ERROR, "Secmem command returned with unexpected command buffer parameter");
    return TEEC_ERROR_COMMUNICATION;
  }

  // Ensure that `cmd_buffer_vec` is of the appropriate size
  cmd_buffer_vec->resize(out_cmd_buffer.size() - out_cmd_buffer.offset(), /*val=*/0);

  // Read output into provided `cmd_buffer_vec`
  if (zx_status_t status =
          out_cmd_buffer.vmo().read(cmd_buffer_vec->data(), out_cmd_buffer.offset(),
                                    out_cmd_buffer.size() - out_cmd_buffer.offset());
      status != ZX_OK) {
    LOG(ERROR, "Failed to read parameters from VMO - status: %d", status);
    return TEEC_ERROR_COMMUNICATION;
  }

  if (result.return_code() != TEEC_SUCCESS) {
    // Inability to talk to the TA or similar.
    return static_cast<TEEC_Result>(result.return_code());
  }

  // The "result.return_code()" is sortof a transport-level return code if something goes wrong
  // communicating with the TA.  The actual secmem TA return code is in params[3].a.
  return static_cast<TEEC_Result>(result.parameter_set()[3].value().a());
}

fpromise::result<uint32_t> SecmemSession::UnpackUint32Parameter(const std::vector<uint8_t>& buffer,
                                                                size_t* offset_in_out) {
  ZX_DEBUG_ASSERT(offset_in_out);

  size_t offset = *offset_in_out;

  if (offset + sizeof(TeeCommandParam) > buffer.size()) {
    return fpromise::error();
  }

  const uint8_t* param_addr = buffer.data() + offset;
  auto param = reinterpret_cast<const TeeCommandParam*>(param_addr);
  if (param->type != kTeeParamTypeUint32) {
    LOG(ERROR, "Received unexpected param type");
    return fpromise::error();
  }

  offset += sizeof(TeeCommandParam);
  *offset_in_out = fbl::round_up(offset, kParameterAlignment);

  return fpromise::ok(param->param.u32);
}

TEEC_Result SecmemSession::InvokeProtectMemory(uint32_t start, uint32_t length,
                                               uint32_t enable_flags) {
  std::vector<uint8_t> cmd_buffer;
  // Reserve room for 4 parameters.
  cmd_buffer.reserve(kParameterAlignment * 4);

  PackUint32Parameter(kSecmemCommandIdProtectMemory, &cmd_buffer);

  PackUint32Parameter(enable_flags, &cmd_buffer);

  // count of regions must be 1-4 inclusive
  constexpr uint32_t kRegionNum = 1;
  PackUint32Parameter(kRegionNum, &cmd_buffer);

  PackUint32Parameter(start, &cmd_buffer);

  PackUint32Parameter(length, &cmd_buffer);

  TEEC_Result result = InvokeSecmemCommand(kSecmemCommandIdProtectMemory, &cmd_buffer);
  return result;
}

bool SecmemSession::DetectIsAdjustAndSkipDeviceSecureModeUpdateAvailable() {
  // If Adjust is available, then so is SkipDeviceSecureModeUpdate, so we only need to detect if
  // Adjust is available.
  //
  // We don't expect to be running with back-version firmware in any normal situation, but we need
  // to be sure that in abnormal situations we don't cause problems getting back to a normal
  // situation asap, so we accommodate running on back-version firmware by detecting if we're
  // missing new-version firmware, and if so, disabling dynamic protected contiguous memory
  // management.
  if (is_detect_called_) {
    return is_adjust_known_available_;
  }
  is_detect_called_ = true;
  // In the TEE, if the firmware doesn't have DetectSubCommand, this will result in an enabled HW
  // protection range that has a last block address < first block address, which covers zero 64 KiB
  // blocks.  In addition, due to legacy firmware side-effects of creating a memory protection range
  // this will modify per-device protected mode config, and then change those back as we unwind from
  // discovering that we're running on legacy firmware somehow, temporarily.
  uint32_t start = std::numeric_limits<uint32_t>::max() & kProtectionRangeGranularityMask;
  // This can't be zero or the TEE will reject the request.  If we find we're on older-version
  // firmware (and only if we're on older-version firmware), we clean up the phantom block to regain
  // use of all the HW protection ranges.
  //
  // Current-version firmware only requires this value to be non-zero, but otherwise ignores the
  // value (when using DetectSubCommand).
  uint32_t length = 0u - 1u;
  ZX_DEBUG_ASSERT(length == 0xFFFFFFFFu);

  uint32_t enable_flags = 0;
  static_assert(kEnableFlag_SubCommand_Shift == 0);
  enable_flags |= kEnableFlag_SubCommand_DetectSubCommand << kEnableFlag_SubCommand_Shift;
  enable_flags |= kEnableFlag_SubCommand_DetectSubCommand
                  << kEnableFlag_DetectSubCommand_CommandNumber_Shift;

  TEEC_Result detect_is_detect_available_result = InvokeProtectMemory(start, length, enable_flags);
  // The sense of success/failure is flipped here, for legacy reasons.
  bool is_detect_available = (detect_is_detect_available_result == TEEC_ERROR_GENERIC);
  LOG(INFO, "is_detect_available: %d detect_is_detect_available_result: 0x%x", is_detect_available,
      detect_is_detect_available_result);
  if (!is_detect_available) {
    LOG(INFO, "!is_detect_available");
    enable_flags = 0;
    enable_flags |= kEnableFlag_SubCommand_Disable << kEnableFlag_SubCommand_Shift;
    ZX_DEBUG_ASSERT(enable_flags == 0);
    TEEC_Result cleanup_result = InvokeProtectMemory(start, length, enable_flags);
    // This isn't verifying much since older firmware doesn't plumb status from very far down, but
    // we should see TEEC_SUCCESS here.
    ZX_ASSERT(cleanup_result == TEEC_SUCCESS);
    return false;
  }

  // Now we know that the DetectSubCommand sub-command exists.  At this point we could just return
  // true, since we know that DetectSubCommand existing implies Adjust existing, but in the interest
  // of establishing a pattern, we go ahead and detect whether Adjust exists explicitly here.
  enable_flags = 0;
  enable_flags |= kEnableFlag_SubCommand_DetectSubCommand << kEnableFlag_SubCommand_Shift;
  enable_flags |= kEnableFlag_SubCommand_Adjust << kEnableFlag_DetectSubCommand_CommandNumber_Shift;
  // We just need start, length that both aren't zero; the specific non-zero values don't matter.
  TEEC_Result detect_is_adjust_available_result = InvokeProtectMemory(start, length, enable_flags);
  is_adjust_known_available_ = (detect_is_adjust_available_result == TEEC_SUCCESS);
  // For this particular sub-command, we know this will be true given that detect is available.  For
  // potential future-added sub-commands, we won't be able to have a similar assert.
  ZX_ASSERT(is_adjust_known_available_);

  enable_flags = 0;
  enable_flags |= kEnableFlag_SubCommand_DetectSubCommand << kEnableFlag_SubCommand_Shift;
  enable_flags |= kEnableFlag_SubCommand_InitTvpForAllRanges
                  << kEnableFlag_DetectSubCommand_CommandNumber_Shift;
  // We just need start, length that both aren't zero; the specific non-zero values don't matter.
  TEEC_Result detect_is_init_tvp_available_result =
      InvokeProtectMemory(start, length, enable_flags);
  bool is_init_tvp_available = (detect_is_init_tvp_available_result == TEEC_SUCCESS);
  // For this particular sub-command, we know this will be true given that detect is available.  For
  // potential future-added sub-commands, we won't be able to have a similar assert.
  ZX_ASSERT(is_init_tvp_available);

  enable_flags = 0;
  enable_flags |= kEnableFlag_SubCommand_InitTvpForAllRanges << kEnableFlag_SubCommand_Shift;
  // The start and length both need to be non-zero, but otherwise are ignored for this sub-command.
  TEEC_Result enable_result = InvokeProtectMemory(0xFFFFFFFF, 0xFFFFFFFF, enable_flags);
  ZX_ASSERT(enable_result == TEEC_SUCCESS);

  return is_adjust_known_available_;
}

TEEC_Result SecmemSession::ProtectMemoryRange(uint32_t start, uint32_t length,
                                              bool is_enable_protection) {
  ZX_DEBUG_ASSERT(is_detect_called_);
  ZX_DEBUG_ASSERT(start % kProtectionRangeGranularity == 0);
  ZX_DEBUG_ASSERT(length % kProtectionRangeGranularity == 0);
  ZX_DEBUG_ASSERT(length != 0);

  uint32_t enable_flags = 0;
  static_assert(kEnableFlag_SubCommand_Shift == 0);
  if (is_enable_protection) {
    enable_flags |= kEnableFlag_SubCommand_Enable << kEnableFlag_SubCommand_Shift;
  } else {
    enable_flags |= kEnableFlag_SubCommand_Disable << kEnableFlag_SubCommand_Shift;
  }
  if (is_adjust_known_available_) {
    enable_flags |= kEnableFlag_EnableDisable_SkipDeviceSecureModeUpdate;
  }

  return InvokeProtectMemory(start, length, enable_flags);
}

TEEC_Result SecmemSession::AdjustMemoryRange(uint32_t start, uint32_t length,
                                             uint32_t adjustment_magnitude, bool at_start,
                                             bool longer) {
  ZX_DEBUG_ASSERT(is_adjust_known_available_);
  ZX_DEBUG_ASSERT(start % kProtectionRangeGranularity == 0);
  ZX_DEBUG_ASSERT(length % kProtectionRangeGranularity == 0);
  ZX_DEBUG_ASSERT(length != 0);
  ZX_DEBUG_ASSERT(adjustment_magnitude % kProtectionRangeGranularity == 0);

  // The available choices here are 64KiB, 256KiB, 1MiB, 4MiB.  We don't want to zero too much per
  // call since that could have us in the TEE long enough to cause trouble with scheduling.  For now
  // let's see if we can zero 256KiB without glitching.  If not, we may need to zero only 64KiB per
  // call, at the cost of 4x as many calls.  We haven't tried 1MiB yet.
  constexpr uint32_t kMaxZeroingSizeInSingleCall = 64u * 1024;

  uint32_t enable_flags_base = 0;
  static_assert(kEnableFlag_SubCommand_Shift == 0);
  enable_flags_base |= kEnableFlag_SubCommand_Adjust << kEnableFlag_SubCommand_Shift;
  if (at_start) {
    enable_flags_base |= kEnableFlag_Adjust_RangeAtStart;
  }
  if (longer) {
    enable_flags_base |= kEnableFlag_Adjust_RangeLonger;
  }

  uint32_t adjustment_todo = adjustment_magnitude;
  while (adjustment_todo != 0) {
    uint32_t enable_flags = enable_flags_base;
    uint32_t magnitude = 0;
    int32_t value;
    for (value = (kEnableFlag_Adjust_Size_Mask >> kEnableFlag_Adjust_Size_Shift); value >= 0;
         --value) {
      magnitude = kEnableFlag_Adjust_Size_Coefficient
                  << (value * kEnableFlag_Adjust_Size_ExponentMultiplier);
      if (magnitude <= adjustment_todo && (longer || magnitude <= kMaxZeroingSizeInSingleCall)) {
        break;
      }
    }
    ZX_DEBUG_ASSERT(magnitude != 0);
    ZX_DEBUG_ASSERT(magnitude <= adjustment_todo);
    ZX_DEBUG_ASSERT(value >= 0 && static_cast<uint32_t>(value) <= (kEnableFlag_Adjust_Size_Mask >>
                                                                   kEnableFlag_Adjust_Size_Shift));
    uint32_t to_adjust_this_time_magnitude = magnitude;
    uint32_t to_adjust_this_time_value = value;
    enable_flags |= (to_adjust_this_time_value << kEnableFlag_Adjust_Size_Shift);

    TEEC_Result adjust_result = InvokeProtectMemory(start, length, enable_flags);
    if (adjust_result != TEEC_SUCCESS) {
      LOG(WARNING,
          "InvokeProtectMemory (adjust) failed - start: 0x%x length: 0x%x enable_flags: 0x%x "
          "adjust_result: %x",
          start, length, enable_flags, adjust_result);
      if (adjustment_todo != adjustment_magnitude) {
        // If this fails after making a partial adjustment, we don't have a way to report the actual
        // current range to the layers above.  In addition, this call to the TEE should _never_
        // fail, and the fact that it has failed is good evidence that the TEE has gotten into a
        // broken state, which for security reasons is good justification for doing a hard reboot to
        // get back to a functional TEE.  We really can't be having range shortening or range
        // deletion failing; that just can't really work from the user's point of view even if we
        // could report the actual current range back to sysmem in this path.
        ZX_PANIC("AdjustMemoryRange() failed - adjust_result: 0x%x\n", adjust_result);
      }
      return adjust_result;
    }
    uint32_t old_start = start;
    uint32_t old_length = length;
    adjustment_todo -= to_adjust_this_time_magnitude;
    // We adjust the parameters so we can refer to the newly-adjusted range next iteration.
    if (longer) {
      length += to_adjust_this_time_magnitude;
      if (at_start) {
        start -= to_adjust_this_time_magnitude;
      }
    } else {
      length -= to_adjust_this_time_magnitude;
      if (at_start) {
        start += to_adjust_this_time_magnitude;
      }
    }
    uint32_t old_end = old_start + old_length;
    uint32_t new_end = start + length;
    ZX_DEBUG_ASSERT(start == old_start || new_end == old_end);
  }
  return TEEC_SUCCESS;
}

TEEC_Result SecmemSession::ZeroSubRange(bool is_covering_range_explicit, uint32_t start,
                                        uint32_t length) {
  ZX_DEBUG_ASSERT(start % zx_system_get_page_size() == 0);
  ZX_DEBUG_ASSERT(length % zx_system_get_page_size() == 0);
  ZX_DEBUG_ASSERT(length != 0);

  // We're not restricted by the TEE API here but it's good to avoid zeroing too much in one call
  // to the TEE.
  constexpr uint32_t kMaxZeroingSizeInSingleCall = 64u * 1024;

  uint32_t enable_flags = 0;
  static_assert(kEnableFlag_SubCommand_Shift == 0);
  enable_flags |= kEnableFlag_SubCommand_ZeroSubRange << kEnableFlag_SubCommand_Shift;
  if (is_covering_range_explicit) {
    enable_flags |= kEnableFlag_ZeroSubRange_IsCoveringRangeExplicit;
  }

  uint32_t end = start + length;
  uint32_t todo_this_time;
  for (uint32_t iter = start; iter != end; iter += todo_this_time) {
    todo_this_time = std::min(end - iter, kMaxZeroingSizeInSingleCall);
    TEEC_Result zero_result = InvokeProtectMemory(iter, todo_this_time, enable_flags);
    if (zero_result != TEEC_SUCCESS) {
      LOG(WARNING,
          "InvokeProtectMemory() (zero) failed - start: 0x%x length: 0x%x enable_flags: 0x%x", iter,
          todo_this_time, enable_flags);
      return zero_result;
    }
  }

  return TEEC_SUCCESS;
}

void SecmemSession::DumpRanges() {
  uint32_t enable_flags = 0;
  static_assert(kEnableFlag_SubCommand_Shift == 0);
  enable_flags |= kEnableFlag_SubCommand_DumpRanges << kEnableFlag_SubCommand_Shift;
  TEEC_Result dump_result = InvokeProtectMemory(0xFFFFFFFF, 0xFFFFFFFF, enable_flags);
  if (dump_result != TEEC_SUCCESS) {
    LOG(WARNING, "InvokeProtectMemory() (dump ranges) failed - dump_result: %d", dump_result);
    ZX_ASSERT(dump_result == TEEC_SUCCESS);
    return;
  }
  // done
}

TEEC_Result SecmemSession::AllocateSecureMemory(uint32_t* start, uint32_t* length) {
  // First, ask secmem TA for the max size of VDEC, then allocate that size.

  std::vector<uint8_t> cmd_buffer;
  // Reserve room for 4 parameters.
  cmd_buffer.reserve(kParameterAlignment * 4);

  // kSecmemCommandIdGetMemSize command first
  PackUint32Parameter(kSecmemCommandIdGetMemSize, &cmd_buffer);
  TEEC_Result tee_status = InvokeSecmemCommand(kSecmemCommandIdGetMemSize, &cmd_buffer);
  if (tee_status != TEEC_SUCCESS) {
    LOG(ERROR, "kSecmemCommandIdGetMemSize failed - TEEC_Result: %" PRIx32, tee_status);
    return tee_status;
  }

  size_t output_offset = 0;
  fpromise::result<uint32_t> max_vdec_size_result =
      UnpackUint32Parameter(cmd_buffer, &output_offset);
  if (!max_vdec_size_result.is_ok()) {
    LOG(ERROR, "UnpackUint32Parameter() after kSecmemCommandIdGetMemSize failed");
    return TEEC_ERROR_COMMUNICATION;
  }

  // Reset for new command: kSecmemCommandIdAllocateSecureMemory.
  cmd_buffer.clear();

  PackUint32Parameter(kSecmemCommandIdAllocateSecureMemory, &cmd_buffer);

  // ignored
  constexpr uint32_t kDbgLevel = 0;
  PackUint32Parameter(kDbgLevel, &cmd_buffer);

  // We can pass false for is_vp9, even if later when we do
  // kSecmemCommandIdGetVp9HeaderSize we start at exactly one AMLV header length
  // into a page to avoid one frame/sub-frame being copied.
  constexpr auto kIsVp9 = static_cast<uint32_t>(false);  // 0
  PackUint32Parameter(kIsVp9, &cmd_buffer);

  PackUint32Parameter(max_vdec_size_result.value(), &cmd_buffer);

  tee_status = InvokeSecmemCommand(kSecmemCommandIdAllocateSecureMemory, &cmd_buffer);
  if (tee_status != TEEC_SUCCESS) {
    LOG(ERROR, "kSecmemCommandIdAllocateSecureMemory failed - TEEC_Result: %" PRIx32, tee_status);
    return tee_status;
  }

  output_offset = 0;
  fpromise::result<uint32_t> vdec_paddr_result = UnpackUint32Parameter(cmd_buffer, &output_offset);
  if (!vdec_paddr_result.is_ok()) {
    LOG(ERROR, "UnpackUint32Parameter() after kSecmemCommandIdAllocateSecureMemory failed");
    return TEEC_ERROR_COMMUNICATION;
  }

  *start = vdec_paddr_result.value();
  *length = max_vdec_size_result.value();

  return TEEC_SUCCESS;
}
