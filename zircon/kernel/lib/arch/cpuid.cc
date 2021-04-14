// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/x86/cpuid.h>

namespace arch {

namespace {

constexpr std::optional<bool> IsFullyAssociative(CpuidL2L3Associativity assoc) {
  switch (assoc) {
    case CpuidL2L3Associativity::kDisabled:
      return std::nullopt;
    case CpuidL2L3Associativity::kFullyAssociative:
      return true;
    default:
      return false;
  }
}

constexpr size_t ToWays(CpuidL2L3Associativity assoc) {
  switch (assoc) {
    case CpuidL2L3Associativity::kDisabled:
    case CpuidL2L3Associativity::kSeeLeaf0x8000001d:
    case CpuidL2L3Associativity::kFullyAssociative:
      return 0;
    case CpuidL2L3Associativity::kDirectMapped:
      return 1;
    case CpuidL2L3Associativity::k2Way:
      return 2;
    case CpuidL2L3Associativity::k3Way:
      return 3;
    case CpuidL2L3Associativity::k4Way:
      return 4;
    case CpuidL2L3Associativity::k6Way:
      return 6;
    case CpuidL2L3Associativity::k8Way:
      return 8;
    case CpuidL2L3Associativity::k16Way:
      return 16;
    case CpuidL2L3Associativity::k32Way:
      return 32;
    case CpuidL2L3Associativity::k48Way:
      return 48;
    case CpuidL2L3Associativity::k64Way:
      return 64;
    case CpuidL2L3Associativity::k96Way:
      return 96;
    case CpuidL2L3Associativity::k128Way:
      return 128;
  }
  return 0;
}

}  // namespace

std::string_view ToString(Vendor vendor) {
  switch (vendor) {
    case Vendor::kUnknown:
      return "Unknown";
    case Vendor::kIntel:
      return "Intel";
    case Vendor::kAmd:
      return "AMD";
  }
  __UNREACHABLE;
}

std::string_view ToString(Microarchitecture microarch) {
  switch (microarch) {
    case Microarchitecture::kUnknown:
      return "Unknown";
    case Microarchitecture::kIntelCore2:
      return "Intel Core 2";
    case Microarchitecture::kIntelNehalem:
      return "Intel Nehalem";
    case Microarchitecture::kIntelWestmere:
      return "Intel Westmere";
    case Microarchitecture::kIntelSandyBridge:
      return "Intel Sandy Bridge";
    case Microarchitecture::kIntelIvyBridge:
      return "Intel Ivy Bridge";
    case Microarchitecture::kIntelBroadwell:
      return "Intel Broadwell";
    case Microarchitecture::kIntelHaswell:
      return "Intel Haswell";
    case Microarchitecture::kIntelSkylake:
      return "Intel Skylake";
    case Microarchitecture::kIntelSkylakeServer:
      return "Intel Skylake (server)";
    case Microarchitecture::kIntelCannonLake:
      return "Intel Cannon Lake";
    case Microarchitecture::kIntelBonnell:
      return "Intel Bonnell";
    case Microarchitecture::kIntelSilvermont:
      return "Intel Silvermont";
    case Microarchitecture::kIntelAirmont:
      return "Intel Airmont";
    case Microarchitecture::kIntelGoldmont:
      return "Intel Goldmont";
    case Microarchitecture::kIntelGoldmontPlus:
      return "Intel Goldmont Plus";
    case Microarchitecture::kIntelTremont:
      return "Intel Tremont";
    case Microarchitecture::kAmdFamilyBulldozer:
      return "AMD Bulldozer";
    case Microarchitecture::kAmdFamilyJaguar:
      return "AMD Jaguar";
    case Microarchitecture::kAmdFamilyZen:
      return "AMD Zen";
    case Microarchitecture::kAmdFamilyZen3:
      return "AMD Zen 3";
  }
  __UNREACHABLE;
}

uint8_t CpuidVersionInfo::family() const {
  if (base_family() == 0xf) {
    return (static_cast<uint8_t>(base_family() + extended_family()));
  }
  return static_cast<uint8_t>(base_family());
}

uint8_t CpuidVersionInfo::model() const {
  if (base_family() == 0x6 || base_family() == 0xf) {
    return (static_cast<uint8_t>(extended_model() << 4)) | static_cast<uint8_t>(base_model());
  }
  return static_cast<uint8_t>(base_model());
}

// TODO(fxbug.dev/60649): check in a source of truth for this information and
// refer to that here.
Microarchitecture CpuidVersionInfo::microarchitecture(Vendor vendor) const {
  switch (vendor) {
    case Vendor::kIntel: {
      switch (family()) {
        case 0x6: {
          switch (model()) {
            case 0x0f:  // Merom.
            case 0x16:  // Merom L.
            case 0x17:  // Penryn, Wolfdale, Yorkfield, Harpertown, QC.
            case 0x1d:  // Dunnington.
              return Microarchitecture::kIntelCore2;
            case 0x1a:  // Bloomfield, EP, WS.
            case 0x1e:  // Lynnfield, Clarksfield.
            case 0x1f:  // Auburndale, Havendale.
            case 0x2e:  // EX.
              return Microarchitecture::kIntelNehalem;
            case 0x25:  // Arrandale, Clarkdale.
            case 0x2c:  // Gulftown, EP.
            case 0x2f:  // EX.
              return Microarchitecture::kIntelWestmere;
            case 0x2a:  // M, H.
            case 0x2d:  // E, EN, EP.
              return Microarchitecture::kIntelSandyBridge;
            case 0x3a:  // M, H, Gladden
            case 0x3e:  // E, EN, EP, EX.
              return Microarchitecture::kIntelIvyBridge;
            case 0x3c:  // S.
            case 0x3f:  // E, EP, EX.
            case 0x45:  // ULT.
            case 0x46:  // GT3E.
              return Microarchitecture::kIntelHaswell;
            case 0x3d:  // U, Y, S.
            case 0x47:  // H, C, W.
            case 0x56:  // DE, Hewitt Lake.
            case 0x4f:  // E, EP, EX.
              return Microarchitecture::kIntelBroadwell;
            case 0x4e:  // Skylake Y, U.
            case 0x5e:  // Skylake DT, H, S.
            // Kaby Lake Y, U; Coffee Lake U; Whiskey Lake U; Amber Lake Y;
            // Comet Lake U.
            case 0x8e:
            // Kaby Lake T, H, S, X; Coffee Lake S, H, E; Comet Lake S, H.
            case 0x9e:
              return Microarchitecture::kIntelSkylake;
            // Skylake SP, X, DE, W; Cascade Lake SP, X, W; Cooper Lake.
            case 0x55:
              return Microarchitecture::kIntelSkylakeServer;
            case 0x66:  // U.
              return Microarchitecture::kIntelCannonLake;
            case 0x1c:  // Silverthorne, Diamondville, Pineview.
            case 0x26:  // Lincroft.
            case 0x27:  // Penwell.
            case 0x35:  // Cloverview.
            case 0x36:  // Cedarview.
              return Microarchitecture::kIntelBonnell;
            case 0x37:  // Bay Trail.
            case 0x4a:  // Tangier.
            case 0x4d:  // Avoton, Rangeley.
            case 0x5a:  // Anniedale.
            case 0x5d:  // SoFIA.
              return Microarchitecture::kIntelSilvermont;
            case 0x4c:  // Cherry Trail, Braswell.
              return Microarchitecture::kIntelAirmont;
            case 0x5c:  // Apollo Lake, Broxton.
            case 0x5f:  // Denverton.
              return Microarchitecture::kIntelGoldmont;
            case 0x7a:  // Gemini Lake.
              return Microarchitecture::kIntelGoldmontPlus;
            case 0x86:  // Elkhart Lake.
              return Microarchitecture::kIntelTremont;
          }
          return Microarchitecture::kUnknown;
        }
      }
      return Microarchitecture::kUnknown;
    }
    case Vendor::kAmd: {
      switch (family()) {
        case 0x15: // Bulldozer/Piledriver/Steamroller/Excavator
          return Microarchitecture::kAmdFamilyBulldozer;
        case 0x16: // Jaguar
          return Microarchitecture::kAmdFamilyJaguar;
        case 0x17: // Zen 1 - 2
          return Microarchitecture::kAmdFamilyZen;
        case 0x19: // Zen 3
          return Microarchitecture::kAmdFamilyZen3;
      }
      return Microarchitecture::kUnknown;
    }
    case Vendor::kUnknown:
      return Microarchitecture::kUnknown;
  }
  __UNREACHABLE;
}

std::string_view ToString(X86CacheType type) {
  switch (type) {
    case X86CacheType::kNull:
      return "Null";
    case X86CacheType::kData:
      return "Data";
    case X86CacheType::kInstruction:
      return "Instruction";
    case X86CacheType::kUnified:
      return "Unified";
  }
  return "";
}

std::optional<bool> CpuidL1CacheInformation::fully_associative() const {
  switch (assoc()) {
    case 0:  // Disabled.
      return std::nullopt;
    case CpuidL1CacheInformation::kFullyAssociative:
      return true;
    default:
      return false;
  }
}

size_t CpuidL1CacheInformation::ways_of_associativity() const {
  if (assoc() == CpuidL1CacheInformation::kFullyAssociative) {
    return 0;
  }
  return assoc();
}

std::optional<bool> CpuidL2CacheInformation::fully_associative() const {
  return IsFullyAssociative(assoc());
}

size_t CpuidL2CacheInformation::ways_of_associativity() const { return ToWays(assoc()); }

std::optional<bool> CpuidL3CacheInformation::fully_associative() const {
  return IsFullyAssociative(assoc());
}

size_t CpuidL3CacheInformation::ways_of_associativity() const { return ToWays(assoc()); }

}  // namespace arch
