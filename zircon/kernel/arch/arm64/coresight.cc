// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/console.h>

#include <arch/arm64/mp.h>
#include <arch/arm64/periphmap.h>
#include <dev/coresight/rom_table.h>
#include <hwreg/mmio.h>
#include <vm/pmm.h>
#include <vm/vm_aspace.h>

namespace {

using coresight::ComponentIDRegister;
using coresight::DeviceAffinityRegister;
using coresight::DeviceArchRegister;
using coresight::DeviceTypeRegister;

// At the time of writing this, all observed ROM tables on the supported ARM
// boards fit within an 8MiB address range. Increase as needed.
constexpr uint32_t kViewSize = 0x80'0000;

// Prints information about a generic CoreSight component.
void DumpComponentInfo(uintptr_t component) {
  hwreg::RegisterMmio mmio(reinterpret_cast<void*>(component));
  paddr_t paddr = vaddr_to_paddr(reinterpret_cast<void*>(component));
  printf("address: %#" PRIxPTR "\n", paddr);

  const ComponentIDRegister::Class classid = ComponentIDRegister::Get().ReadFrom(&mmio).classid();
  const uint16_t partid = coresight::GetPartID(mmio);

  // Morally a CoreSight component, if not one technically, ARM puts them in ROM tables.
  if (classid == ComponentIDRegister::Class::kNonStandard &&
      partid == coresight::arm::partid::kTimestampGenerator) {
    printf("type: N/A\n");
    printf("affinity: cluster\n");
    printf("architect: ARM\n");
    printf("architecture: Timestamp Generator\n");
    return;
  } else if (classid != ComponentIDRegister::Class::kCoreSight) {
    std::string_view classid_str = coresight::ToString(classid);
    printf("unexpected component found; (class, part number) = (%#x (%.*s), %#x)\n",
           static_cast<uint8_t>(classid), static_cast<int>(classid_str.size()), classid_str.data(),
           partid);
    return;
  }

  const DeviceTypeRegister::Type type = DeviceTypeRegister::Get().ReadFrom(&mmio).type();
  std::string_view type_str = coresight::ToString(type);
  printf("type: %.*s\n", static_cast<int>(type_str.size()), type_str.data());

  const uint64_t affinity = DeviceAffinityRegister::Get().ReadFrom(&mmio).reg_value();
  printf("affinity: ");
  if (affinity == 0) {
    printf("cluster\n");
  } else if (cpu_num_t cpu_num = arm64_mpidr_to_cpu_num(affinity); cpu_num != INVALID_CPU) {
    printf("CPU #%u (%#lx)\n", cpu_num, affinity);
  } else {
    printf("%#lx\n", affinity);
  }

  const DeviceArchRegister arch_reg = DeviceArchRegister::Get().ReadFrom(&mmio);
  const auto archid = static_cast<uint16_t>(arch_reg.archid());
  const auto revision = static_cast<uint16_t>(arch_reg.revision());

  // The device architecture register might not be populated; in this case, we
  // consult the designer designation.
  const auto architect = arch_reg.architect() ? static_cast<uint16_t>(arch_reg.architect())
                                              : coresight::GetDesigner(mmio);

  if (architect == coresight::arm::kArchitect) {
    printf("architect: ARM\n");
  } else {
    printf("architect: unknown (%#x)\n", architect);
    printf("archid: %#x\n", archid);
    printf("part number: %#x\n", partid);
    return;  // Not much more we can say.
  }

  printf("architecture: ");
  switch (archid) {
    case coresight::arm::archid::kCTI:
      printf("Cross-Trigger Matrix (CTI)\n");
      return;
    case coresight::arm::archid::kETMv3:
      printf("Embedded Trace Monitor (ETM) v3.%u\n", revision);
      return;
    case coresight::arm::archid::kETMv4:
      printf("Embedded Trace Monitor (ETM) v4.%u\n", revision);
      return;
    case coresight::arm::archid::kPMUv2:
      printf("Performance Monitor Unit (PMU) v2.%u\n", revision);
      return;
    case coresight::arm::archid::kPMUv3:
      printf("Performance Monitor Unit (PMU) v3.%u\n", revision);
      return;
    case coresight::arm::archid::kROMTable:
      printf("0x9 ROM Table\n");
      return;
    case coresight::arm::archid::kV8Dot0A:
      printf("ARM v8.0-A Core Debug Interface\n");
      return;
    case coresight::arm::archid::kV8Dot1A:
      printf("ARM v8.1-A Core Debug Interface\n");
      return;
    case coresight::arm::archid::kV8Dot2A:
      printf("ARM v8.2-A Core Debug Interface\n");
      return;
  };

  // Sometimes no architecture ID is populated; fall back to part ID.
  switch (partid) {
    case coresight::arm::partid::kETB:
      printf("Embedded Trace Buffer (ETB)\n");
      return;
    case coresight::arm::partid::kCTI400:
      printf("Cross-Trigger Matrix (CTI) (SoC400 generation)\n");
      return;
    case coresight::arm::partid::kCTI600:
      printf("Cross-Trigger Matrix (CTI) (SoC600 generation)\n");
      return;
    case coresight::arm::partid::kTMC:
      printf("Trace Memory Controller (TMC) (SoC400 generation)\n");
      return;
    case coresight::arm::partid::kTPIU:
      printf("Trace Port Interface Unit (TPIU)\n");
      return;
    case coresight::arm::partid::kTraceFunnel:
      printf("Trace Funnel (SoC400 generation)\n");
      return;
    case coresight::arm::partid::kTraceReplicator:
      printf("Trace Replicator (SoC400 generation)\n");
      return;
  };
  printf("unknown: (archid, part number) = (%#x, %#x)\n", archid, partid);
}

int WalkROMTable(uintptr_t addr, uint32_t view_size) {
  hwreg::RegisterMmio mmio(reinterpret_cast<void*>(addr));
  coresight::ROMTable table(addr, view_size);
  auto result = table.Walk(mmio, [](uintptr_t component) {
    printf("\n----------------------------------------\n");
    DumpComponentInfo(component);
  });
  if (result.is_error()) {
    printf("error: %s\n", result.error_value().data());
  }
  return 0;
}

int cmd_coresight(int argc, const cmd_args* argv, uint32_t flags) {
  auto usage = [&argv]() {
    printf("usage:\n");
    printf("k %s help\n", argv[0].str);
    printf("k %s walk <ROM table physical address>\n", argv[0].str);
  };

  if (argc < 2) {
    usage();
    return 1;
  } else if (!strcmp(argv[1].str, "help")) {
    usage();
  } else if (!strcmp(argv[1].str, "walk")) {
    if (argc < 3) {
      printf("too few arguments\n");
      usage();
      return 1;
    }

    paddr_t paddr = argv[2].u;
    printf("attempting to walk a ROM table at %#" PRIxPTR "...\n", paddr);
    void* virt = nullptr;
    zx_status_t status = VmAspace::kernel_aspace()->AllocPhysical(
        "k coresight walk",
        kViewSize,                                                 // Range size
        &virt,                                                     // Requested virtual address
        PAGE_SIZE_SHIFT,                                           // Alignment log2
        paddr,                                                     // Physical address
        0,                                                         // VMM flags
        ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_UNCACHED_DEVICE);  // MMU flags
    if (status != ZX_OK || !virt) {
      printf("failed to map address range starting at %#" PRIxPTR ": %d\n", paddr, status);
      return 1;
    }
    printf("virtual address: %p\n", virt);
    return WalkROMTable(reinterpret_cast<uintptr_t>(virt), kViewSize);
  } else {
    printf("unrecognized command: %s\n", argv[1].str);
    usage();
    return 1;
  }
  return 0;
}

STATIC_COMMAND_START
STATIC_COMMAND("coresight", "access information within a CoreSight system", &cmd_coresight)
STATIC_COMMAND_END(coresight)

}  // namespace
