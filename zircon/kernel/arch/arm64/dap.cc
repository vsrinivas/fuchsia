// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "arch/arm64/dap.h"

#include <bits.h>
#include <inttypes.h>
#include <lib/arch/intrin.h>
#include <lib/boot-options/boot-options.h>
#include <lib/console.h>
#include <platform.h>
#include <stdio.h>
#include <trace.h>
#include <zircon/time.h>

#include <arch/arm64/mp.h>
#include <dev/coresight/rom_table.h>
#include <fbl/alloc_checker.h>
#include <fbl/array.h>
#include <hwreg/mmio.h>
#include <kernel/auto_preempt_disabler.h>
#include <kernel/cpu.h>
#include <ktl/iterator.h>
#include <lk/init.h>
#include <vm/pmm.h>
#include <vm/vm_aspace.h>

#include <ktl/enforce.h>

#define LOCAL_TRACE 0

namespace {

using coresight::ComponentIdRegister;
using coresight::DeviceAffinityRegister;
using coresight::DeviceArchRegister;

// This struct describes the aperture in which a ROM table resides.
//
// An aperture is logically part of a CPU cluster. Prior to booting the cluster the table may appear
// invalid (reads return zero) so it's important to not read an aperture until its cluster has
// booted. The aperture's |mask| field is the set of CPUs that belong to the cluster of the given
// aperture. When walking the ROM tables in an aperture be sure to only do so on CPUs in the |mask|.
struct dap_aperture {
  paddr_t base;
  size_t size;
  cpu_mask_t mask;
  void *virt;
};

struct debug_port {
  bool initialized;
  cpu_num_t cpu_num;
  volatile uint32_t *dap;  // pointer to the DAP register window
  volatile uint32_t *cti;  // pointer to the CTI register window
};

fbl::Array<dap_aperture> dap_apertures;
fbl::Array<debug_port> daps;

// Called on the boot CPU.
void arm_dap_init(uint level) {
  LTRACE_ENTRY;

  enum {
    None,
    t931g,
    s905d2,
    s905d3g,
  } soc = None;

  // Parse the valid options out of the kernel command line
  // kernel.arm64.debug.dap-rom-soc
  if (strcmp(gBootOptions->arm64_debug_dap_rom_soc.data(), "amlogic-t931g") == 0) {
    soc = t931g;
  } else if (strcmp(gBootOptions->arm64_debug_dap_rom_soc.data(), "amlogic-s905d2") == 0) {
    soc = s905d2;
  } else if (strcmp(gBootOptions->arm64_debug_dap_rom_soc.data(), "amlogic-s905d3g") == 0) {
    soc = s905d3g;
  } else if (strcmp(gBootOptions->arm64_debug_dap_rom_soc.data(), "") != 0) {
    dprintf(INFO, "ARM DAP: unrecognized non-empty option passed '%s'\n",
            gBootOptions->arm64_debug_dap_rom_soc.data());
  }
  if (soc == None) {
    return;
  }

  // Set the ROM table locations to search in for DAP apertures for each cpu
  {
    DEBUG_ASSERT(soc != None);

    // Pre-canned values for the debug rom table base, taken from
    // the manuals for these particular SOCs.
    // TODO: read this from ZBI as well
    // clang-format off
    static dap_aperture t931g_aperture[] = {
      { .base = 0xf580'0000,    // A53_BASE
        .size = 0x80'0000,
        .mask = cpu_num_to_mask(0) | cpu_num_to_mask(1),
      },
      { .base = 0xf500'0000,    // A73_BASE
        .size = 0x80'0000,
        .mask = cpu_num_to_mask(2) | cpu_num_to_mask(3) | cpu_num_to_mask(4) | cpu_num_to_mask(5),
      }
    };
    static dap_aperture s905_aperture[] = {
      { .base = 0xf580'0000,    // A53_BASE
        .size = 0x80'0000,
        .mask = CPU_MASK_ALL,
      },
    };
    // clang-format on

    switch (soc) {
      case t931g:
        // two dap register windows for T931G
        dap_apertures = {t931g_aperture, ktl::size(t931g_aperture)};
        break;
      case s905d2:
      case s905d3g:
        // s905d2 and s905d3g have the same aperture
        dap_apertures = {s905_aperture, ktl::size(s905_aperture)};
        break;
      case None:;
    }
  }

  // allocate a list of parsed dap structures, to be filled in by each cpu as they run their init
  // hook
  {
    fbl::AllocChecker ac;

    debug_port *dp = new (&ac) debug_port[arch_max_num_cpus()]{};
    if (!ac.check()) {
      return;
    }
    daps = {dp, arch_max_num_cpus()};
  }

  dprintf(INFO, "DAP: enabling dap for %s\n", gBootOptions->arm64_debug_dap_rom_soc.data());

  // map the dap base into the kernel
  for (auto &da : dap_apertures) {
    LTRACEF("mapping aperture: base %#lx size %#zx mask %#x\n", da.base, da.size, da.mask);

    zx_status_t err = VmAspace::kernel_aspace()->AllocPhysical(
        "arm dap",
        da.size,          // size
        &da.virt,         // requested virtual vaddress
        PAGE_SIZE_SHIFT,  // alignment log2
        da.base,          // physical vaddress
        0,                // vmm flags
        ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE |
            ARCH_MMU_FLAG_UNCACHED_DEVICE);  // arch mmu flags
    if (err != ZX_OK) {
      printf("failed to map dap address\n");
      return;
    }

    LTRACEF("dap address %p\n", da.virt);
  }
}

LK_INIT_HOOK(arm_dap, arm_dap_init, LK_INIT_LEVEL_ARCH)

// per cpu walk the dap rom tables, looking for debug components associated with this cpu
void arm_dap_init_percpu(uint level) {
  LTRACE_ENTRY;

  const cpu_num_t curr_cpu_num = arch_curr_cpu_num();

  LTRACEF("cpu-%u mdrar %#lx\n", curr_cpu_num, __arm_rsr64("mdrar_el1"));
  LTRACEF("cpu-%u dbgauthstatus %#lx\n", curr_cpu_num, __arm_rsr64("dbgauthstatus_el1"));

  if (!dap_apertures) {
    LTRACEF("cpu-%u no apertures\n", curr_cpu_num);
    return;
  }

  const uint64_t curr_cpu_mpidr = __arm_rsr64("mpidr_el1");
  debug_port &dp = daps[curr_cpu_num];

  for (dap_aperture &da : dap_apertures) {
    if (!da.virt) {
      LTRACEF("cpu-%u not mapped, skipping aperture paddr %#lx\n", curr_cpu_num, da.base);
      continue;
    }

    // Is the aperture associated with this CPU?
    if ((cpu_num_to_mask(curr_cpu_num) & da.mask) == 0) {
      // Nope, skip it.
      LTRACEF("cpu-%u not in mask, skipping aperture paddr %#lx\n", curr_cpu_num, da.base);
      continue;
    }

    LTRACEF("cpu-%u walking ROM table at paddr %#lx, vaddr %p\n", curr_cpu_num, da.base, da.virt);

    // Walk the ROM table to find the debug interface for this CPU.
    hwreg::RegisterMmio mmio(reinterpret_cast<void *>(da.virt));
    const auto vaddr = reinterpret_cast<uintptr_t>(da.virt);
    fit::result<coresight::RomTable::WalkError> result = coresight::RomTable::Walk(
        mmio, static_cast<uint32_t>(da.size),
        [&dp, vaddr, curr_cpu_mpidr, curr_cpu_num](uint32_t offset) {
          const uintptr_t component = vaddr + offset;
          hwreg::RegisterMmio mmio(reinterpret_cast<void *>(component));
          const ComponentIdRegister::Class classid =
              ComponentIdRegister::Get().ReadFrom(&mmio).classid();

          // We're only interested in ARM-architected CoreSight components.
          const DeviceArchRegister arch_reg = DeviceArchRegister::Get().ReadFrom(&mmio);
          const uint16_t architect = arch_reg.architect()
                                         ? static_cast<uint16_t>(arch_reg.architect())
                                         : coresight::GetDesigner(mmio);
          if (architect != coresight::arm::kArchitect) {
            LTRACEF("cpu-%u ignoring component with architect %#x\n", curr_cpu_num, architect);
            return;
          }
          if (classid != ComponentIdRegister::Class::kCoreSight) {
            const ktl::string_view classid_sv = coresight::ToString(classid);
            LTRACEF("cpu-%u ignoring component with classid %.*s\n", curr_cpu_num,
                    static_cast<int>(classid_sv.size()), classid_sv.data());
            return;
          }

          // We're only interested in components for *this* CPU.
          const uint64_t component_affinity =
              DeviceAffinityRegister::Get().ReadFrom(&mmio).reg_value();
          if (component_affinity != curr_cpu_mpidr) {
            LTRACEF("cpu-%u ignoring component with affinity %#lx\n", curr_cpu_num,
                    component_affinity);
            return;
          }

          // We're only interested in Core Debug Interface and ARM Cross-Trigger Matrix components.
          const auto archid = static_cast<uint16_t>(arch_reg.archid());
          switch (archid) {
            case coresight::arm::archid::kCti:
              dp.cpu_num = curr_cpu_num;
              dp.cti = reinterpret_cast<volatile uint32_t *>(component);
              break;
            case coresight::arm::archid::kCoreDebugInterface8_0A:
            case coresight::arm::archid::kCoreDebugInterface8_1A:
            case coresight::arm::archid::kCoreDebugInterface8_2A:
              dp.cpu_num = curr_cpu_num;
              dp.dap = reinterpret_cast<volatile uint32_t *>(component);
              break;

            default:
              LTRACEF("ignoring component with archid %#x\n", archid);
              break;
          };
          if (dp.cti && dp.dap) {
            dp.initialized = true;
          }
        });
    if (result.is_error()) {
      coresight::RomTable::WalkError error = ktl::move(result).error_value();
      printf("DAP: error during ROM table walk (base %#lx) at address %#lx on cpu-%u: %.*s\n",
             da.base, da.base + error.offset, curr_cpu_num, static_cast<int>(error.reason.size()),
             error.reason.data());
    }
  }

  if (!dp.initialized) {
    printf("DAP: failed to find components for cpu-%u\n", curr_cpu_num);
  }
}

LK_INIT_HOOK_FLAGS(arm_dap_percpu, arm_dap_init_percpu, LK_INIT_LEVEL_ARCH + 1,
                   LK_INIT_FLAG_ALL_CPUS)

// helper class to access registers within a memory block
template <typename T>
class RegBlock {
 public:
  explicit RegBlock(volatile uint32_t *regs) : regs_(regs) {}

  void Write(T reg_offset, uint32_t val) {
    regs_[static_cast<size_t>(reg_offset) / 4u] = val;
    arch::DeviceMemoryBarrier();
  }

  uint32_t Read(T reg_offset) {
    uint32_t val = regs_[static_cast<size_t>(reg_offset) / 4u];
    return val;
  }

  zx_status_t WaitFor(T reg_offset, uint32_t mask, uint32_t val,
                      zx_duration_t timeout = ZX_MSEC(250)) {
    zx_time_t t;
    if (timeout != ZX_TIME_INFINITE) {
      t = current_time();
    }

    uint32_t temp;
    do {
      temp = Read(reg_offset);
      if (timeout != ZX_TIME_INFINITE && (zx_time_sub_time(current_time(), t) >= timeout)) {
        TRACEF("timed out, val %#x\n", temp);
        return ZX_ERR_TIMED_OUT;
      }
    } while ((temp & mask) != val);

    return ZX_OK;
  }

 private:
  volatile uint32_t *regs_;
};

// cti registers (move to top)
enum class cti_regs {
  CTICONTROL = 0x0,
  CTIINTACK = 0x10,
  CTIAPPPULSE = 0x1c,
  CTIOUTEN0 = 0xa0,
  CTIGATE = 0x140,
  CTILAR = 0xfb0,
  CTILSR = 0xfb4,
};

enum class dap_regs {
  DBGDTRRX = 0x80,
  EDITR = 0x84,
  EDSCR = 0x88,
  DBGDTRTX = 0x8c,
  EDRCR = 0x90,
  EDPRSR = 0x314,
  EDLAR = 0xfb0,
  EDLSR = 0xfb4,
  DBGAUTHSTATUS = 0xfb8
};

// some pre-canned arm instructions
constexpr uint32_t arm64_nop = 0xd503201f;          // nop
constexpr uint32_t arm64_msr_dbgdtr = 0xd5130400;   // msr dbgdtr_el0, x0 -- write x0 to dbgdtr
constexpr uint32_t arm64_mov_sp = 0x910003e0;       // mov x0, sp
constexpr uint32_t arm64_mrs_dlr = 0xd53b4520;      // mrs x0, dlr_el0    -- write dlr to x0
constexpr uint32_t arm64_mrs_dspsr = 0xd53b4500;    // mrs x0, dspsr_el0  -- write dspsr to x0
constexpr uint32_t arm64_mrs_esr_el1 = 0xd5385200;  // mrs x0, esr_el1    -- write esr_el1 to x0
constexpr uint32_t arm64_mrs_esr_el2 = 0xd53c5200;  // mrs x0, esr_el2    -- write esr_el2 to x0
constexpr uint32_t arm64_mrs_far_el1 = 0xd5386000;  // mrs x0, far_el1    -- write far_el1 to x0
constexpr uint32_t arm64_mrs_far_el2 = 0xd53c6000;  // mrs x0, far_el2    -- write far_el2 to x0
constexpr uint32_t arm64_mrs_elr_el1 = 0xd5384020;  // mrs x0, elr_el1    -- write elr_el1 to x0
constexpr uint32_t arm64_mrs_elr_el2 = 0xd53c4020;  // mrs x0, elr_el2    -- write elr_el2 to x0

zx_status_t run_instruction(RegBlock<dap_regs> &dap, uint32_t instruction, bool trace = false) {
  zx_status_t err;

  if (trace) {
    printf("DAP: running instruction %#x\n", instruction);
  }
  dap.Write(dap_regs::EDRCR, (1 << 3));  // clear the EDSCR.PipeAdv bit

  // wait for EDSCR.PipeAdv == 0 and EDSCR.ITE == 1
  err = dap.WaitFor(dap_regs::EDSCR, (1 << 25) | (1 << 24), (1 << 24));
  if (err != ZX_OK)
    return err;

  // write the instruction
  dap.Write(dap_regs::EDITR, instruction);

  // wait for EDSCR.PipeAdv == 1 and EDSCR.ITE == 1
  // TODO: figure out why pipeadv doesn't always set
  // err = dap.WaitFor(dap_regs::EDSCR, (1<<25) | (1<<24), (1<<25) | (1<<24));
  // if (err != ZX_OK) return err;

  if (trace) {
    printf("DAP: done running instruction %#x\n", instruction);
  }
  return ZX_OK;
}

zx_status_t read_dcc(RegBlock<dap_regs> &dap, uint64_t *val) {
  auto err = dap.WaitFor(dap_regs::EDSCR, (1 << 29), (1 << 29));  // wait for TXFull
  if (err != ZX_OK)
    return err;

  *val = ((uint64_t)dap.Read(dap_regs::DBGDTRRX) << 32) | dap.Read(dap_regs::DBGDTRTX);

  return ZX_OK;
}

// Fetch a register from the target processor.
//
// We do this by executing on the remote processor a given instruction that
// is expected to write the target register into x0. This register is then
// written to DBGDTR on the remote processor, and then read locally.
zx_status_t fetch_remote_register(RegBlock<dap_regs> &dap, uint32_t reg_read_instruction,
                                  uint64_t *result) {
  // Fetch register to x0.
  zx_status_t err = run_instruction(dap, reg_read_instruction);
  if (err != ZX_OK) {
    return err;
  }

  // Write to DBGDTR.
  err = run_instruction(dap, arm64_msr_dbgdtr);
  if (err != ZX_OK) {
    return err;
  }

  // Read the result.
  err = read_dcc(dap, result);
  if (err != ZX_OK) {
    return err;
  }

  return ZX_OK;
}

zx_status_t read_processor_state(RegBlock<dap_regs> &dap, arm64_dap_processor_state *state) {
  zx_status_t err;

  // Clear out state.
  *state = arm64_dap_processor_state{};

  // save a copy of the EDSCR which has EL level and other things
  state->edscr = dap.Read(dap_regs::EDSCR);
  uint8_t el_level = state->get_el_level();

  // read x0 - x30
  // mov xN -> dbgdtr, read out of our end of the DCC
  for (uint32_t i = 0; i <= 30; i++) {
    err = run_instruction(dap, arm64_msr_dbgdtr | i);
    if (err != ZX_OK)
      return err;

    err = read_dcc(dap, &state->r[i]);
    if (err != ZX_OK)
      return err;
  }

  // Read the PC (saved in the DLR_EL0 register), SP, and CPSR (saved in DSPSR_EL0).
  struct Register {
    uint32_t instruction;
    uint64_t *dest;
  };
  for (const auto &reg : {
           Register{.instruction = arm64_mrs_dlr, .dest = &state->pc},
           Register{.instruction = arm64_mov_sp, .dest = &state->sp},
           Register{.instruction = arm64_mrs_dspsr, .dest = &state->cpsr},
       }) {
    err = fetch_remote_register(dap, reg.instruction, reg.dest);
    if (err != ZX_OK) {
      return err;
    }
  }

  // If running in EL1 or above, fetch EL1 exception state.
  if (el_level >= 1) {
    for (const auto &reg : {
             Register{.instruction = arm64_mrs_esr_el1, .dest = &state->esr_el1},
             Register{.instruction = arm64_mrs_far_el1, .dest = &state->far_el1},
             Register{.instruction = arm64_mrs_elr_el1, .dest = &state->elr_el1},
         }) {
      err = fetch_remote_register(dap, reg.instruction, reg.dest);
      if (err != ZX_OK) {
        return err;
      }
    }
  }

  // If running in EL2 or above, fetch EL2 exception state.
  if (el_level >= 2) {
    for (const auto &reg : {
             Register{.instruction = arm64_mrs_esr_el2, .dest = &state->esr_el2},
             Register{.instruction = arm64_mrs_far_el2, .dest = &state->far_el2},
             Register{.instruction = arm64_mrs_elr_el2, .dest = &state->elr_el2},
         }) {
      err = fetch_remote_register(dap, reg.instruction, reg.dest);
      if (err != ZX_OK) {
        return err;
      }
    }
  }

  // TODO: put x0 back so the cpu could be restarted

  return ZX_OK;
}

#if LK_DEBUGLEVEL > 0
void cpu_debug_command(cpu_num_t cpu) {
  printf("attempting to debug cpu %u\n", cpu);

  if (cpu == 0 || cpu >= arch_max_num_cpus()) {
    printf("invalid cpu, cannot be 0 or out of bounds\n");
    return;
  }

  // body of the debug logic, to run on cpu 0
  auto dap_debug_thread = [](void *arg) -> int {
    AutoPreemptDisabler pd;

    cpu_num_t cpu = (cpu_num_t)(uintptr_t)(arg);
    printf("victim cpu %u\n", cpu);

    arm64_dap_processor_state state = {};
    auto err = arm64_dap_read_processor_state(cpu, &state);
    if (err != ZX_OK) {
      printf("failed to read processor state, err %d\n", err);
      return err;
    }

    state.Dump();

    return ZX_OK;
  };

  // create a thread to run on cpu 0 to run this
  auto thread =
      Thread::Create("dap debug", dap_debug_thread, (void *)(uintptr_t)cpu, DEFAULT_PRIORITY);
  thread->SetCpuAffinity(cpu_num_to_mask(0));
  thread->DetachAndResume();
}

void dump() {
  printf("mdrar %#lx\n", __arm_rsr64("mdrar_el1"));
  printf("dbgauthstatus %#lx\n", __arm_rsr64("dbgauthstatus_el1"));

  if (!dap_apertures || !daps) {
    printf("DAP not detected\n");
    return;
  }

  for (auto &da : dap_apertures) {
    printf("DAP aperture at %p, length %#zx\n", da.virt, da.size);
  }

  for (auto &dap : daps) {
    printf("cpu %u DAP %p CTI %p initialized %d\n", dap.cpu_num, dap.dap, dap.cti, dap.initialized);
  }
}

int cmd_dap(int argc, const cmd_args *argv, uint32_t flags) {
  if (argc < 2) {
  notenoughargs:
    printf("not enough arguments\n");
  usage:
    printf("usage:\n");
    printf("%s dump\n", argv[0].str);
    printf("%s cpu_debug <n>\n", argv[0].str);
    return ZX_ERR_INTERNAL;
  }

  if (!strcmp(argv[1].str, "dump")) {
    dump();
  } else if (!strcmp(argv[1].str, "cpu_debug")) {
    if (argc < 3) {
      goto notenoughargs;
    }
    cpu_debug_command(static_cast<cpu_num_t>(argv[2].u));
  } else {
    printf("unknown command\n");
    goto usage;
  }

  return ZX_OK;
}

STATIC_COMMAND_START
STATIC_COMMAND("dap", "arm debug port", &cmd_dap)
STATIC_COMMAND_END(dap)
#endif

}  // namespace

// External routines

// top level entry point, try to drop the victim cpu into debug state and dump the register state
zx_status_t arm64_dap_read_processor_state(cpu_num_t victim, arm64_dap_processor_state *state) {
  // pin ourselves to the current cpu
  AutoPreemptDisabler pd;

  if (!arm64_dap_is_enabled()) {
    return ZX_ERR_BAD_STATE;
  }
  // find the CTI for this cpu
  auto ctir = daps[victim].cti;
  auto dapr = daps[victim].dap;

  RegBlock<cti_regs> cti(ctir);
  RegBlock<dap_regs> dap(dapr);
  LTRACEF("dbgauthstatus %#x\n", dap.Read(dap_regs::DBGAUTHSTATUS));

  // try to unlock the dap
  LTRACEF("edlsr %#x\n", dap.Read(dap_regs::EDLSR));
  dap.Write(dap_regs::EDLAR, 0xC5ACCE55);
  LTRACEF("edlsr %#x\n", dap.Read(dap_regs::EDLSR));

  // unlock the cti
  LTRACEF("ctilsr %#x\n", cti.Read(cti_regs::CTILSR));
  cti.Write(cti_regs::CTILAR, 0xC5ACCE55);
  LTRACEF("ctilsr %#x\n", cti.Read(cti_regs::CTILSR));

  // enable the cti
  LTRACEF("cticontrol %#x\n", cti.Read(cti_regs::CTICONTROL));
  cti.Write(cti_regs::CTICONTROL, 1);  // make sure CTI is enabled

  // try to put the victim cpu in debug mode
  LTRACEF("ctigate %#x\n", cti.Read(cti_regs::CTIGATE));
  cti.Write(cti_regs::CTIGATE, 0);  // mask off all internal channels
  LTRACEF("ctigate %#x\n", cti.Read(cti_regs::CTIGATE));
  cti.Write(cti_regs::CTIOUTEN0, 1);    // generate input event to channel 0 debug request
  cti.Write(cti_regs::CTIAPPPULSE, 1);  // generate debug event

  // read the status register of the processor
  // TODO: add timeout
  zx_status_t err = dap.WaitFor(dap_regs::EDPRSR, (1 << 4), (1 << 4));
  if (err != ZX_OK) {
    printf("DAP: failed to drop cpu %u into debug mode, error %d\n", victim, err);
    return err;
  }

  // cpu is stopped
  printf("DAP: cpu %u is in debug state\n", victim);

  // ack the CTI
  cti.Write(cti_regs::CTIINTACK, 1);

  // shove a nop down the hole to see if it works
  err = run_instruction(dap, arm64_nop);
  if (err != ZX_OK) {
    printf("DAP: failed to run first instruction on cpu, error %d\n", err);
    return err;
  }

  // load the full state of the cpu
  err = read_processor_state(dap, state);
  if (err != ZX_OK) {
    printf("DAP: failed to read processor state, error %d\n", err);
    return err;
  }

  // TODO: restart the cpu

  return ZX_OK;
}

bool arm64_dap_is_enabled() {
  if (!dap_apertures || !daps) {
    return false;
  }

  // see if we've detected and initialized DAP ports for all the cpus
  for (const auto &dap : daps) {
    if (!dap.initialized) {
      return false;
    }
  }

  return true;
}

void arm64_dap_processor_state::Dump(FILE *fp) {
  fprintf(fp, "x0  %#18" PRIx64 " x1  %#18" PRIx64 " x2  %#18" PRIx64 " x3  %#18" PRIx64 "\n",  //
          r[0], r[1], r[2], r[3]);
  fprintf(fp, "x4  %#18" PRIx64 " x5  %#18" PRIx64 " x6  %#18" PRIx64 " x7  %#18" PRIx64 "\n",  //
          r[4], r[5], r[6], r[7]);
  fprintf(fp, "x8  %#18" PRIx64 " x9  %#18" PRIx64 " x10 %#18" PRIx64 " x11 %#18" PRIx64 "\n",  //
          r[8], r[9], r[10], r[11]);
  fprintf(fp, "x12 %#18" PRIx64 " x13 %#18" PRIx64 " x14 %#18" PRIx64 " x15 %#18" PRIx64 "\n",  //
          r[12], r[13], r[14], r[15]);
  fprintf(fp, "x16 %#18" PRIx64 " x17 %#18" PRIx64 " x18 %#18" PRIx64 " x19 %#18" PRIx64 "\n",  //
          r[16], r[17], r[18], r[19]);
  fprintf(fp, "x20 %#18" PRIx64 " x21 %#18" PRIx64 " x22 %#18" PRIx64 " x23 %#18" PRIx64 "\n",  //
          r[20], r[21], r[22], r[23]);
  fprintf(fp, "x24 %#18" PRIx64 " x25 %#18" PRIx64 " x26 %#18" PRIx64 " x27 %#18" PRIx64 "\n",  //
          r[24], r[25], r[26], r[27]);
  fprintf(fp, "x28 %#18" PRIx64 " x29 %#18" PRIx64 " lr  %#18" PRIx64 " sp  %#18" PRIx64 "\n",  //
          r[28], r[29], r[30], sp);
  fprintf(fp, "\n");
  fprintf(fp, "pc      %#18" PRIx64 "\n", pc);
  fprintf(fp, "cpsr    %#18" PRIx64 "\n", cpsr);
  fprintf(fp, "edscr   %#18" PRIx32 ": EL %u\n", edscr, get_el_level());
  fprintf(fp, "\n");
  if (get_el_level() >= 1) {
    fprintf(fp, "elr_el1 %#18" PRIx64 " far_el1 %#18" PRIx64 " esr_el1 %#18" PRIx64 "\n",  //
            elr_el1, far_el1, esr_el1);
  }
  if (get_el_level() >= 2) {
    fprintf(fp, "elr_el2 %#18" PRIx64 " far_el2 %#18" PRIx64 " esr_el2 %#18" PRIx64 "\n",  //
            elr_el2, far_el2, esr_el2);
  }
}
