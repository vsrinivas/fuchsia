// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#include "arch/arm64/periphmap.h"

#include <lib/console.h>

#include <arch/arm64/mmu.h>
#include <ktl/optional.h>
#include <vm/vm.h>
#include <vm/vm_aspace.h>

#define PERIPH_RANGE_MAX 4

typedef struct {
  uint64_t base_phys;
  uint64_t base_virt;
  uint64_t length;
} periph_range_t;

static periph_range_t periph_ranges[PERIPH_RANGE_MAX] = {};

namespace {
struct Phys2VirtTrait {
  static uint64_t src(const periph_range_t& r) { return r.base_phys; }
  static uint64_t dst(const periph_range_t& r) { return r.base_virt; }
};

struct Virt2PhysTrait {
  static uint64_t src(const periph_range_t& r) { return r.base_virt; }
  static uint64_t dst(const periph_range_t& r) { return r.base_phys; }
};

template <typename Fetch>
struct PeriphUtil {
  // Translate (without range checking) the (virt|phys) peripheral provided to
  // its (phys|virt) counterpart using the provided range.
  static uint64_t Translate(const periph_range_t& range, uint64_t addr) {
    return addr - Fetch::src(range) + Fetch::dst(range);
  }

  // Find the index (if any) of the peripheral range which contains the
  // (virt|phys) address <addr>
  static ktl::optional<uint32_t> LookupNdx(uint64_t addr) {
    for (uint32_t i = 0; i < countof(periph_ranges); ++i) {
      const auto& range = periph_ranges[i];
      if (range.length == 0) {
        break;
      } else if (addr >= Fetch::src(range)) {
        uint64_t offset = addr - Fetch::src(range);
        if (offset < range.length) {
          return {i};
        }
      }
    }
    return {};
  }

  // Map the (virt|phys) peripheral provided to its (phys|virt) counterpart (if
  // any)
  static ktl::optional<uint64_t> Map(uint64_t addr) {
    auto ndx = LookupNdx(addr);
    if (ndx.has_value()) {
      return Translate(periph_ranges[ndx.value()], addr);
    }
    return {};
  }
};

using Phys2Virt = PeriphUtil<Phys2VirtTrait>;
using Virt2Phys = PeriphUtil<Virt2PhysTrait>;

template <typename T>
uint32_t rd_reg(vaddr_t addr) {
  return static_cast<uint32_t>(reinterpret_cast<volatile T*>(addr)[0]);
}

template <typename T>
void wr_reg(vaddr_t addr, uint32_t val) {
  reinterpret_cast<volatile T*>(addr)[0] = static_cast<T>(val);
}

// Note; the choice of these values must also align with the definitions in the
// options array below.
enum class AccessWidth {
  Byte = 0,
  Halfword = 1,
  Word = 2,
};
constexpr struct {
  const char* tag;
  void (*print)(uint32_t);
  uint32_t (*rd)(vaddr_t);
  void (*wr)(vaddr_t, uint32_t);
  uint32_t byte_width;
} kDumpModOptions[] = {
    {
        .tag = "byte",
        .print = [](uint32_t val) { printf(" %02x", val); },
        .rd = rd_reg<uint8_t>,
        .wr = wr_reg<uint8_t>,
        .byte_width = 1,
    },
    {
        .tag = "halfword",
        .print = [](uint32_t val) { printf(" %04x", val); },
        .rd = rd_reg<uint16_t>,
        .wr = wr_reg<uint16_t>,
        .byte_width = 2,
    },
    {
        .tag = "word",
        .print = [](uint32_t val) { printf(" %08x", val); },
        .rd = rd_reg<uint32_t>,
        .wr = wr_reg<uint32_t>,
        .byte_width = 4,
    },
};

zx_status_t dump_periph(paddr_t phys, uint64_t count, AccessWidth width) {
  const auto& opt = kDumpModOptions[static_cast<uint32_t>(width)];

  // Sanity check count
  if (!count) {
    printf("Illegal count %lu\n", count);
    return ZX_ERR_INVALID_ARGS;
  }

  uint64_t byte_amt = count * opt.byte_width;
  paddr_t phys_end_addr = phys + byte_amt - 1;

  // Sanity check alignment.
  if (phys & (opt.byte_width - 1)) {
    printf("%016lx is not aligned to a %u byte boundary!\n", phys, opt.byte_width);
    return ZX_ERR_INVALID_ARGS;
  }

  // Validate that the entire requested range fits within a single mapping.
  auto start_ndx = Phys2Virt::LookupNdx(phys);
  auto end_ndx = Phys2Virt::LookupNdx(phys_end_addr);
  if (!start_ndx.has_value() || !end_ndx.has_value() || (start_ndx.value() != end_ndx.value())) {
    printf("Physical range [%016lx, %016lx] is not contained in a single mapping!\n", phys,
           phys_end_addr);
    return ZX_ERR_INVALID_ARGS;
  }

  // OK, all of our sanity checks are complete.  Time to start dumping data.
  constexpr uint32_t bytes_per_line = 16;
  const uint64_t count_per_line = bytes_per_line / opt.byte_width;
  vaddr_t virt = Phys2Virt::Translate(periph_ranges[start_ndx.value()], phys);
  vaddr_t virt_end_addr = virt + byte_amt;

  printf("Dumping %lu %s%s starting at phys 0x%016lx\n", count, opt.tag, count == 1 ? "" : "s",
         phys);
  while (virt < virt_end_addr) {
    printf("%016lx :", phys);
    for (uint64_t i = 0; (i < count_per_line) && (virt < virt_end_addr);
         ++i, virt += opt.byte_width) {
      opt.print(opt.rd(virt));
    }
    phys += bytes_per_line;
    printf("\n");
  }

  return ZX_OK;
}

zx_status_t mod_periph(paddr_t phys, uint32_t val, AccessWidth width) {
  const auto& opt = kDumpModOptions[static_cast<uint32_t>(width)];

  // Sanity check alignment.
  if (phys & (opt.byte_width - 1)) {
    printf("%016lx is not aligned to a %u byte boundary!\n", phys, opt.byte_width);
    return ZX_ERR_INVALID_ARGS;
  }

  // Translate address
  auto vaddr = Phys2Virt::Map(phys);
  if (!vaddr.has_value()) {
    printf("Physical addr %016lx in not in the peripheral mappings!\n", phys);
  }

  // Perform the write, then report what we did.
  opt.wr(vaddr.value(), val);
  printf("Wrote");
  opt.print(val);
  printf(" to phys addr %016lx\n", phys);

  return ZX_OK;
}

int cmd_peripheral_map(int argc, const cmd_args* argv, uint32_t flags) {
  auto usage = [cmd = argv[0].str](bool not_enough_args = false) -> zx_status_t {
    if (not_enough_args) {
      printf("not enough arguments\n");
    }

    printf("usage:\n");
    printf("%s dump\n", cmd);
    printf("%s phys2virt <addr>\n", cmd);
    printf("%s virt2phys <addr>\n", cmd);
    printf(
        "%s dw|dh|db <phys_addr> [<count>] :: Dump <count> (word|half|byte) from <phys_addr> "
        "(count default = 1)\n",
        cmd);
    printf(
        "%s mw|mh|mb <phys_addr> <value> :: Write the contents of <value> to the (word|half|byte) "
        "at <phys_addr>\n",
        cmd);

    return ZX_ERR_INTERNAL;
  };

  if (argc < 2) {
    return usage(true);
  }

  if (!strcmp(argv[1].str, "dump")) {
    uint32_t i = 0;
    for (const auto& range : periph_ranges) {
      if (range.length) {
        printf("Phys [%016lx, %016lx] ==> Virt [%016lx, %016lx] (len 0x%08lx)\n", range.base_phys,
               range.base_phys + range.length - 1, range.base_virt,
               range.base_virt + range.length - 1, range.length);
        ++i;
      }
    }
    printf("Dumped %u defined peripheral map ranges\n", i);
  } else if (!strcmp(argv[1].str, "phys2virt") || !strcmp(argv[1].str, "virt2phys")) {
    if (argc < 3) {
      return usage(true);
    }

    bool phys_src = !strcmp(argv[1].str, "phys2virt");
    auto map_fn = phys_src ? Phys2Virt::Map : Virt2Phys::Map;
    auto res = map_fn(argv[2].u);
    if (res.has_value()) {
      printf("%016lx ==> %016lx\n", argv[2].u, res.value());
    } else {
      printf("Failed to find the %s address 0x%016lx in the peripheral mappings.\n",
             phys_src ? "physical" : "virtual", argv[2].u);
    }
  } else if ((argv[1].str[0] == 'd') || (argv[1].str[0] == 'm')) {
    // If this is a valid display or modify command, its length will be exactly 2.
    if (strlen(argv[1].str) != 2) {
      return usage();
    }

    // Parse the next letter to figure out the width of the operation.
    AccessWidth width;
    switch (argv[1].str[1]) {
      case 'w':
        width = AccessWidth::Word;
        break;
      case 'h':
        width = AccessWidth::Halfword;
        break;
      case 'b':
        width = AccessWidth::Byte;
        break;
      default:
        return usage();
    }

    paddr_t phys_addr = argv[2].u;
    if (argv[1].str[0] == 'd') {
      // Dump commands have a default count of 1
      return dump_periph(phys_addr, (argc < 4) ? 1 : argv[3].u, width);
    } else {
      // Modify commands are required to have a value.
      return (argc < 4) ? usage(true)
                        : mod_periph(phys_addr, static_cast<uint32_t>(argv[3].u), width);
    }

  } else {
    return usage();
  }

  return ZX_OK;
}

}  // namespace

STATIC_COMMAND_START
STATIC_COMMAND("pm", "peripheral mapping commands", &cmd_peripheral_map)
STATIC_COMMAND_END(pm)

zx_status_t add_periph_range(paddr_t base_phys, size_t length) {
  // peripheral ranges are allocated below the kernel image.
  uint64_t base_virt = (uint64_t)__code_start;

  DEBUG_ASSERT(IS_PAGE_ALIGNED(base_phys));
  DEBUG_ASSERT(IS_PAGE_ALIGNED(length));

  for (auto& range : periph_ranges) {
    if (range.length == 0) {
      base_virt -= length;
      auto status = arm64_boot_map_v(base_virt, base_phys, length, MMU_INITIAL_MAP_DEVICE);
      if (status == ZX_OK) {
        range.base_phys = base_phys;
        range.base_virt = base_virt;
        range.length = length;
      }
      return status;
    } else {
      base_virt -= range.length;
    }
  }
  return ZX_ERR_OUT_OF_RANGE;
}

void reserve_periph_ranges() {
  for (auto& range : periph_ranges) {
    if (range.length == 0) {
      break;
    }
    VmAspace::kernel_aspace()->ReserveSpace("periph", range.length, range.base_virt);
  }
}

vaddr_t periph_paddr_to_vaddr(paddr_t paddr) {
  auto ret = Phys2Virt::Map(paddr);
  return ret.has_value() ? ret.value() : 0;
}
