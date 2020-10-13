// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chipset/axi_backplane.h"

#include <lib/zx/time.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <algorithm>
#include <utility>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chipset/chipset_regs.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chipset/spinwait.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/debug.h"
#include "src/connectivity/wlan/lib/common/cpp/include/wlan/common/bitfield.h"

namespace wlan {
namespace brcmfmac {
namespace {

// Size of the register window we use to access the EROM.
constexpr size_t kEromWindowSize = 0x1000;

// Size of the register window we use to access core registers.
constexpr size_t kRegWindowSize = 0x1000;

// Size of the register window we use to access wrapbase registers.
constexpr size_t kWrapWindowSize = 0x1000;

// Interval and timeout for internal spinwaits.
constexpr zx::duration kSpinwaitInterval = zx::usec(20);
constexpr zx::duration kSpinwaitTimeout = zx::usec(2000);

class EromDescriptor : public wlan::common::BitField<uint32_t> {
 public:
  enum class Type : uint32_t {
    kComponent = 0,
    kPrimaryPort = 1,
    kAddress = 2,
    kEndOfTable = 3,
  };

  WLAN_BIT_FIELD(valid, 0, 1);
  WLAN_BIT_FIELD(type, 1, 2);
  WLAN_BIT_FIELD(addr_64bit, 3, 1);
};

class EromComponentDescriptorLow : public EromDescriptor {
 public:
  WLAN_BIT_FIELD(part_class, 4, 4);
  WLAN_BIT_FIELD(part_num, 8, 12);
  WLAN_BIT_FIELD(designer, 20, 12);
};

class EromComponentDescriptorHigh : public EromDescriptor {
 public:
  WLAN_BIT_FIELD(num_mport, 4, 5);
  WLAN_BIT_FIELD(num_sport, 9, 5);
  WLAN_BIT_FIELD(num_mwrap, 14, 5);
  WLAN_BIT_FIELD(num_swrap, 19, 5);
  WLAN_BIT_FIELD(revision, 24, 8);
};

constexpr size_t kEromSecondaryDescriptorAddrBaseMultiplier = 0x1000;

class EromSecondaryDescriptor : public EromDescriptor {
 public:
  enum class SizeType : uint32_t {
    k4k = 0,
    k8k = 1,
    k16k = 2,
    kDesc = 3,
  };

  enum class SecondaryType : uint32_t {
    kSecondary = 0,
    kBridge = 1,
    kSwrap = 2,
    kMwrap = 3,
  };

  WLAN_BIT_FIELD(size_type, 4, 2);
  WLAN_BIT_FIELD(secondary_type, 6, 2);
  WLAN_BIT_FIELD(port_num, 8, 4);
  WLAN_BIT_FIELD(addr_base, 12, 20);
};

class BuscoreCoreControl : public wlan::common::AddressableBitField<uint16_t, uint32_t, 0x0408> {
 public:
  WLAN_BIT_FIELD(clock, 0, 1);
  WLAN_BIT_FIELD(fgc, 1, 1);
};

class BuscoreCoreResetControl
    : public wlan::common::AddressableBitField<uint16_t, uint32_t, 0x0800> {
 public:
  WLAN_BIT_FIELD(reset, 0, 1);
};

}  // namespace

AxiBackplane::AxiBackplane(CommonCoreId chip_id, uint16_t chip_rev)
    : Backplane(chip_id, chip_rev) {}

AxiBackplane::AxiBackplane(AxiBackplane&& other) { swap(*this, other); }

AxiBackplane& AxiBackplane::operator=(AxiBackplane other) {
  swap(*this, other);
  return *this;
}

void swap(AxiBackplane& lhs, AxiBackplane& rhs) {
  using std::swap;
  swap(static_cast<Backplane&>(lhs), static_cast<Backplane&>(rhs));
  swap(lhs.register_window_provider_, rhs.register_window_provider_);
  swap(lhs.cores_, rhs.cores_);
}

AxiBackplane::~AxiBackplane() = default;

// static
zx_status_t AxiBackplane::Create(RegisterWindowProviderInterface* register_window_provider,
                                 CommonCoreId chip_id, uint16_t chip_rev,
                                 std::optional<AxiBackplane>* out_backplane) {
  zx_status_t status = ZX_OK;

  std::vector<AxiBackplane::Core> cores;
  if ((status = EnumerateCores(register_window_provider, &cores)) != ZX_OK) {
    BRCMF_ERR("Failed to enumerate cores: %s", zx_status_get_string(status));
    return status;
  }

  auto& backplane = out_backplane->emplace(chip_id, chip_rev);
  backplane.register_window_provider_ = register_window_provider;
  backplane.cores_ = std::move(cores);
  return ZX_OK;
}

const Backplane::Core* AxiBackplane::GetCore(Backplane::CoreId core_id) const {
  auto core = GetAxiCore(core_id);
  if (core == nullptr) {
    return nullptr;
  }
  return &core->core;
}

zx_status_t AxiBackplane::IsCoreUp(Backplane::CoreId core_id, bool* out_is_up) {
  zx_status_t status = ZX_OK;

  std::unique_ptr<RegisterWindowProviderInterface::RegisterWindow> wrap_window;
  if ((status = GetWrapWindow(core_id, &wrap_window)) != ZX_OK) {
    BRCMF_ERR("Failed to get wrap window: %s", zx_status_get_string(status));
    return status;
  }

  BuscoreCoreControl core_control;
  if ((status = wrap_window->Read(core_control.addr(), core_control.mut_val())) != ZX_OK) {
    BRCMF_ERR("Failed to read core control: %s", zx_status_get_string(status));
    return status;
  }

  BuscoreCoreResetControl core_reset_control;
  if ((status = wrap_window->Read(core_reset_control.addr(), core_reset_control.mut_val())) !=
      ZX_OK) {
    BRCMF_ERR("Failed to read core reset control: %s", zx_status_get_string(status));
    return status;
  }

  *out_is_up = (core_control.clock() && !core_control.fgc() && !core_reset_control.reset());
  return ZX_OK;
}

zx_status_t AxiBackplane::DisableCore(Backplane::CoreId core_id, uint32_t prereset,
                                      uint32_t postreset) {
  zx_status_t status = ZX_OK;

  std::unique_ptr<RegisterWindowProviderInterface::RegisterWindow> wrap_window;
  if ((status = GetWrapWindow(core_id, &wrap_window)) != ZX_OK) {
    BRCMF_ERR("Failed to get wrap window: %s", zx_status_get_string(status));
    return status;
  }

  // Configure the disable.
  BuscoreCoreControl core_control;
  core_control.set_val(prereset);
  core_control.set_clock(1);
  core_control.set_fgc(1);
  if ((status = wrap_window->Write(core_control.addr(), core_control.val())) != ZX_OK) {
    BRCMF_ERR("Failed to write core control: %s", zx_status_get_string(status));
    return status;
  }
  if ((status = wrap_window->Read(core_control.addr(), core_control.mut_val())) != ZX_OK) {
    BRCMF_ERR("Failed to read core control: %s", zx_status_get_string(status));
    return status;
  }

  // Perform the disable.
  BuscoreCoreResetControl core_reset_control;
  core_reset_control.set_reset(1);
  if ((status = wrap_window->Write(core_reset_control.addr(), core_reset_control.val())) != ZX_OK) {
    BRCMF_ERR("Failed to write core reset control: %s", zx_status_get_string(status));
    return status;
  }

  // Spinwait for the disable to commence.
  if ((status = Spinwait(kSpinwaitInterval, kSpinwaitTimeout, [&]() {
         zx_status_t status = ZX_OK;
         if ((status = wrap_window->Read(core_reset_control.addr(),
                                         core_reset_control.mut_val())) != ZX_OK) {
           BRCMF_ERR("Failed to read core reset control: %s", zx_status_get_string(status));
           return status;
         }
         if (core_reset_control.reset()) {
           return ZX_OK;
         }
         return ZX_ERR_NEXT;
       })) != ZX_OK) {
    BRCMF_ERR("Failed to wait for core reset control: %s", zx_status_get_string(status));
    return status;
  }

  // Post-configure the disable.
  core_control.set_val(postreset);
  core_control.set_clock(1);
  core_control.set_fgc(1);
  if ((status = wrap_window->Write(core_control.addr(), core_control.val())) != ZX_OK) {
    BRCMF_ERR("Failed to write core control: %s", zx_status_get_string(status));
    return status;
  }
  if ((status = wrap_window->Read(core_control.addr(), core_control.mut_val())) != ZX_OK) {
    BRCMF_ERR("Failed to read core control: %s", zx_status_get_string(status));
    return status;
  }

  return ZX_OK;
}

zx_status_t AxiBackplane::ResetCore(CoreId core_id, uint32_t prereset, uint32_t postreset) {
  zx_status_t status = ZX_OK;

  if ((status = DisableCore(core_id, prereset, postreset)) != ZX_OK) {
    BRCMF_ERR("Failed to disable core %d: %s", static_cast<int>(core_id),
              zx_status_get_string(status));
    return status;
  }

  std::unique_ptr<RegisterWindowProviderInterface::RegisterWindow> wrap_window;
  if ((status = GetWrapWindow(core_id, &wrap_window)) != ZX_OK) {
    BRCMF_ERR("Failed to get wrap window: %s", zx_status_get_string(status));
    return status;
  }

  // Spinwait for the disable to complete.
  BuscoreCoreResetControl core_reset_control;
  if ((status = Spinwait(kSpinwaitInterval, kSpinwaitTimeout, [&]() {
         zx_status_t status = ZX_OK;
         if ((status = wrap_window->Read(core_reset_control.addr(),
                                         core_reset_control.mut_val())) != ZX_OK) {
           BRCMF_ERR("Failed to read core reset control: %s", zx_status_get_string(status));
           return status;
         }
         if (!core_reset_control.reset()) {
           return ZX_OK;
         }
         core_reset_control.clear();
         if ((status = wrap_window->Write(core_reset_control.addr(), core_reset_control.val())) !=
             ZX_OK) {
           BRCMF_ERR("Failed to write core reset control: %s", zx_status_get_string(status));
           return status;
         }
         return ZX_ERR_NEXT;
       })) != ZX_OK) {
    BRCMF_ERR("Failed to wait for core reset complete: %s", zx_status_get_string(status));
    return status;
  }

  // Post-configure the reset.
  BuscoreCoreControl core_control;
  core_control.set_val(postreset);
  core_control.set_clock(1);
  if ((status = wrap_window->Write(core_control.addr(), core_control.val())) != ZX_OK) {
    BRCMF_ERR("Failed to write core control: %s", zx_status_get_string(status));
    return status;
  }
  if ((status = wrap_window->Read(core_control.addr(), core_control.mut_val())) != ZX_OK) {
    BRCMF_ERR("Failed to read core control: %s", zx_status_get_string(status));
    return status;
  }

  return ZX_OK;
}

// static
zx_status_t AxiBackplane::EnumerateCores(RegisterWindowProviderInterface* register_window_provider,
                                         std::vector<AxiBackplane::Core>* out_cores) {
  zx_status_t status = ZX_OK;
  std::vector<AxiBackplane::Core> cores;

  uint32_t erom_addr = 0;
  {
    std::unique_ptr<RegisterWindowProviderInterface::RegisterWindow> register_window;
    if ((status = register_window_provider->GetRegisterWindow(SI_ENUM_BASE, sizeof(ChipsetCoreRegs),
                                                              &register_window)) != ZX_OK) {
      BRCMF_ERR("Failed to open enum space window: %s", zx_status_get_string(status));
      return status;
    }

    if ((status = register_window->Read(offsetof(ChipsetCoreRegs, eromptr), &erom_addr)) != ZX_OK) {
      BRCMF_ERR("Failed to get EROM address: %s", zx_status_get_string(status));
      return status;
    }
  }

  // This lambda encapsulates the state required to incrementally read from EROM.
  std::unique_ptr<RegisterWindowProviderInterface::RegisterWindow> register_window;
  if ((status = register_window_provider->GetRegisterWindow(erom_addr, kEromWindowSize,
                                                            &register_window)) != ZX_OK) {
    BRCMF_ERR("Failed to get EROM window: %s", zx_status_get_string(status));
    return status;
  }
  uint32_t erom_offset = 0;
  const auto read_from_erom = [window = std::move(register_window), &erom_offset](auto* desc) {
    zx_status_t status = ZX_OK;
    if ((status = window->Read(erom_offset, desc->mut_val())) != ZX_OK) {
      BRCMF_ERR("Failed to read EROM descriptor at offset 0x%08x: %s", erom_offset,
                zx_status_get_string(status));
      return status;
    }
    erom_offset += 4;
    return ZX_OK;
  };

  // Iterate over all the component descriptors in the EROM table.
  while (true) {
    // Read the component descriptor low/high pair.
    EromComponentDescriptorLow component_low;
    if ((status = read_from_erom(&component_low)) != ZX_OK) {
      return status;
    }
    const EromDescriptor::Type component_low_type =
        static_cast<EromDescriptor::Type>(component_low.type());
    if (component_low_type == EromDescriptor::Type::kEndOfTable) {
      break;
    } else if (component_low_type != EromDescriptor::Type::kComponent) {
      continue;
    }
    EromComponentDescriptorHigh component_high;
    if ((status = read_from_erom(&component_high)) != ZX_OK) {
      return status;
    }
    const EromDescriptor::Type component_high_type =
        static_cast<EromDescriptor::Type>(component_high.type());
    if (component_high_type != EromDescriptor::Type::kComponent) {
      BRCMF_ERR("Invalid descriptor for component_high: %08x", component_high.val());
      return ZX_ERR_INVALID_ARGS;
    }

    // Get the `regbase` and `wrapbase` associated with this component.
    const Backplane::CoreId core_id = static_cast<Backplane::CoreId>(component_low.part_num());
    uint32_t regbase = 0;
    uint32_t wrapbase = 0;
    status = [&]() {
      EromSecondaryDescriptor::SecondaryType wraptype = {};
      EromSecondaryDescriptor descriptor;
      if ((status = read_from_erom(&descriptor)) != ZX_OK) {
        return status;
      }
      if (static_cast<EromDescriptor::Type>(descriptor.type()) ==
          EromDescriptor::Type::kPrimaryPort) {
        wraptype = EromSecondaryDescriptor::SecondaryType::kMwrap;
      } else if (static_cast<EromDescriptor::Type>(descriptor.type()) ==
                 EromDescriptor::Type::kAddress) {
        wraptype = EromSecondaryDescriptor::SecondaryType::kSwrap;
      } else {
        // Wrong type.
        return ZX_ERR_INVALID_ARGS;
      }

      while (true) {
        while (true) {
          if (static_cast<EromDescriptor::Type>(descriptor.type()) ==
              EromDescriptor::Type::kAddress) {
            if (descriptor.addr_64bit()) {
              // The next descriptor has the upper 32 bits.  Skip it.
              EromDescriptor d;
              if ((status = read_from_erom(&d)) != ZX_OK) {
                return status;
              }
            }

            if (static_cast<EromSecondaryDescriptor::SizeType>(descriptor.size_type()) ==
                EromSecondaryDescriptor::SizeType::kDesc) {
              // This is a secondary descriptor, so we skip that too.
              EromDescriptor d;
              if ((status = read_from_erom(&d)) != ZX_OK) {
                return status;
              }
              if (d.addr_64bit()) {
                if ((status = read_from_erom(&d)) != ZX_OK) {
                  return status;
                }
              }
            }

            // We are only interested in 4K register regions.
            if (static_cast<EromSecondaryDescriptor::SizeType>(descriptor.size_type()) ==
                EromSecondaryDescriptor::SizeType::k4k) {
              break;
            }
          } else if (static_cast<EromDescriptor::Type>(descriptor.type()) ==
                         EromDescriptor::Type::kComponent ||
                     static_cast<EromDescriptor::Type>(descriptor.type()) ==
                         EromDescriptor::Type::kEndOfTable) {
            // This component's descriptor entries has ended before both `regbase` and `wrapbase`
            // have been found.
            return ZX_ERR_NEXT;
          }

          // This was not the descriptor we're looking for, so keep going.
          if ((status = read_from_erom(&descriptor)) != ZX_OK) {
            return status;
          }
        }

        // Read the register and wrap base from the EROM entry.
        const EromSecondaryDescriptor::SecondaryType stype =
            static_cast<EromSecondaryDescriptor::SecondaryType>(descriptor.secondary_type());
        if (regbase == 0 && stype == EromSecondaryDescriptor::SecondaryType::kSecondary) {
          regbase = descriptor.addr_base() * kEromSecondaryDescriptorAddrBaseMultiplier;
        }
        if (wrapbase == 0 && stype == wraptype) {
          wrapbase = descriptor.addr_base() * kEromSecondaryDescriptorAddrBaseMultiplier;
        }
        if (regbase != 0 && wrapbase != 0) {
          return ZX_OK;
        }

        if ((status = read_from_erom(&descriptor)) != ZX_OK) {
          return status;
        }
      }
    }();
    if (status != ZX_OK) {
      if (status != ZX_ERR_NEXT) {
        BRCMF_ERR("Failed to get core addresses for core %d: %s", static_cast<int>(core_id),
                  zx_status_get_string(status));
        return status;
      }

      // Revert `erom_offset` to the previous EROM entry, and continue trying to parse.
      erom_offset -= 4;
      continue;
    }

    const uint16_t rev = component_high.revision();
    cores.push_back({{core_id, rev, regbase, kRegWindowSize}, wrapbase, kWrapWindowSize});
  }

  if (cores.empty()) {
    BRCMF_ERR("Failed to find any cores");
    return ZX_ERR_NOT_FOUND;
  }

  // Sort the list of cores, and make sure it is unique.
  std::sort(cores.begin(), cores.end(),
            [](const AxiBackplane::Core& lhs, const AxiBackplane::Core& rhs) {
              return static_cast<uint16_t>(lhs.core.id) < static_cast<uint16_t>(rhs.core.id);
            });
  const auto duplicate = std::adjacent_find(
      cores.begin(), cores.end(), [](const AxiBackplane::Core& lhs, const AxiBackplane::Core& rhs) {
        return lhs.core.id == rhs.core.id;
      });
  if (duplicate != cores.end()) {
    BRCMF_ERR("Found duplicate cores for core_id %d", static_cast<int>(duplicate->core.id));
    return ZX_ERR_INVALID_ARGS;
  }
  cores.shrink_to_fit();

  *out_cores = std::move(cores);
  return ZX_OK;
}

const AxiBackplane::Core* AxiBackplane::GetAxiCore(Backplane::CoreId core_id) const {
  auto found =
      std::lower_bound(cores_.begin(), cores_.end(), core_id,
                       [](const AxiBackplane::Core& lhs, Backplane::CoreId core_id) {
                         return static_cast<uint16_t>(lhs.core.id) < static_cast<uint16_t>(core_id);
                       });
  if (found == cores_.end()) {
    return nullptr;
  }
  if (found->core.id != core_id) {
    return nullptr;
  }
  return &(*found);
}

zx_status_t AxiBackplane::GetWrapWindow(
    Backplane::CoreId core_id,
    std::unique_ptr<RegisterWindowProviderInterface::RegisterWindow>* out_wrap_window) {
  zx_status_t status = ZX_OK;

  const auto core = GetAxiCore(core_id);
  if (core == nullptr) {
    BRCMF_ERR("Failed to find core %d", static_cast<int>(core_id));
    return ZX_ERR_NOT_FOUND;
  }

  std::unique_ptr<RegisterWindowProviderInterface::RegisterWindow> register_window;
  if ((status = register_window_provider_->GetRegisterWindow(core->wrapbase, core->wrapsize,
                                                             &register_window)) != ZX_OK) {
    BRCMF_ERR("Failed to open wrap window: %s", zx_status_get_string(status));
    return status;
  }

  *out_wrap_window = std::move(register_window);
  return ZX_OK;
}

}  // namespace brcmfmac
}  // namespace wlan
