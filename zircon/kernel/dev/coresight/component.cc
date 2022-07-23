// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "dev/coresight/component.h"

#include <stdio.h>

namespace coresight {

DeviceTypeRegister::Type DeviceTypeRegister::type() const {
  // Allows us to switch across (major, sub-) type pairs.
#define TYPE(major, sub) ((sub << 4) | static_cast<uint8_t>(major))

  // [CS] Table B2-9.
  switch (TYPE(major(), sub())) {
    case TYPE(MajorType::kMiscellaneous, 0x4):
      return Type::kValidationComponent;
    case TYPE(MajorType::kTraceSink, 0x1):
      return Type::kTracePort;
    case TYPE(MajorType::kTraceSink, 0x2):
      return Type::kTraceBuffer;
    case TYPE(MajorType::kTraceSink, 0x3):
      return Type::kTraceRouter;
    case TYPE(MajorType::kTraceLink, 0x1):
      return Type::kTraceFunnel;
    case TYPE(MajorType::kTraceLink, 0x2):
      return Type::kTraceFilter;
    case TYPE(MajorType::kTraceLink, 0x3):
      return Type::kTraceFifo;
    case TYPE(MajorType::kTraceSource, 0x1):
      return Type::kCpuTraceSource;
    case TYPE(MajorType::kTraceSource, 0x2):
      return Type::kDspTraceSource;
    case TYPE(MajorType::kTraceSource, 0x3):
      return Type::kDataEngineTraceSource;
    case TYPE(MajorType::kTraceSource, 0x4):
      return Type::kBusTraceSource;
    case TYPE(MajorType::kTraceSource, 0x6):
      return Type::kSoftwareTraceSource;
    case TYPE(MajorType::kDebugControl, 0x1):
      return Type::kTriggerMatrix;
    case TYPE(MajorType::kDebugControl, 0x2):
      return Type::kDebugAuthenticationModule;
    case TYPE(MajorType::kDebugControl, 0x3):
      return Type::kPowerRequestor;
    case TYPE(MajorType::kDebugLogic, 0x1):
      return Type::kCpuDebugLogic;
    case TYPE(MajorType::kDebugLogic, 0x2):
      return Type::kDspDebugLogic;
    case TYPE(MajorType::kDebugLogic, 0x3):
      return Type::kDataEngineDebugLogic;
    case TYPE(MajorType::kDebugLogic, 0x4):
      return Type::kBusDebugLogic;
    case TYPE(MajorType::kDebugLogic, 0x5):
      return Type::kMemoryDebugLogic;
    case TYPE(MajorType::kPerformanceMonitor, 0x1):
      return Type::kCpuPerformanceMonitor;
    case TYPE(MajorType::kPerformanceMonitor, 0x2):
      return Type::kDspPerformanceMonitor;
    case TYPE(MajorType::kPerformanceMonitor, 0x3):
      return Type::kDataEnginePerformanceMonitor;
    case TYPE(MajorType::kPerformanceMonitor, 0x4):
      return Type::kBusPerformanceMonitor;
    case TYPE(MajorType::kPerformanceMonitor, 0x5):
      return Type::kMmuPerformanceMonitor;
    default:
      break;
  };
#undef TYPE
  return Type::kUnknown;
}

std::string_view ToString(ComponentIdRegister::Class classid) {
  switch (classid) {
    case ComponentIdRegister::Class::kGenericVerification:
      return "generic verification";
    case ComponentIdRegister::Class::k0x1RomTable:
      return "0x1 ROM table";
    case ComponentIdRegister::Class::kCoreSight:
      return "CoreSight";
    case ComponentIdRegister::Class::kPeripheralTestBlock:
      return "peripheral test block";
    case ComponentIdRegister::Class::kGenericIp:
      return "generic IP";
    case ComponentIdRegister::Class::kNonStandard:
      return "non-standard";
  }
  return "unknown";
}

std::string_view ToString(DeviceTypeRegister::Type type) {
  using Type = DeviceTypeRegister::Type;

  switch (type) {
    case Type::kUnknown:
      break;
    case Type::kValidationComponent:
      return "validation component";
    case Type::kTracePort:
      return "trace port";
    case Type::kTraceBuffer:
      return "trace buffer";
    case Type::kTraceRouter:
      return "trace router";
    case Type::kTraceFunnel:
      return "trace funnel";
    case Type::kTraceFilter:
      return "trace filter";
    case Type::kTraceFifo:
      return "trace FIFO";
    case Type::kCpuTraceSource:
      return "CPU trace source";
    case Type::kDspTraceSource:
      return "DSP trace source";
    case Type::kDataEngineTraceSource:
      return "data engine or coprocessor trace source";
    case Type::kBusTraceSource:
      return "bus trace source";
    case Type::kSoftwareTraceSource:
      return "software trace source";
    case Type::kTriggerMatrix:
      return "trigger matrix";
    case Type::kDebugAuthenticationModule:
      return "debug authentication module";
    case Type::kPowerRequestor:
      return "power requestor";
    case Type::kCpuDebugLogic:
      return "CPU debug logic";
    case Type::kDspDebugLogic:
      return "DSP debug logic";
    case Type::kDataEngineDebugLogic:
      return "data engine or coprocessor debug logic";
    case Type::kBusDebugLogic:
      return "bus debug logic";
    case Type::kMemoryDebugLogic:
      return "memory debug logic";
    case Type::kCpuPerformanceMonitor:
      return "CPU performance monitor";
    case Type::kDspPerformanceMonitor:
      return "DSP performance monitor";
    case Type::kDataEnginePerformanceMonitor:
      return "Data engine or coprocessor performance monitor";
    case Type::kBusPerformanceMonitor:
      return "bus performance monitor";
    case Type::kMmuPerformanceMonitor:
      return "MMU performance monitor";
  }
  return "unknown";
}

}  // namespace coresight
