// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <lib/affine/ratio.h>
#include <lib/console.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <arch/mp.h>
#include <arch/x86.h>
#include <arch/x86/apic.h>
#include <arch/x86/feature.h>
#include <fbl/alloc_checker.h>
#include <fbl/array.h>
#include <ktl/array.h>
#include <ktl/atomic.h>
#include <ktl/bit.h>
#include <ktl/iterator.h>
#include <ktl/limits.h>

/*
 * MSR Benchmark:
 *
 * This benchmark attempts to measure the cost of reading and writing MSR
 * registers (specifically, the TSC Deadline register used to implement timers
 * on x64), and the effect that doing so might have on other CPUs' performance.
 *
 * These measurements are meant to serve two purposes:
 *
 * 1) To compare the relative performance of MSR reads/writes across
 * 1a) Native HW environments (eg; running on a 'host');
 * 1b) Guest VM environments running directly inside of a host.
 * 1b) Nested guest VM environments (eg; a guest inside of a guest inside of a
 *     host)
 * 2) To see if reading/writing MSR registers on one CPU has an affect on other
 *    CPUs.
 *
 * #1 helps us to understand the cost of MSR access in a VM environment, while
 * #2 helps us to understand if a VM environments implementation of MSR access
 * affects other CPUs.  We expected that it would not, but VMs can be tricky
 * (esp. nested VMs).
 *
 * The structure of the benchmark is as follows:
 *
 * We will take measurements across a number of stages across all currently
 * online CPUs.  One of the online CPUs is considered to be the "primary" CPU,
 * while the others are considered to be "secondaries".  Each stage has two
 * "actions" it will perform, one for the primary CPU, and another for the
 * secondaries.  During the measurement for a stage, each CPU will disable
 * interrupts, and then see how many times they can complete their assigned
 * action within a fixed measurement interval.
 *
 * During the first stage, all of the CPU actions will consist of simple
 * arithmetic in order to establish a baseline.  Subsequent stages will consist
 * of the tests of MSR register reads and writes, split into two phases.  In the
 * first phase the primary CPU will perform MSR reads/writes, while the
 * secondaries run the arithmetic action.  In the second phase, all of the CPUs
 * will perform the MSR read/writes performed by the primary CPU in the first
 * phase.
 *
 * After taking measurements for each stage the test threads shut down and the
 * results are printed.  If MSR reads/writes are not having an affect on other
 * CPUs, we expect to see the arithmetic numbers for secondaries to be basically
 * unchanged from the baseline established in the first stage when the primary
 * CPU is performing MSR accesses.  Likewise, if MSR accesses have no affect on
 * other CPUs, we expect all CPUs to show the same MSR performance when running
 * concurrently as the primary CPU did when it was the only CPU performing MSR
 * accesses.
 *
 * The console thread is used to sequence the benchmarks, but is not actually
 * responsible for taking any measurements.  It creates one thread per-active
 * CPU, each of which run with default weight and has hard affinity for one of
 * the currently active CPUs.  Each of these threads will
 * spin-sleep until the console thread tells them to start the next measurement
 * stage.
 *
 * At that point in time, all of the threads become more aggressive in their
 * spinning behavior.  Once realizing that the stage has started, each CPU
 * disables interrupts, and then each secondary CPU signals to the primary that
 * they are ready to start before spin-waiting on the signal from the primary
 * CPU to start.
 *
 * The primary spin-waits for the secondaries to become ready, then assigns a
 * deadline for the stage, finally signals to everyone that the measurement is
 * ready to start.  Each thread:
 * 1) Counts the number of times they are able to make it through their stage's
 *    measurement action before the deadline.
 * 2) Records the result.
 * 3) Signals to the console thread that they are finished.
 * 4) Re-enables interrupts.
 * 5) And finally waits for the console thread to tell them to start the next
 *    stage.
 *
 * Once all of the measurements have been taken, the measurement threads exit,
 * the console thread prints the results, and finally cleans up all of the test
 * resources.
 *
 */

namespace {

class BenchmarkState;

// The structure which defines the name of, and actions for, each
// measurement stage.
struct TestStage {
  using Action = uint64_t (*)(uint64_t a, uint64_t b);
  using EnabledTest = bool (*)();
  TestStage(const char* _name, Action _primary_action, Action _secondary_action,
            EnabledTest _enabled_test)
      : name{_name},
        primary_action{_primary_action},
        secondary_action{_secondary_action},
        enabled_test(_enabled_test) {}

  bool enabled() const { return enabled_test(); }

  const char* const name;
  const Action primary_action;
  const Action secondary_action;
  const EnabledTest enabled_test;
};

// The structure which holds the result for a stage.  Specifically, the start
// time, end time, and number of times that a CPU managed to execute its action
// during the stage.  When results are printed, they are normalized to show the
// number of actions/second the CPU managed to execute.
struct StageResults {
  zx_ticks_t start{0};
  zx_ticks_t end{0};
  size_t count{0};
};

// The arithmetic action just does some simple adds and multiplies before
// exiting. Note, we need to flag our accumulator as volatile in order to
// convince the compiler to not simply optimize away this operation.
static uint64_t ArithmeticAction(uint64_t a, uint64_t b) {
  static constexpr uint32_t kCycles = 1 << 10;
  volatile uint64_t acc = 0;

  for (uint32_t i = 0; i < kCycles; ++i) {
    acc += a;
    acc *= b;
  }

  return acc;
}

// Read the TSC Deadline register 256 times.
static uint64_t TscDeadlineReadAction(uint64_t a, uint64_t b) {
  static constexpr uint32_t kCycles = 1 << 8;

  for (uint32_t i = 0; i < kCycles; ++i) {
    [[maybe_unused]] volatile const uint64_t val = read_msr(X86_MSR_IA32_TSC_DEADLINE);
  }

  return 0;
}

// Read the TSC Deadline register, then write to it 256 times before finally
// restoring it to the initially read value.
static uint64_t TscDeadlineWriteAction(uint64_t a, uint64_t b) {
  static constexpr uint32_t kCycles = 1 << 8;
  const uint64_t original = read_msr(X86_MSR_IA32_TSC_DEADLINE);

  for (uint32_t i = 0; i < kCycles; ++i) {
    write_msr(X86_MSR_IA32_TSC_DEADLINE, original + i + 1);
  }

  write_msr(X86_MSR_IA32_TSC_DEADLINE, original);
  return original;
}

// Read the LVT Timer Interrupt control register 256 times.
static uint64_t LvtTimerReadAction(uint64_t a, uint64_t b) {
  static constexpr uint32_t kCycles = 1 << 8;

  for (uint32_t i = 0; i < kCycles; ++i) {
    [[maybe_unused]] volatile const uint64_t val = read_msr(X86_MSR_IA32_X2APIC_LVT_TIMER);
  }

  return 0;
}

// Read the LVT Timer Interrupt control register, then write to it toggling the
// Masked bit 256 times.  Make sure that we also backup and restore the value in
// the TSC_DEADLINE register in the process.  When we perform a write to the
// timer interrupt control register, it will disable any armed deadline.  We can
// re-arm the deadline by writing to the deadline register again.
static uint64_t LvtTimerWriteAction(uint64_t a, uint64_t b) {
  static constexpr uint32_t kCycles = 1 << 8;
  static constexpr uint64_t kMaskBit = 0x10000;  // Intel SW Dev Manual, Vol 3, section 10.5.1
                                                 //
  const uint64_t old_deadline = read_msr(X86_MSR_IA32_TSC_DEADLINE);
  const uint64_t original = read_msr(X86_MSR_IA32_X2APIC_LVT_TIMER);
  uint64_t val = original;

  for (uint32_t i = 0; i < kCycles; ++i) {
    val ^= kMaskBit;
    write_msr(X86_MSR_IA32_X2APIC_LVT_TIMER, val);
  }

  write_msr(X86_MSR_IA32_X2APIC_LVT_TIMER, original);
  // Make sure we put an explicit MFENCE in-between the write to the timer
  // interrupt control register and the deadline register.  If the timer write
  // hits the register after the deadline write, it will disable the armed
  // deadline.
  arch::DeviceMemoryBarrier();
  write_msr(X86_MSR_IA32_TSC_DEADLINE, old_deadline);
  return original;
}

static bool enable_tscd() { return x86_feature_test(X86_FEATURE_TSC_DEADLINE); }
static bool enable_lvtt_rd() { return is_x2apic_enabled(); }
static bool enable_lvtt_wr() { return is_x2apic_enabled() && enable_tscd(); }

// The definitions of each benchmark stage.
static const ktl::array kStages{
    TestStage{"basic arithmetic", ArithmeticAction, ArithmeticAction, []() { return true; }},
    TestStage{"primary TSCD Rd", TscDeadlineReadAction, ArithmeticAction, enable_tscd},
    TestStage{"primary TSCD Wr", TscDeadlineWriteAction, ArithmeticAction, enable_tscd},
    TestStage{"all TSCD Rd", TscDeadlineReadAction, TscDeadlineReadAction, enable_tscd},
    TestStage{"all TSCD Wr", TscDeadlineWriteAction, TscDeadlineWriteAction, enable_tscd},
    TestStage{"primary LVTT Rd", LvtTimerReadAction, ArithmeticAction, enable_lvtt_rd},
    TestStage{"primary LVTT Wr", LvtTimerWriteAction, ArithmeticAction, enable_lvtt_wr},
    TestStage{"all LVTT Rd", LvtTimerReadAction, LvtTimerReadAction, enable_lvtt_rd},
    TestStage{"all LVTT Wr", LvtTimerWriteAction, LvtTimerWriteAction, enable_lvtt_wr},
};

// A structure which holds a CPUs context.  Mostly, this holds the state for a
// CPU's thread, and the results for that CPU's measurements.
struct CpuContext {
  ~CpuContext() { DEBUG_ASSERT(thread == nullptr); }

  zx_status_t Init(BenchmarkState* _owner, cpu_num_t _cpu_id, bool _is_primary,
                   thread_start_routine entry, void* arg) {
    DEBUG_ASSERT(thread == nullptr);
    DEBUG_ASSERT(cpu_id == INVALID_CPU);

    cpu_id = _cpu_id;
    is_primary = _is_primary;
    owner = _owner;

    char name[ZX_MAX_NAME_LEN];
    snprintf(name, sizeof(name), "BenchmarkState %u", cpu_id);

    // Create our thread, then set its hard affinity its assigned CPU before
    // allowing it to run.
    thread = Thread::Create(name, entry, arg, DEFAULT_PRIORITY);
    if (thread == nullptr) {
      return ZX_ERR_NO_MEMORY;
    }

    thread->SetCpuAffinity(cpu_num_to_mask(cpu_id));
    thread->Resume();

    return ZX_OK;
  }

  void Cleanup() {
    if (thread != nullptr) {
      int junk;
      thread->Join(&junk, ZX_TIME_INFINITE);
      thread = nullptr;
    }
  }

  BenchmarkState* owner{nullptr};
  Thread* thread{nullptr};
  cpu_num_t cpu_id{INVALID_CPU};
  bool is_primary{false};
  ktl::array<StageResults, ktl::size(kStages)> results;
};

// The top level state for the benchmark.  This holds each of the CPU contexts,
// as well as the atomic variables used for advancing through each the stages,
// as well as synchronizing the CPU test threads during the measurement phase of
// each stage.
class BenchmarkState {
 public:
  constexpr BenchmarkState() = default;
  ~BenchmarkState() { Cleanup(); }

  int Run();
  int RunContext(CpuContext& ctx);

 private:
  static inline constexpr zx_duration_t kMeasurementTime = ZX_SEC(1);

  bool WaitForGate(size_t gate_id) {
    while (!shutdown_now_.load() && (stage_gate_.load() < gate_id)) {
      Thread::Current::SleepRelative(ZX_MSEC(1));
    }

    return !shutdown_now_.load();
  }

  void Cleanup() {
    // Release any running threads from whatever they are doing.
    shutdown_now_.store(true);

    // Then clean them all up.
    for (auto& ctx : cpu_contexts_) {
      ctx.Cleanup();
    }
  }

  fbl::Array<CpuContext> cpu_contexts_;
  ktl::atomic<bool> shutdown_now_{false};
  ktl::atomic<size_t> stage_gate_{0};
  ktl::atomic<size_t> ready_to_start_count_{0};
  ktl::atomic<size_t> finished_count_{0};
  ktl::atomic<zx_ticks_t> ticks_deadline_{0};
};

int BenchmarkState::Run() {
  // Figure out how many CPUs we have currently online.
  cpu_mask_t online_cpus = mp_get_online_mask();
  size_t online_count = ktl::popcount(online_cpus);

  // Allocate enough context storage for the online CPUs.
  fbl::AllocChecker ac;
  cpu_contexts_.reset(new (&ac) CpuContext[online_count], online_count);
  if (!ac.check()) {
    printf("Failed to allocate %zu CpuContexts (mask 0x%08x)\n", online_count, online_cpus);
    return -1;
  }

  // Now start each of the test threads.
  cpu_num_t cpu_id = 0;
  bool is_primary = true;
  size_t ndx = 0;
  while (online_cpus) {
    if (online_cpus & 0x1) {
      zx_status_t status = cpu_contexts_[ndx].Init(
          this, cpu_id, is_primary,
          [](void* _ctx) -> int {
            CpuContext& ctx = *(reinterpret_cast<CpuContext*>(_ctx));
            return ctx.owner->RunContext(ctx);
          },
          &cpu_contexts_[ndx]);

      if (status != ZX_OK) {
        printf("Failed to initialize CpuContext for cpu %u (status %d)\n", cpu_id, status);
        return -1;
      }
      ++ndx;
    }

    ++cpu_id;
    is_primary = false;
    online_cpus >>= 1;
  }

  // Cycle all of test threads through all of the stages.
  for (size_t stage = 0; stage < kStages.size(); ++stage) {
    // Reset the stage sync state, and report which stage we are about to measure.
    const TestStage& s = kStages[stage];
    ready_to_start_count_.store(0);
    finished_count_.store(0);
    printf("%s stage \"%s\".\n", s.enabled() ? "Measuring" : "Skipping", s.name);
    Thread::Current::SleepRelative(ZX_MSEC(10));

    // Signal the threads that they may start the next measurement stage, and
    // wait until they have finished.
    stage_gate_.store(stage + 1);
    while (finished_count_.load() < cpu_contexts_.size()) {
      Thread::Current::SleepRelative(ZX_MSEC(1));
    }
  }

  // Print out results and exit, cleaning up as we go.
  printf(" %22s |", "Stage");
  for (const auto& ctx : cpu_contexts_) {
    printf("      %cCPU %2u |", ctx.is_primary ? '*' : ' ', ctx.cpu_id);
  }

  printf("\n------------------------+");
  for (size_t i = 0; i < cpu_contexts_.size(); ++i) {
    printf("--------------+");
  }
  printf("\n");

  for (size_t stage = 0; stage < kStages.size(); ++stage) {
    if (kStages[stage].enabled() == false) {
      continue;
    }
    printf(" %22s |", kStages[stage].name);
    for (const auto& ctx : cpu_contexts_) {
      DEBUG_ASSERT(kStages.size() == ctx.results.size());

      const StageResults& result = ctx.results[stage];
      const zx_ticks_t ticks_duration = result.end - result.start;
      const zx_time_t time_duration = platform_get_ticks_to_time_ratio().Scale(ticks_duration);
      if ((time_duration > 0) && (time_duration <= ktl::numeric_limits<uint32_t>::max())) {
        printf(" %12ld |",
               affine::Ratio{ZX_SEC(1), static_cast<uint32_t>(time_duration)}.Scale(result.count));
      } else {
        printf(" %12s |", "???");
      }
    }
    printf("\n");
  }

  return 0;
}

int BenchmarkState::RunContext(CpuContext& ctx) {
  const size_t cpu_count = cpu_contexts_.size();
  DEBUG_ASSERT(cpu_count >= 1);

  // Run through all of the measurement stages, syncing up with the other
  // threads at each stage.
  for (size_t stage = 0; stage < std::size(kStages); ++stage) {
    // Wait until the control thread tells us it is OK to shut interrupts off
    // and to start the next measurement.  If something goes wrong, this wait
    // will return false, and we should bail out immediately.
    if (!WaitForGate(stage + 1)) {
      return -1;
    }

    // It is time to take the next stage measurements.  Turn off interrupts for
    // the duration of the measurement cycle.
    {
      InterruptDisableGuard irqd;

      // Only take the measurement if this stage is actually enabled.
      if (kStages[stage].enabled()) {
        // Are we the "primary" CPU?  If so, wait until all of the secondary
        // CPUs are ready to go.  Then set up the deadline for the measurement
        // cycle and join the group of ready threads (signaling that the
        // measurement is ready to start).
        //
        // If we are a "secondary" CPU, simply indicate that we are ready to go,
        // and wait for all of the other CPUs to be ready as well.
        TestStage::Action action =
            ctx.is_primary ? kStages[stage].primary_action : kStages[stage].secondary_action;
        if (ctx.is_primary) {
          while (ready_to_start_count_.load() < (cpu_count - 1)) {
            arch::Yield();
          }

          zx_ticks_t ticks = platform_get_ticks_to_time_ratio().Inverse().Scale(kMeasurementTime);
          ticks_deadline_.store(current_ticks() + ticks);
          ready_to_start_count_.fetch_add(1);
        } else {
          ready_to_start_count_.fetch_add(1);
          while (ready_to_start_count_.load() < cpu_count) {
            arch::Yield();
          }
        }

        // OK, time to take the actual measurement.  See how many times we can
        // make it through the measurement action before we hit the deadline, then
        // record the start/end times, as well as the count.
        size_t count = 0;
        zx_ticks_t end = 0;
        zx_ticks_t deadline = ticks_deadline_.load();
        zx_ticks_t start = current_ticks();

        do {
          action(0xc235754ef00c463d, 0x9ba8562ddc0932cf);
          ++count;
        } while ((end = current_ticks()) < deadline);

        // Record our results;
        ctx.results[stage].start = start;
        ctx.results[stage].end = end;
        ctx.results[stage].count = count;
      }

      // Signal that we are finished, then wait until everyone else is as well.
      finished_count_.fetch_add(1);
      while (finished_count_.load() < cpu_count) {
        arch::Yield();
      }
    }
  }

  return 0;
}

int msr_bench(int argc, const cmd_args* argv, uint32_t flags) {
  fbl::AllocChecker ac;
  ktl::unique_ptr<BenchmarkState> benchmark = fbl::make_unique_checked<BenchmarkState>(&ac);

  if (!ac.check()) {
    printf("Failed to allocate benchmark context!\n");
    return -1;
  }

  return benchmark->Run();
}

STATIC_COMMAND_START
STATIC_COMMAND("msr_bench", "MSR bechmarks", msr_bench)
STATIC_COMMAND_END(msr_x64)

}  // namespace
