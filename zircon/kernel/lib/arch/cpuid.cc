// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/x86/cpuid.h>

namespace arch {

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
    case Microarchitecture::kAmdFamily0x15:
      return "AMD Family 0x15";
    case Microarchitecture::kAmdFamily0x16:
      return "AMD Family 0x16";
    case Microarchitecture::kAmdFamily0x17:
      return "AMD Family 0x17";
    case Microarchitecture::kAmdFamily0x19:
      return "AMD Family 0x19";
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
        case 0x15:
          return Microarchitecture::kAmdFamily0x15;
        case 0x16:
          return Microarchitecture::kAmdFamily0x16;
        case 0x17:
          return Microarchitecture::kAmdFamily0x17;
        case 0x19:
          return Microarchitecture::kAmdFamily0x19;
      }
      return Microarchitecture::kUnknown;
    }
    case Vendor::kUnknown:
      return Microarchitecture::kUnknown;
  }
  __UNREACHABLE;  
}

}  // namespace arch
