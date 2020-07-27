// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_DEV_CORESIGHT_INCLUDE_DEV_CORESIGHT_COMPONENT_H_
#define ZIRCON_KERNEL_DEV_CORESIGHT_INCLUDE_DEV_CORESIGHT_COMPONENT_H_

#include <zircon/assert.h>

#include <string_view>

#include <hwreg/bitfields.h>

namespace coresight {

// Typically components are 4KiB in size; the spec permits them to be larger.
constexpr size_t kMinimumComponentSize = 4096;

// Device architecture constants for ARM-designed components.
namespace arm {

// DEVARCH.ARCHITECT.
constexpr uint16_t kArchitect = 0x23b;

// DEVARCH.ARCHID values.
namespace archid {

constexpr uint16_t kCTI = 0x1a14;
constexpr uint16_t kETMv3 = 0x3a13;
constexpr uint16_t kETMv4 = 0x4a13;
constexpr uint16_t kPMUv2 = 0x1a16;
constexpr uint16_t kPMUv3 = 0x2a16;
constexpr uint16_t kROMTable = 0x0af7;
constexpr uint16_t kV8Dot0A = 0x6a15;
constexpr uint16_t kV8Dot1A = 0x7a15;
constexpr uint16_t kV8Dot2A = 0x8a15;

}  // namespace archid

namespace partid {

constexpr uint16_t kCTI400 = 0x0906;  // SoC400 generation
constexpr uint16_t kCTI600 = 0x09ed;  // SoC600 generation
constexpr uint16_t kETB = 0x0907;
constexpr uint16_t kTimestampGenerator = 0x0101;
constexpr uint16_t kTMC = 0x0961;
constexpr uint16_t kTPIU = 0x0912;
constexpr uint16_t kTraceFunnel = 0x0908;
constexpr uint16_t kTraceReplicator = 0x0909;

}  // namespace partid

}  // namespace arm

// [CS] B2.2.1
// The first component identification register (CIDR1).
struct ComponentIDRegister : public hwreg::RegisterBase<ComponentIDRegister, uint32_t> {
  enum class Class : uint8_t {
    // clang-format off
    kGenericVerification = 0x0,
    k0x1ROMTable         = 0x1,
    kCoreSight           = 0x9,
    kPeripheralTestBlock = 0xB,
    kGenericIP           = 0xE,
    kNonStandard         = 0xF,  // For older components without standardized registers.
    // clang-format on
  };
  DEF_RSVDZ_FIELD(31, 8);
  // Should conventionally be called |class| to match the spec, but that is C++
  // keyword. We opt for the Germanized |classid| to match archid, powerid,
  // etc.
  DEF_ENUM_FIELD(Class, 7, 4, classid);
  // Not technically reserved, but is expected to be identically zero (and is
  // currently unused in this codebase).
  DEF_RSVDZ_FIELD(3, 0);

  static auto GetAt(uint32_t offset) {
    return hwreg::RegisterAddr<ComponentIDRegister>(offset + 0xff4);
  }
  static auto Get() { return GetAt(0u); }
};

// B.2.2
struct PeripheralID0Register : public hwreg::RegisterBase<PeripheralID0Register, uint32_t> {
  DEF_RSVDZ_FIELD(31, 8);
  DEF_FIELD(7, 0, part0);

  static auto GetAt(uint32_t offset) {
    return hwreg::RegisterAddr<PeripheralID0Register>(offset + 0xfe0);
  }
  static auto Get() { return GetAt(0); }
};

struct PeripheralID1Register : public hwreg::RegisterBase<PeripheralID1Register, uint32_t> {
  DEF_RSVDZ_FIELD(31, 8);
  DEF_FIELD(7, 4, des0);
  DEF_FIELD(3, 0, part1);

  static auto GetAt(uint32_t offset) {
    return hwreg::RegisterAddr<PeripheralID1Register>(offset + 0xfe4);
  }
  static auto Get() { return GetAt(0); }
};

struct PeripheralID2Register : public hwreg::RegisterBase<PeripheralID2Register, uint32_t> {
  DEF_RSVDZ_FIELD(31, 8);
  DEF_FIELD(7, 4, revision);
  DEF_BIT(3, jedec);
  DEF_FIELD(2, 0, des1);

  static auto GetAt(uint32_t offset) {
    return hwreg::RegisterAddr<PeripheralID2Register>(offset + 0xfe8);
  }
  static auto Get() { return GetAt(0); }
};

struct PeripheralID4Register : public hwreg::RegisterBase<PeripheralID4Register, uint32_t> {
  DEF_RSVDZ_FIELD(31, 8);
  DEF_FIELD(7, 4, size);
  DEF_FIELD(3, 0, des2);

  static auto GetAt(uint32_t offset) {
    return hwreg::RegisterAddr<PeripheralID4Register>(offset + 0xfd0);
  }
  static auto Get() { return GetAt(0); }
};

// [CS] B2.2.2
// JEDEC ID of the designer.
template <typename IoProvider>
inline uint16_t GetDesigner(IoProvider io) {
  const auto des0 = static_cast<uint16_t>(PeripheralID1Register::Get().ReadFrom(&io).des0());
  const auto des1 = static_cast<uint16_t>(PeripheralID2Register::Get().ReadFrom(&io).des1());
  const auto des2 = static_cast<uint16_t>(PeripheralID4Register::Get().ReadFrom(&io).des2());
  return static_cast<uint16_t>((des2 << 7) | (des1 << 4) | des0);
}

// [CS] B2.2.2
// This number is an ID chosen by the designer.
template <typename IoProvider>
inline uint16_t GetPartIDAt(IoProvider io, uint32_t offset) {
  const auto part0 =
      static_cast<uint16_t>(PeripheralID0Register::GetAt(offset).ReadFrom(&io).part0());
  const auto part1 =
      static_cast<uint16_t>(PeripheralID1Register::GetAt(offset).ReadFrom(&io).part1());
  return static_cast<uint16_t>((part1 << 8) | part0);
}
template <typename IoProvider>
inline uint16_t GetPartID(IoProvider io) {
  return GetPartIDAt(io, 0);
}

// B2.3.3
// Used to determine whether two components have an affinity with one another
// (e.g., if both correspond to the same CPU).
//
// This 64-bit register is actually an amalgamation of the two device affinity
// registers, DEVAFF0 and DEVAFF1. We combine them as, in practice, the
// resulting value is typically that of the 64-bit MPIDR register of the
// associated CPU.
struct DeviceAffinityRegister : public hwreg::RegisterBase<DeviceAffinityRegister, uint64_t> {
  static auto Get() { return hwreg::RegisterAddr<ComponentIDRegister>(0xfa8); }
};

// [CS] B2.3.4
// Identifies the architect and architecture of a CoreSight component
// (DEVARCH).
struct DeviceArchRegister : public hwreg::RegisterBase<DeviceArchRegister, uint32_t> {
  DEF_FIELD(31, 21, architect);
  DEF_BIT(20, present);
  DEF_FIELD(19, 16, revision);
  DEF_FIELD(15, 0, archid);

  static auto GetAt(uint32_t offset) {
    return hwreg::RegisterAddr<DeviceArchRegister>(offset + 0xfbc);
  }
  static auto Get() { return GetAt(0u); }
};

// [CS] B2.3.8
// Gives a high-level information about the type of a CoreSight component.
struct DeviceTypeRegister : public hwreg::RegisterBase<DeviceTypeRegister, uint32_t> {
  enum class MajorType : uint8_t {
    // clang-format off
    kMiscellaneous      = 0x0,
    kTraceSink          = 0x1,
    kTraceLink          = 0x2,
    kTraceSource        = 0x3,
    kDebugControl       = 0x4,
    kDebugLogic         = 0x5,
    kPerformanceMonitor = 0x6,
    // clang-format on
  };

  // The identifiable type is parametrized by the bottom byte, which is
  // comprised of the MAJOR and SUB fields. The following is not exhaustive;
  // new entries should be added as they are encountered.
#define TYPE(major, sub) ((sub << 4) | static_cast<uint8_t>(major))
  enum class Type : uint8_t {
    // clang-format off
    kTracePort             = TYPE(MajorType::kTraceSink, 0x1),
    kTraceBuffer           = TYPE(MajorType::kTraceSink, 0x2),
    kTraceRouter           = TYPE(MajorType::kTraceSink, 0x3),
    kTraceFunnel           = TYPE(MajorType::kTraceLink, 0x1),
    kTraceFilter           = TYPE(MajorType::kTraceLink, 0x2),
    kTraceFIFO             = TYPE(MajorType::kTraceLink, 0x3),
    kCPUTraceSource        = TYPE(MajorType::kTraceSource, 0x1),
    kTriggerMatrix         = TYPE(MajorType::kDebugControl, 0x1),
    kCPUDebugLogic         = TYPE(MajorType::kDebugLogic, 0x1),
    kCPUPerformanceMonitor = TYPE(MajorType::kPerformanceMonitor, 0x1),
    // clang-format on
  };

  Type type() const { return static_cast<Type>(TYPE(major(), sub())); }
#undef TYPE

  DEF_RSVDZ_FIELD(31, 8);
  DEF_FIELD(7, 4, sub);  // Subtype.
  DEF_ENUM_FIELD(MajorType, 3, 0, major);

  static auto Get() { return hwreg::RegisterAddr<DeviceTypeRegister>(0xfcc); }
};

inline std::string_view ToString(ComponentIDRegister::Class classid) {
  switch (classid) {
    case ComponentIDRegister::Class::kGenericVerification:
      return "generic verification";
    case ComponentIDRegister::Class::k0x1ROMTable:
      return "0x1 ROM table";
    case ComponentIDRegister::Class::kCoreSight:
      return "CoreSight";
    case ComponentIDRegister::Class::kPeripheralTestBlock:
      return "peripheral test block";
    case ComponentIDRegister::Class::kGenericIP:
      return "generic IP";
    case ComponentIDRegister::Class::kNonStandard:
      return "non-standard";
    default:
      printf("unrecognized component class: %#x\n", static_cast<uint8_t>(classid));
      return "unknown";
  }
}

inline std::string_view ToString(DeviceTypeRegister::Type type) {
  switch (type) {
    case DeviceTypeRegister::Type::kTracePort:
      return "trace port";
    case DeviceTypeRegister::Type::kTraceBuffer:
      return "trace buffer";
    case DeviceTypeRegister::Type::kTraceRouter:
      return "trace router";
    case DeviceTypeRegister::Type::kTraceFunnel:
      return "trace funnel";
    case DeviceTypeRegister::Type::kTraceFilter:
      return "trace filter";
    case DeviceTypeRegister::Type::kTraceFIFO:
      return "trace FIFO";
    case DeviceTypeRegister::Type::kCPUTraceSource:
      return "CPU trace source";
    case DeviceTypeRegister::Type::kTriggerMatrix:
      return "trigger matrix";
    case DeviceTypeRegister::Type::kCPUDebugLogic:
      return "CPU debug logic";
    case DeviceTypeRegister::Type::kCPUPerformanceMonitor:
      return "CPU performance monitor";
    default: {
      // See DeviceTypeRegister::Type documentation.
      uint8_t sub = (0b11110000 & static_cast<uint8_t>(type)) >> 4;
      uint8_t major = 0b00001111 & static_cast<uint8_t>(type);
      printf("unrecognized device type: (major, sub) = (%#x, %#x)\n", major, sub);
      return "unknown";
    }
  }
}

}  // namespace coresight

#endif  // ZIRCON_KERNEL_DEV_CORESIGHT_INCLUDE_DEV_CORESIGHT_COMPONENT_H_
