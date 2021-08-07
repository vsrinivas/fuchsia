// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidlcat/lib/syscall_decoder_dispatcher.h"

#include <sys/time.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <cstdint>
#include <fstream>
#include <memory>
#include <sstream>

#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/lib/fidl_codec/semantic.h"
#include "tools/fidlcat/lib/code_generator/test_generator.h"
#include "tools/fidlcat/lib/inference.h"
#include "tools/fidlcat/lib/interception_workflow.h"
#include "tools/fidlcat/lib/syscall_decoder.h"
#include "tools/fidlcat/lib/top.h"
#include "tools/fidlcat/proto/session.pb.h"

namespace fidlcat {

std::unique_ptr<fidl_codec::Struct> uint128_struct_definition = nullptr;

const fidl_codec::Struct& GetUint128StructDefinition() {
  if (uint128_struct_definition == nullptr) {
    uint128_struct_definition = std::make_unique<fidl_codec::Struct>("zx.uint128");
    uint128_struct_definition->AddMember("low",
                                         SyscallTypeToFidlCodecType(SyscallType::kUint64Hexa));
    uint128_struct_definition->AddMember("high",
                                         SyscallTypeToFidlCodecType(SyscallType::kUint64Hexa));
  }
  return *uint128_struct_definition;
}

std::unique_ptr<fidl_codec::Type> SyscallTypeToFidlCodecType(fidlcat::SyscallType syscall_type) {
  switch (syscall_type) {
    case SyscallType::kBool:
      return std::make_unique<fidl_codec::BoolType>();
    case SyscallType::kBtiPerm:
      return std::make_unique<fidl_codec::Uint32Type>(fidl_codec::Uint32Type::Kind::kBtiPerm);
    case SyscallType::kCachePolicy:
      return std::make_unique<fidl_codec::Uint32Type>(fidl_codec::Uint32Type::Kind::kCachePolicy);
    case SyscallType::kChannelOption:
      return std::make_unique<fidl_codec::Uint32Type>(fidl_codec::Uint32Type::Kind::kChannelOption);
    case SyscallType::kChar:
      return std::make_unique<fidl_codec::Int8Type>(fidl_codec::Int8Type::Kind::kChar);
    case SyscallType::kClock:
      return std::make_unique<fidl_codec::Uint32Type>(fidl_codec::Uint32Type::Kind::kClock);
    case SyscallType::kDuration:
      return std::make_unique<fidl_codec::Int64Type>(fidl_codec::Int64Type::Kind::kDuration);
    case SyscallType::kExceptionChannelType:
      return std::make_unique<fidl_codec::Uint32Type>(
          fidl_codec::Uint32Type::Kind::kExceptionChannelType);
    case SyscallType::kExceptionState:
      return std::make_unique<fidl_codec::Uint32Type>(
          fidl_codec::Uint32Type::Kind::kExceptionState);
    case SyscallType::kFeatureKind:
      return std::make_unique<fidl_codec::Uint32Type>(fidl_codec::Uint32Type::Kind::kFeatureKind);
    case SyscallType::kFutex:
      return std::make_unique<fidl_codec::Int32Type>(fidl_codec::Int32Type::Kind::kFutex);
    case SyscallType::kGpAddr:
      return std::make_unique<fidl_codec::Uint64Type>(fidl_codec::Uint64Type::Kind::kGpAddr);
    case SyscallType::kGuestTrap:
      return std::make_unique<fidl_codec::Uint32Type>(fidl_codec::Uint32Type::Kind::kGuestTrap);
    case SyscallType::kHandle:
      return std::make_unique<fidl_codec::HandleType>();
    case SyscallType::kInfoMapsType:
      return std::make_unique<fidl_codec::Uint32Type>(fidl_codec::Uint32Type::Kind::kInfoMapsType);
    case SyscallType::kInt32:
      return std::make_unique<fidl_codec::Int32Type>();
    case SyscallType::kInt64:
      return std::make_unique<fidl_codec::Int64Type>();
    case SyscallType::kInterruptFlags:
      return std::make_unique<fidl_codec::Uint32Type>(
          fidl_codec::Uint32Type::Kind::kInterruptFlags);
    case SyscallType::kIommuType:
      return std::make_unique<fidl_codec::Uint32Type>(fidl_codec::Uint32Type::Kind::kIommuType);
    case SyscallType::kKoid:
      return std::make_unique<fidl_codec::Uint64Type>(fidl_codec::Uint64Type::Kind::kKoid);
    case SyscallType::kKtraceControlAction:
      return std::make_unique<fidl_codec::Uint32Type>(
          fidl_codec::Uint32Type::Kind::kKtraceControlAction);
    case SyscallType::kMonotonicTime:
      return std::make_unique<fidl_codec::Int64Type>(fidl_codec::Int64Type::Kind::kMonotonicTime);
    case SyscallType::kObjectInfoTopic:
      return std::make_unique<fidl_codec::Uint32Type>(
          fidl_codec::Uint32Type::Kind::kObjectInfoTopic);
    case SyscallType::kObjType:
      return std::make_unique<fidl_codec::Uint32Type>(fidl_codec::Uint32Type::Kind::kObjType);
    case SyscallType::kPacketGuestVcpuType:
      return std::make_unique<fidl_codec::Uint8Type>(
          fidl_codec::Uint8Type::Kind::kPacketGuestVcpuType);
    case SyscallType::kPacketPageRequestCommand:
      return std::make_unique<fidl_codec::Uint16Type>(
          fidl_codec::Uint16Type::Kind::kPacketPageRequestCommand);
    case SyscallType::kPaddr:
      return std::make_unique<fidl_codec::Uint64Type>(fidl_codec::Uint64Type::Kind::kPaddr);
    case SyscallType::kPciBarType:
      return std::make_unique<fidl_codec::Uint32Type>(fidl_codec::Uint32Type::Kind::kPciBarType);
    case SyscallType::kPolicyAction:
      return std::make_unique<fidl_codec::Uint32Type>(fidl_codec::Uint32Type::Kind::kPolicyAction);
    case SyscallType::kPolicyCondition:
      return std::make_unique<fidl_codec::Uint32Type>(
          fidl_codec::Uint32Type::Kind::kPolicyCondition);
    case SyscallType::kPolicyTopic:
      return std::make_unique<fidl_codec::Uint32Type>(fidl_codec::Uint32Type::Kind::kPolicyTopic);
    case SyscallType::kPortPacketType:
      return std::make_unique<fidl_codec::Uint32Type>(
          fidl_codec::Uint32Type::Kind::kPortPacketType);
    case SyscallType::kProfileInfoFlags:
      return std::make_unique<fidl_codec::Uint32Type>(
          fidl_codec::Uint32Type::Kind::kProfileInfoFlags);
    case SyscallType::kPropType:
      return std::make_unique<fidl_codec::Uint32Type>(fidl_codec::Uint32Type::Kind::kPropType);
    case SyscallType::kRights:
      return std::make_unique<fidl_codec::Uint32Type>(fidl_codec::Uint32Type::Kind::kRights);
    case SyscallType::kRsrcKind:
      return std::make_unique<fidl_codec::Uint32Type>(fidl_codec::Uint32Type::Kind::kRsrcKind);
    case SyscallType::kSignals:
      return std::make_unique<fidl_codec::Uint32Type>(fidl_codec::Uint32Type::Kind::kSignals);
    case SyscallType::kSize:
      return std::make_unique<fidl_codec::Uint64Type>(fidl_codec::Uint64Type::Kind::kSize);
    case SyscallType::kSocketCreateOptions:
      return std::make_unique<fidl_codec::Uint32Type>(
          fidl_codec::Uint32Type::Kind::kSocketCreateOptions);
    case SyscallType::kSocketReadOptions:
      return std::make_unique<fidl_codec::Uint32Type>(
          fidl_codec::Uint32Type::Kind::kSocketReadOptions);
    case SyscallType::kSocketShutdownOptions:
      return std::make_unique<fidl_codec::Uint32Type>(
          fidl_codec::Uint32Type::Kind::kSocketShutdownOptions);
    case SyscallType::kSocketDisposition:
      return std::make_unique<fidl_codec::Uint32Type>(
          fidl_codec::Uint32Type::Kind::kSocketDisposition);
    case SyscallType::kStatus:
      return std::make_unique<fidl_codec::Uint32Type>(fidl_codec::Uint32Type::Kind::kStatus);
    case SyscallType::kSystemEventType:
      return std::make_unique<fidl_codec::Uint32Type>(
          fidl_codec::Uint32Type::Kind::kSystemEventType);
    case SyscallType::kSystemPowerctl:
      return std::make_unique<fidl_codec::Uint32Type>(
          fidl_codec::Uint32Type::Kind::kSystemPowerctl);
    case SyscallType::kThreadState:
      return std::make_unique<fidl_codec::Uint32Type>(fidl_codec::Uint32Type::Kind::kThreadState);
    case SyscallType::kThreadStateTopic:
      return std::make_unique<fidl_codec::Uint32Type>(
          fidl_codec::Uint32Type::Kind::kThreadStateTopic);
    case SyscallType::kTime:
      return std::make_unique<fidl_codec::Int64Type>(fidl_codec::Int64Type::Kind::kTime);
    case SyscallType::kTimerOption:
      return std::make_unique<fidl_codec::Uint32Type>(fidl_codec::Uint32Type::Kind::kTimerOption);
    case SyscallType::kUint8:
      return std::make_unique<fidl_codec::Uint8Type>();
    case SyscallType::kUint8Hexa:
      return std::make_unique<fidl_codec::Uint8Type>(fidl_codec::Uint8Type::Kind::kHexaDecimal);
    case SyscallType::kUint16:
      return std::make_unique<fidl_codec::Uint16Type>();
    case SyscallType::kUint16Hexa:
      return std::make_unique<fidl_codec::Uint16Type>(fidl_codec::Uint16Type::Kind::kHexaDecimal);
    case SyscallType::kUint32:
      return std::make_unique<fidl_codec::Uint32Type>();
    case SyscallType::kUint32Hexa:
      return std::make_unique<fidl_codec::Uint32Type>(fidl_codec::Uint32Type::Kind::kHexaDecimal);
    case SyscallType::kUint64:
      return std::make_unique<fidl_codec::Uint64Type>();
    case SyscallType::kUint64Hexa:
      return std::make_unique<fidl_codec::Uint64Type>(fidl_codec::Uint64Type::Kind::kHexaDecimal);
    case SyscallType::kUint128Hexa:
      return std::make_unique<fidl_codec::StructType>(GetUint128StructDefinition(), false);
    case SyscallType::kUintptr:
      return std::make_unique<fidl_codec::Uint64Type>(fidl_codec::Uint64Type::Kind::kUintptr);
    case SyscallType::kVaddr:
      return std::make_unique<fidl_codec::Uint64Type>(fidl_codec::Uint64Type::Kind::kVaddr);
    case SyscallType::kVcpu:
      return std::make_unique<fidl_codec::Uint32Type>(fidl_codec::Uint32Type::Kind::kVcpu);
    case SyscallType::kVmOption:
      return std::make_unique<fidl_codec::Uint32Type>(fidl_codec::Uint32Type::Kind::kVmOption);
    case SyscallType::kVmoCreationOption:
      return std::make_unique<fidl_codec::Uint32Type>(
          fidl_codec::Uint32Type::Kind::kVmoCreationOption);
    case SyscallType::kVmoOp:
      return std::make_unique<fidl_codec::Uint32Type>(fidl_codec::Uint32Type::Kind::kVmoOp);
    case SyscallType::kVmoOption:
      return std::make_unique<fidl_codec::Uint32Type>(fidl_codec::Uint32Type::Kind::kVmoOption);
    case SyscallType::kVmoType:
      return std::make_unique<fidl_codec::Uint32Type>(fidl_codec::Uint32Type::Kind::kVmoType);
    default:
      return nullptr;
  }
}

std::unique_ptr<fidl_codec::Type> AccessBase::ComputeType() const {
  return SyscallTypeToFidlCodecType(GetSyscallType());
}

std::unique_ptr<fidl_codec::Type> SyscallInputOutputBase::ComputeType() const { return nullptr; }

std::unique_ptr<fidl_codec::Value> SyscallInputOutputBase::GenerateValue(
    SyscallDecoderInterface* decoder, Stage stage) const {
  return std::make_unique<fidl_codec::InvalidValue>();
}

bool SyscallInputOutputBase::GetAutomationInstructions(
    const std::vector<debug::RegisterID>& argument_indexes, bool is_invoked,
    const std::vector<debug_ipc::AutomationCondition>& conditions, Syscall& syscall) {
  return false;
}

void SyscallFidlMessageBase::LoadBytes(SyscallDecoderInterface* decoder, Stage stage) const {
  handle_->Load(decoder, stage);
  options_->Load(decoder, stage);
  num_bytes_->Load(decoder, stage);
  if (num_bytes_->Loaded(decoder, stage)) {
    uint32_t count = num_bytes_->Value(decoder, stage);
    bool use_iovec = false;
    if ((type_ == fidl_codec::SyscallFidlType::kOutputMessage) ||
        (type_ == fidl_codec::SyscallFidlType::kOutputRequest)) {
      uint32_t options = options_->Value(decoder, stage);
      if ((options & ZX_CHANNEL_WRITE_USE_IOVEC) != 0) {
        use_iovec = true;
      }
    }
    if (count > 0) {
      if (use_iovec) {
        bytes_->LoadArray(decoder, stage, count * sizeof(zx_channel_iovec_t));
        if (bytes_->ArrayLoaded(decoder, stage, count * sizeof(zx_channel_iovec_t))) {
          auto iovec = reinterpret_cast<const zx_channel_iovec_t*>(bytes_->Content(decoder, stage));
          for (uint32_t buffer = 0; buffer < count; ++buffer) {
            decoder->LoadBuffer(stage, reinterpret_cast<uint64_t>(iovec[buffer].buffer),
                                iovec[buffer].capacity);
          }
        }
      } else {
        bytes_->LoadArray(decoder, stage, count);
      }
    }
  }
}

SyscallFidlMessageBase::ByteBuffer::ByteBuffer(SyscallDecoderInterface* decoder, Stage stage,
                                               const SyscallFidlMessageBase* from) {
  uint32_t options_value = from->options()->Value(decoder, stage);
  if (((from->type() == fidl_codec::SyscallFidlType::kOutputMessage) ||
       (from->type() == fidl_codec::SyscallFidlType::kOutputRequest)) &&
      ((options_value & ZX_CHANNEL_WRITE_USE_IOVEC) != 0)) {
    // For the iovec case, we need to concatanate all the buffers into one.
    const zx_channel_iovec_t* iovec =
        reinterpret_cast<const zx_channel_iovec_t*>(from->bytes()->Content(decoder, stage));
    uint32_t iovec_count = from->num_bytes()->Value(decoder, stage);
    for (uint32_t i = 0; i < iovec_count; ++i) {
      count_ += static_cast<uint32_t>(iovec[i].capacity);
    }
    buffer_ = new uint8_t[count_];
    uint8_t* dst = buffer_;
    for (uint32_t i = 0; i < iovec_count; ++i) {
      const uint8_t* data =
          decoder->BufferContent(stage, reinterpret_cast<uint64_t>(iovec[i].buffer));
      memcpy(dst, data, iovec[i].capacity);
      dst += iovec[i].capacity;
    }
    bytes_ = buffer_;
  } else {
    bytes_ = from->bytes()->Content(decoder, stage);
    count_ = from->num_bytes()->Value(decoder, stage);
  }
}

std::unique_ptr<fidl_codec::Type> SyscallFidlMessageHandle::ComputeType() const {
  return std::make_unique<fidl_codec::FidlMessageType>();
}

std::unique_ptr<fidl_codec::Value> SyscallFidlMessageHandle::GenerateValue(
    SyscallDecoderInterface* decoder, Stage stage) const {
  zx_handle_t handle_value = handle()->Value(decoder, stage);
  ByteBuffer buffer(decoder, stage, this);
  const zx_handle_t* handles_value = handles()->Content(decoder, stage);
  uint32_t num_handles_value = num_handles()->Value(decoder, stage);
  zx_handle_disposition_t* handle_dispositions_value = nullptr;
  if (num_handles_value > 0) {
    handle_dispositions_value = new zx_handle_disposition_t[num_handles_value];
    for (uint32_t i = 0; i < num_handles_value; ++i) {
      handle_dispositions_value[i].operation = fidl_codec::kNoHandleDisposition;
      handle_dispositions_value[i].handle = handles_value[i];
      handle_dispositions_value[i].rights = 0;
      handle_dispositions_value[i].type = ZX_OBJ_TYPE_NONE;
      handle_dispositions_value[i].result = ZX_OK;
    }
  }
  fidl_codec::DecodedMessage message;
  std::stringstream error_stream;
  message.DecodeMessage(decoder->dispatcher()->MessageDecoderDispatcher(),
                        decoder->fidlcat_thread()->process()->koid(), handle_value, buffer.bytes(),
                        buffer.count(), handle_dispositions_value, num_handles_value, type(),
                        error_stream);
  auto result = std::make_unique<fidl_codec::FidlMessageValue>(
      &message, error_stream.str(), buffer.bytes(), buffer.count(), handle_dispositions_value,
      num_handles_value);
  delete[] handle_dispositions_value;
  if (result->is_request()) {
    if (result->matched_request()) {
      decoder->set_semantic(result->method()->semantic());
      decoder->set_decoded_request(result->decoded_request());
    }
    if (result->matched_response()) {
      decoder->set_semantic(result->method()->semantic());
      decoder->set_decoded_response(result->decoded_response());
    }
  }
  return result;
}

std::unique_ptr<fidl_codec::Type> SyscallFidlMessageHandleInfo::ComputeType() const {
  return std::make_unique<fidl_codec::FidlMessageType>();
}

std::unique_ptr<fidl_codec::Value> SyscallFidlMessageHandleInfo::GenerateValue(
    SyscallDecoderInterface* decoder, Stage stage) const {
  zx_handle_t handle_value = handle()->Value(decoder, stage);
  ByteBuffer buffer(decoder, stage, this);
  const zx_handle_info_t* handle_infos_value = handles()->Content(decoder, stage);
  uint32_t num_handles_value = num_handles()->Value(decoder, stage);
  zx_handle_disposition_t* handle_dispositions_value = nullptr;
  if (num_handles_value > 0) {
    handle_dispositions_value = new zx_handle_disposition_t[num_handles_value];
    for (uint32_t i = 0; i < num_handles_value; ++i) {
      handle_dispositions_value[i].operation = fidl_codec::kNoHandleDisposition;
      handle_dispositions_value[i].handle = handle_infos_value[i].handle;
      handle_dispositions_value[i].type = handle_infos_value[i].type;
      handle_dispositions_value[i].rights = handle_infos_value[i].rights;
      handle_dispositions_value[i].result = ZX_OK;
    }
  }
  fidl_codec::DecodedMessage message;
  std::stringstream error_stream;
  message.DecodeMessage(decoder->dispatcher()->MessageDecoderDispatcher(),
                        decoder->fidlcat_thread()->process()->koid(), handle_value, buffer.bytes(),
                        buffer.count(), handle_dispositions_value, num_handles_value, type(),
                        error_stream);
  auto result = std::make_unique<fidl_codec::FidlMessageValue>(
      &message, error_stream.str(), buffer.bytes(), buffer.count(), handle_dispositions_value,
      num_handles_value);
  delete[] handle_dispositions_value;
  if (result->is_request()) {
    if (result->matched_request()) {
      decoder->set_semantic(result->method()->semantic());
      decoder->set_decoded_request(result->decoded_request());
    }
    if (result->matched_response()) {
      decoder->set_semantic(result->method()->semantic());
      decoder->set_decoded_response(result->decoded_response());
    }
  }
  return result;
}

std::unique_ptr<fidl_codec::Type> SyscallFidlMessageHandleDisposition::ComputeType() const {
  return std::make_unique<fidl_codec::FidlMessageType>();
}

std::unique_ptr<fidl_codec::Value> SyscallFidlMessageHandleDisposition::GenerateValue(
    SyscallDecoderInterface* decoder, Stage stage) const {
  zx_handle_t handle_value = handle()->Value(decoder, stage);
  ByteBuffer buffer(decoder, stage, this);
  const zx_handle_disposition_t* handle_dispositions_value = handles()->Content(decoder, stage);
  uint32_t num_handles_value = num_handles()->Value(decoder, stage);
  fidl_codec::DecodedMessage message;
  std::stringstream error_stream;
  message.DecodeMessage(decoder->dispatcher()->MessageDecoderDispatcher(),
                        decoder->fidlcat_thread()->process()->koid(), handle_value, buffer.bytes(),
                        buffer.count(), handle_dispositions_value, num_handles_value, type(),
                        error_stream);
  auto result = std::make_unique<fidl_codec::FidlMessageValue>(
      &message, error_stream.str(), buffer.bytes(), buffer.count(), handle_dispositions_value,
      num_handles_value);
  if (result->is_request()) {
    if (result->matched_request()) {
      decoder->set_semantic(result->method()->semantic());
      decoder->set_decoded_request(result->decoded_request());
    }
    if (result->matched_response()) {
      decoder->set_semantic(result->method()->semantic());
      decoder->set_decoded_response(result->decoded_response());
    }
  }
  return result;
}

void ComputeTypes(const std::vector<std::unique_ptr<SyscallInputOutputBase>>& fields,
                  std::vector<std::unique_ptr<fidl_codec::StructMember>>* inline_members,
                  std::vector<std::unique_ptr<fidl_codec::StructMember>>* outline_members) {
  for (const auto& field : fields) {
    std::unique_ptr<fidl_codec::Type> type = field->ComputeType();
    if (field->InlineValue()) {
      inline_members->emplace_back(
          std::make_unique<fidl_codec::StructMember>(field->name(), std::move(type), field->id()));
    } else {
      outline_members->emplace_back(
          std::make_unique<fidl_codec::StructMember>(field->name(), std::move(type), field->id()));
    }
  }
}

void Syscall::ComputeTypes() {
  fidlcat::ComputeTypes(inputs_, &input_inline_members_, &input_outline_members_);
  fidlcat::ComputeTypes(outputs_, &output_inline_members_, &output_outline_members_);
}

const fidl_codec::StructMember* Syscall::SearchInlineMember(const std::string& name,
                                                            bool invoked) const {
  if (invoked) {
    for (const auto& member : input_inline_members_) {
      if (member->name() == name) {
        return member.get();
      }
    }
  } else {
    for (const auto& member : output_inline_members_) {
      if (member->name() == name) {
        return member.get();
      }
    }
  }
  return nullptr;
}

const fidl_codec::StructMember* Syscall::SearchInlineMember(uint32_t id, bool invoked) const {
  if (invoked) {
    for (const auto& member : input_inline_members_) {
      if (member->id() == id) {
        return member.get();
      }
    }
  } else {
    for (const auto& member : output_inline_members_) {
      if (member->id() == id) {
        return member.get();
      }
    }
  }
  return nullptr;
}

const fidl_codec::StructMember* Syscall::SearchOutlineMember(const std::string& name,
                                                             bool invoked) const {
  if (invoked) {
    for (const auto& member : input_outline_members_) {
      if (member->name() == name) {
        return member.get();
      }
    }
  } else {
    for (const auto& member : output_outline_members_) {
      if (member->name() == name) {
        return member.get();
      }
    }
  }
  return nullptr;
}

const fidl_codec::StructMember* Syscall::SearchOutlineMember(uint32_t id, bool invoked) const {
  if (invoked) {
    for (const auto& member : input_outline_members_) {
      if (member->id() == id) {
        return member.get();
      }
    }
  } else {
    for (const auto& member : output_outline_members_) {
      if (member->id() == id) {
        return member.get();
      }
    }
  }
  return nullptr;
}

void Syscall::ComputeStatistics(const OutputEvent* event) const {
  if (compute_statistics_ != nullptr) {
    (*compute_statistics_)(event);
  }
}

bool ComputeAutomation(const std::vector<debug::RegisterID>& argument_indexes, debug::Arch arch,
                       const std::vector<std::unique_ptr<SyscallInputOutputBase>>& fields,
                       bool is_invoked, Syscall& syscall) {
  bool fully_automated = true;
  for (const auto& field : fields) {
    std::vector<debug_ipc::AutomationCondition> automation_conditions;
    for (const auto& condition : *(field->conditions())) {
      if (!condition->ComputeAutomationCondition(argument_indexes, is_invoked, arch, syscall,
                                                 automation_conditions)) {
        continue;
      }
    }
    if (!field->GetAutomationInstructions(argument_indexes, is_invoked, automation_conditions,
                                          syscall)) {
      fully_automated = false;
    }
  }
  return fully_automated;
}

void Syscall::ComputeAutomation(debug::Arch arch) {
  if (invoked_bp_instructions_.size() + exit_bp_instructions_.size() > 0) {
    return;
  }

  static const std::vector<debug::RegisterID> amd64_argument_indexes = {
      debug::RegisterID::kX64_rdi, debug::RegisterID::kX64_rsi, debug::RegisterID::kX64_rdx,
      debug::RegisterID::kX64_rcx, debug::RegisterID::kX64_r8,  debug::RegisterID::kX64_r9};

  static const std::vector<debug::RegisterID> arm64_argument_indexes = {
      debug::RegisterID::kARMv8_x0, debug::RegisterID::kARMv8_x1, debug::RegisterID::kARMv8_x2,
      debug::RegisterID::kARMv8_x3, debug::RegisterID::kARMv8_x4, debug::RegisterID::kARMv8_x5,
      debug::RegisterID::kARMv8_x6, debug::RegisterID::kARMv8_x7};
  const std::vector<debug::RegisterID>* arg_index;
  if (arch == debug::Arch::kX64) {
    arg_index = &amd64_argument_indexes;
  } else if (arch == debug::Arch::kArm64) {
    arg_index = &arm64_argument_indexes;
  } else {
    FX_LOGS(ERROR) << "Unknown architecture";
    return;
  }

  bool initial_automated = fidlcat::ComputeAutomation(*arg_index, arch, inputs_, true, *this);
  bool exit_automated = fidlcat::ComputeAutomation(*arg_index, arch, outputs_, false, *this);
  fully_automated_ = initial_automated && exit_automated;
  debug_ipc::AutomationInstruction clear_instr;
  if (invoked_bp_instructions_.size() + exit_bp_instructions_.size() > 0) {
    clear_instr.InitClearStoredValues(std::vector<debug_ipc::AutomationCondition>());
    exit_bp_instructions_.emplace_back(clear_instr);
  }
}

SyscallDecoderDispatcher::SyscallDecoderDispatcher(const DecodeOptions& decode_options)
    : decode_options_(decode_options), inference_(this) {
  Populate();
  ComputeTypes();
  if (!decode_options.trigger_filters.empty()) {
    // We have at least one trigger => wait for a message satisfying the trigger before displaying
    // any syscall.
    display_started_ = false;
  }
  if (!decode_options.message_filters.empty() || !decode_options.exclude_message_filters.empty()) {
    has_filter_ = true;
  }
  if ((decode_options.stack_level != kNoStack) || !decode_options_.save.empty()) {
    needs_stack_frame_ = true;
  }
  if (!decode_options_.save.empty()) {
    needs_to_save_events_ = true;
  } else {
    switch (decode_options_.output_mode) {
      case OutputMode::kNone:
      case OutputMode::kStandard:
        break;
      case OutputMode::kTextProtobuf:
        needs_to_save_events_ = true;
        break;
    }
  }
}

HandleInfo* SyscallDecoderDispatcher::CreateHandleInfo(Thread* thread, uint32_t handle,
                                                       int64_t creation_time, bool startup) {
  auto old_value = thread->process()->SearchHandleInfo(handle);
  if (old_value != nullptr) {
    return old_value;
  }
  auto result = std::make_unique<HandleInfo>(thread, handle, creation_time, startup);
  auto returned_value = result.get();
  thread->process()->handle_infos().emplace_back(result.get());
  thread->process()->handle_info_map().emplace(std::make_pair(handle, result.get()));
  handle_infos_.emplace_back(std::move(result));
  return returned_value;
}

void SyscallDecoderDispatcher::DecodeSyscall(InterceptingThreadObserver* thread_observer,
                                             zxdb::Thread* thread, Syscall* syscall,
                                             uint64_t timestamp) {
  uint64_t thread_id = thread->GetKoid();
  auto current = syscall_decoders_.find(thread_id);
  if (current != syscall_decoders_.end()) {
    FX_LOGS(ERROR) << thread->GetProcess()->GetName() << ' ' << thread->GetProcess()->GetKoid()
                   << ':' << thread_id << ": Internal error: already decoding the thread";
    return;
  }
  auto decoder =
      std::make_unique<SyscallDecoder>(this, thread_observer, thread, syscall, timestamp);
  auto tmp = decoder.get();
  syscall_decoders_[thread_id] = std::move(decoder);
  tmp->Decode();
}

void SyscallDecoderDispatcher::DecodeException(InterceptionWorkflow* workflow, zxdb::Thread* thread,
                                               uint64_t timestamp) {
  uint64_t thread_id = thread->GetKoid();
  auto current = exception_decoders_.find(thread_id);
  if (current != exception_decoders_.end()) {
    FX_LOGS(ERROR) << thread->GetProcess()->GetName() << ' ' << thread->GetProcess()->GetKoid()
                   << ':' << thread_id
                   << ": Internal error: already decoding an exception for the thread";
    return;
  }
  auto decoder = std::make_unique<ExceptionDecoder>(workflow, this, thread, timestamp);
  auto tmp = decoder.get();
  exception_decoders_[thread_id] = std::move(decoder);
  tmp->Decode();
}

void SyscallDecoderDispatcher::DeleteDecoder(SyscallDecoder* decoder) {
  if (!decoder->aborted()) {
    zxdb::Thread* thread = decoder->get_thread();
    if (thread != nullptr) {
      thread->Continue(false);
    }
  }
  syscall_decoders_.erase(decoder->fidlcat_thread()->koid());
}

void SyscallDecoderDispatcher::DeleteDecoder(ExceptionDecoder* decoder) {
  zxdb::Thread* thread = decoder->get_thread();
  if (thread != nullptr) {
    thread->Continue(false);
  }
  exception_decoders_.erase(decoder->thread_id());
}

void SyscallDecoderDispatcher::AddStopMonitoringEvent(std::shared_ptr<StopMonitoringEvent> event) {
  for (const auto& decoder : syscall_decoders_) {
    if (decoder.second->fidlcat_thread()->process() == event->process()) {
      decoder.second->set_aborted();
    }
  }
}

void SyscallDecoderDispatcher::SaveEvent(std::shared_ptr<Event> event) {
  if (needs_to_save_events()) {
    decoded_events_.emplace_back(std::move(event));
  }
}

void SyscallDecoderDispatcher::SessionEnded() {
  bool generate_proto_session = false;
  if (!decode_options_.save.empty()) {
    generate_proto_session = true;
  } else {
    switch (decode_options_.output_mode) {
      case OutputMode::kNone:
      case OutputMode::kStandard:
        break;
      case OutputMode::kTextProtobuf:
        generate_proto_session = true;
        break;
    }
  }
  if (generate_proto_session) {
    proto::Session session;
    GenerateProtoSession(&session);
    if (!decode_options_.save.empty()) {
      std::fstream output(decode_options_.save, std::ios::out | std::ios::trunc | std::ios::binary);
      if (output.fail()) {
        FX_LOGS(ERROR) << "Can't open <" << decode_options_.save << "> for writing.";
      } else if (!session.SerializeToOstream(&output)) {
        FX_LOGS(ERROR) << "Failed to write session to protobuf file <" << decode_options_.save
                       << ">.";
      }
    }
    switch (decode_options_.output_mode) {
      case OutputMode::kNone:
      case OutputMode::kStandard:
        break;
      case OutputMode::kTextProtobuf:
        std::cout << session.DebugString();
        break;
    }
  }
}

void SyscallDecoderDispatcher::GenerateProtoSession(proto::Session* session) {
  for (const auto& process : processes_) {
    proto::Process* proto_process = session->add_process();
    proto_process->set_koid(process.second->koid());
    proto_process->set_name(process.second->name());
    auto process_semantic = inference().GetProcessSemantic(process.second->koid());
    if (process_semantic != nullptr) {
      for (const auto& linked_handles : process_semantic->linked_handles) {
        if (linked_handles.first < linked_handles.second) {
          proto::LinkedHandles* proto_linked_handles = proto_process->add_linked_handles();
          proto_linked_handles->set_handle_0(linked_handles.first);
          proto_linked_handles->set_handle_1(linked_handles.second);
        }
      }
    }
  }
  for (const auto& thread : threads_) {
    proto::Thread* proto_thread = session->add_thread();
    proto_thread->set_koid(thread.second->koid());
    proto_thread->set_process_koid(thread.second->process()->koid());
  }
  for (const auto& handle_info : handle_infos_) {
    fidl_codec::semantic::InferredHandleInfo* inferred_handle_info =
        inference().GetInferredHandleInfo(handle_info->thread()->process()->koid(),
                                          handle_info->handle());
    proto::HandleDescription* proto_handle_description = session->add_handle_description();
    proto_handle_description->set_handle(handle_info->handle());
    proto_handle_description->set_thread_koid(handle_info->thread()->koid());
    proto_handle_description->set_creation_time(handle_info->creation_time());
    proto_handle_description->set_startup(handle_info->startup());
    if (inferred_handle_info != nullptr) {
      proto_handle_description->set_type(inferred_handle_info->type());
      proto_handle_description->set_fd(inferred_handle_info->fd());
      proto_handle_description->set_path(inferred_handle_info->path());
      proto_handle_description->set_attributes(inferred_handle_info->attributes());
    }
    proto_handle_description->set_koid(handle_info->koid());
    proto_handle_description->set_object_type(handle_info->object_type());
  }
  for (const auto& linked_koids : inference().linked_koids()) {
    if (linked_koids.first < linked_koids.second) {
      proto::LinkedKoids* proto_linked_koids = session->add_linked_koids();
      proto_linked_koids->set_koid_0(linked_koids.first);
      proto_linked_koids->set_koid_1(linked_koids.second);
    }
  }
  for (const auto& event : decoded_events_) {
    event->Write(session->add_event());
  }
}

void SyscallDecoderDispatcher::ComputeTypes() {
  for (const auto& syscall : syscalls_) {
    syscall.second->ComputeTypes();
  }
}

double SyscallDisplayDispatcher::GetTime(int64_t timestamp) {
  return static_cast<double>(timestamp) / 1000000000;
}

void SyscallDisplayDispatcher::AddProcessLaunchedEvent(
    std::shared_ptr<ProcessLaunchedEvent> event) {
  if (decode_options().output_mode == OutputMode::kStandard) {
    if (!decode_options().thread_filters.empty()) {
      return;
    }
    os_ << '\n' << colors().green << GetTime(event->timestamp()) << colors().reset << ' ';
    if (event->error_message().empty()) {
      os_ << colors().green << "Launched " << colors().blue << event->command() << colors().reset
          << '\n';
    } else {
      os_ << colors().red << "Can't launch " << colors().blue << event->command() << colors().reset
          << " : " << colors().red << event->error_message() << colors().reset << '\n';
    }
  }

  SaveEvent(std::move(event));
}

void SyscallDisplayDispatcher::AddProcessMonitoredEvent(
    std::shared_ptr<ProcessMonitoredEvent> event) {
  if (!decode_options().thread_filters.empty()) {
    return;
  }
  if (decode_options().output_mode == OutputMode::kStandard) {
    os_ << '\n' << colors().green << GetTime(event->timestamp()) << colors().reset << ' ';
    if (event->error_message().empty()) {
      os_ << colors().green << "Monitoring ";
    } else {
      os_ << colors().red << "Can't monitor ";
    }

    if (event->process()->name().empty()) {
      os_ << colors().reset << "process with koid ";
    } else {
      os_ << colors().blue << event->process()->name() << colors().reset << " koid=";
    }

    os_ << colors().red << event->process()->koid() << colors().reset;
    if (!event->error_message().empty()) {
      os_ << " : " << colors().red << event->error_message() << colors().reset;
    }
    os_ << '\n';
  }

  SaveEvent(std::move(event));
}

void SyscallDisplayDispatcher::AddStopMonitoringEvent(std::shared_ptr<StopMonitoringEvent> event) {
  if (!decode_options().thread_filters.empty()) {
    return;
  }
  if (decode_options().output_mode == OutputMode::kStandard) {
    os_ << '\n' << colors().green << GetTime(event->timestamp()) << colors().reset << ' ';
    if (event->process()->name().empty()) {
      os_ << colors().green << "Stop monitoring process with koid" << colors().reset;
    } else {
      os_ << colors().green << "Stop monitoring" << colors().reset << ' ' << colors().blue
          << event->process()->name() << colors().reset << " koid";
    }
    os_ << ' ' << colors().red << event->process()->koid() << colors().reset << '\n';
  }

  SaveEvent(event);

  SyscallDecoderDispatcher::AddStopMonitoringEvent(std::move(event));
}

void SyscallDisplayDispatcher::SyscallDecodingError(const fidlcat::Thread* fidlcat_thread,
                                                    const Syscall* syscall,
                                                    const DecoderError& error) {
  std::string message = error.message();
  size_t pos = 0;
  for (;;) {
    size_t end = message.find('\n', pos);
    os_ << fidlcat_thread->process()->name() << ' ' << colors().red
        << fidlcat_thread->process()->koid() << colors().reset << ':' << colors().red
        << fidlcat_thread->koid() << colors().reset << ' ' << syscall->name() << ": "
        << colors().red << error.message().substr(pos, end) << colors().reset << '\n';
    if (end == std::string::npos) {
      break;
    }
    pos = end + 1;
  }
  os_ << '\n';
}

void SyscallDisplayDispatcher::AddInvokedEvent(std::shared_ptr<InvokedEvent> invoked_event) {
  invoked_event->set_id(GetNextInvokedEventId());
  if (!extra_generation().empty()) {
    invoked_event->ComputeHandleInfo(this);
  }
  if (!invoked_event->thread()->displayed()) {
    return;
  }
  if (!display_started()) {
    // The user specified a trigger. Check if this is a message which satisfies one of the triggers.
    const fidl_codec::FidlMessageValue* message = invoked_event->GetMessage();
    if ((message == nullptr) || (message->method() == nullptr) ||
        !decode_options().IsTrigger(message->method()->fully_qualified_name())) {
      return;
    }
    // We found a trigger => allow the display.
    set_display_started();
  }
  if (has_filter() && invoked_event->syscall()->has_fidl_message()) {
    // We have filters and this is a syscalls with a FIDL message.
    // Only display the syscall if the message satifies the conditions.
    const fidl_codec::FidlMessageValue* message = invoked_event->GetMessage();
    if ((message == nullptr) || (message->method() == nullptr) ||
        !decode_options().SatisfiesMessageFilters(message->method()->fully_qualified_name())) {
      return;
    }
  }
  invoked_event->set_displayed();
  DisplayInvokedEvent(invoked_event.get());

  SaveEvent(std::move(invoked_event));
}

void SyscallDisplayDispatcher::AddOutputEvent(std::shared_ptr<OutputEvent> output_event) {
  if (!output_event->thread()->displayed()) {
    return;
  }
  if (!extra_generation().empty()) {
    if (output_event->invoked_event()->handle_info() != nullptr) {
      output_event->invoked_event()->handle_info()->AddEvent(output_event.get());
    }
    output_event->syscall()->ComputeStatistics(output_event.get());
  }
  if (!output_event->invoked_event()->displayed()) {
    // The display of the syscall wasn't allowed by the input arguments. Check if the output
    // arguments allows its display.
    if (!display_started()) {
      // The user specified a trigger. Check if this is a message which satisfies one of the
      // triggers.
      const fidl_codec::FidlMessageValue* message = output_event->GetMessage();
      if ((message == nullptr) || (message->method() == nullptr) ||
          !decode_options().IsTrigger(message->method()->fully_qualified_name())) {
        return;
      }
      set_display_started();
    }
    if (has_filter() && output_event->syscall()->has_fidl_message()) {
      // We have filters and this is a syscalls with a FIDL message.
      // Only display the syscall if the message satifies the conditions.
      const fidl_codec::FidlMessageValue* message = output_event->GetMessage();
      if ((message == nullptr) || (message->method() == nullptr) ||
          !decode_options().SatisfiesMessageFilters(message->method()->fully_qualified_name())) {
        return;
      }
    }
    // We can display the syscall but the inputs have not been displayed => display the inputs
    // before displaying the outputs.
    DisplayInvokedEvent(output_event->invoked_event());
  }

  DisplayOutputEvent(output_event.get());

  SaveEvent(std::move(output_event));
}

void SyscallDisplayDispatcher::AddExceptionEvent(std::shared_ptr<ExceptionEvent> exception_event) {
  if (!exception_event->thread()->displayed()) {
    return;
  }

  DisplayExceptionEvent(exception_event.get());

  SaveEvent(std::move(exception_event));
}

void SyscallDisplayDispatcher::SessionEnded() {
  SyscallDecoderDispatcher::SessionEnded();
  if (!decoded_events().empty()) {
    // Uses the first event for the timestamp reference.
    GetTime(decoded_events().front()->timestamp());
  }
  const char* separator = "";
  for (const auto& extra_generation : extra_generation()) {
    if (extra_generation.path.empty()) {
      os_ << separator;
      switch (extra_generation.kind) {
        case ExtraGeneration::Kind::kSummary:
          DisplaySummary(os_);
          break;
        case ExtraGeneration::Kind::kTop: {
          Top top(this);
          top.Display(os_);
          break;
        }
        case ExtraGeneration::Kind::kThreads:
          DisplayThreads(os_);
          break;
        case ExtraGeneration::Kind::kCpp:
          GenerateTests("/tmp/fidlcat-generated-tests/" + std::to_string(std::time(0)));
          break;
      }
      separator = "\n";
    } else {
      if (extra_generation.kind == ExtraGeneration::Kind::kCpp) {
        GenerateTests(extra_generation.path);
      } else {
        std::fstream output(extra_generation.path, std::ios::out | std::ios::trunc);
        if (output.fail()) {
          FX_LOGS(ERROR) << "Can't open <" << extra_generation.path << "> for writing.";
        } else {
          switch (extra_generation.kind) {
            case ExtraGeneration::Kind::kSummary:
              DisplaySummary(output);
              break;
            case ExtraGeneration::Kind::kTop: {
              Top top(this);
              top.Display(output);
              break;
            }
            case ExtraGeneration::Kind::kThreads:
              DisplayThreads(output);
              break;
            case ExtraGeneration::Kind::kCpp:
              break;
          }
        }
      }
    }
  }
}

void SyscallDisplayDispatcher::DisplayInvokedEvent(const InvokedEvent* invoked_event) {
  if (decode_options().output_mode != OutputMode::kStandard) {
    return;
  }
  std::string line_header =
      colors().green + std::to_string(GetTime(invoked_event->timestamp())) + colors().reset + ' ' +
      invoked_event->thread()->process()->name() + ' ' + colors().red +
      std::to_string(invoked_event->thread()->process()->koid()) + colors().reset + ':' +
      colors().red + std::to_string(invoked_event->thread()->koid()) + colors().reset + ' ';
  if (with_process_info()) {
    os_ << line_header;
  }
  os_ << '\n';

  FidlcatPrinter printer(this, invoked_event->thread()->process(), os_, line_header);

  // We have been able to create values from the syscall => print them.
  invoked_event->PrettyPrint(printer);
  last_displayed_event_ = invoked_event;
}

void SyscallDisplayDispatcher::DisplayOutputEvent(const OutputEvent* output_event) {
  if (decode_options().output_mode != OutputMode::kStandard) {
    return;
  }
  if (output_event->syscall()->return_type() != SyscallReturnType::kNoReturn) {
    if (last_displayed_event_ != output_event->invoked_event()) {
      // Add a blank line to tell the user that this display is not linked to the
      // previous displayed lines.
      os_ << "\n";
    }
    std::string line_header;
    if (with_process_info() || (last_displayed_event_ != output_event->invoked_event())) {
      line_header = colors().green + std::to_string(GetTime(output_event->timestamp())) +
                    colors().reset + ' ' + output_event->thread()->process()->name() + ' ' +
                    colors().red + std::to_string(output_event->thread()->process()->koid()) +
                    colors().reset + ':' + colors().red +
                    std::to_string(output_event->thread()->koid()) + colors().reset + ' ';
    } else {
      line_header = colors().green + std::to_string(GetTime(output_event->timestamp())) +
                    colors().reset + ' ';
    }
    FidlcatPrinter printer(this, output_event->thread()->process(), os_, line_header);
    // We have been able to create values from the syscall => print them.
    output_event->PrettyPrint(printer);

    last_displayed_event_ = output_event;
  }
}

void SyscallDisplayDispatcher::DisplayExceptionEvent(const ExceptionEvent* exception_event) {
  if (decode_options().output_mode != OutputMode::kStandard) {
    return;
  }
  os_ << '\n';

  std::string line_header =
      colors().green + std::to_string(GetTime(exception_event->timestamp())) + colors().reset +
      ' ' + exception_event->thread()->process()->name() + ' ' + colors().red +
      std::to_string(exception_event->thread()->process()->koid()) + colors().reset + ':' +
      colors().red + std::to_string(exception_event->thread()->koid()) + colors().reset + ' ';
  FidlcatPrinter printer(this, exception_event->thread()->process(), os_, line_header);
  exception_event->PrettyPrint(printer);
}

void SyscallCompareDispatcher::SyscallDecodingError(const fidlcat::Thread* fidlcat_thread,
                                                    const Syscall* syscall,
                                                    const DecoderError& error) {
  os_.clear();
  os_.str("");
  SyscallDisplayDispatcher::SyscallDecodingError(fidlcat_thread, syscall, error);
  comparator_->DecodingError(os_.str());
}

void SyscallCompareDispatcher::DisplayInvokedEvent(const InvokedEvent* invoked_event) {
  os_.clear();
  os_.str("");
  SyscallDisplayDispatcher::DisplayInvokedEvent(invoked_event);
  comparator_->CompareInput(os_.str(), invoked_event->thread()->process()->name(),
                            invoked_event->thread()->process()->koid(),
                            invoked_event->thread()->koid());
}

void SyscallCompareDispatcher::DisplayOutputEvent(const OutputEvent* output_event) {
  os_.clear();
  os_.str("");
  SyscallDisplayDispatcher::DisplayOutputEvent(output_event);
  if (output_event->syscall()->return_type() != SyscallReturnType::kNoReturn) {
    comparator_->CompareOutput(os_.str(), output_event->thread()->process()->name(),
                               output_event->thread()->process()->koid(),
                               output_event->thread()->koid());
  }
}

void SyscallDisplayDispatcher::GenerateTests(const std::string& output_directory) {
  auto test_generator = TestGenerator(this, output_directory);
  test_generator.GenerateTests();
}

}  // namespace fidlcat
