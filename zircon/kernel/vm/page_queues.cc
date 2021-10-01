// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/counters.h>
#include <lib/fit/defer.h>

#include <fbl/ref_counted_upgradeable.h>
#include <vm/page_queues.h>
#include <vm/scanner.h>
#include <vm/vm_cow_pages.h>

namespace {

KCOUNTER(pq_aging_reason_before_min_timeout, "pq.aging.reason_before_min_timeout")
KCOUNTER(pq_aging_spurious_wakeup, "pq.aging.spurious_wakeup")
KCOUNTER(pq_aging_timeout_with_reason, "pq.aging.timeout_with_reason")
KCOUNTER(pq_aging_reason_none, "pq.aging.reason.none")
KCOUNTER(pq_aging_reason_timeout, "pq.aging.reason.timeout")
KCOUNTER(pq_aging_reason_active_ratio, "pq.aging.reason.active_ratio")
KCOUNTER(pq_aging_reason_manual, "pq.aging.reason.manual")
KCOUNTER(pq_aging_blocked_on_lru, "pq.aging.blocked_on_lru")
KCOUNTER(pq_lru_spurious_wakeup, "pq.lru.spurious_wakeup")
KCOUNTER(pq_inactive_skipped, "pq.inactive_queue.pages_skipped")
KCOUNTER(pq_inactive_evicted, "pq.inactive_queue.pages_evicted")

}  // namespace

// To control when the active ratio triggers, without using floating or fixed point arithmetic, an
// integer multiplier is used so that (active/inactive > threshold) instead becomes
// (active*KActiveRatioMultiplier > inactive).
// This is presently an arbitrary constant, since the min and max mru rotate time are currently
// fixed at the same value, meaning that the active ratio can not presently trigger, or prevent,
// aging.
constexpr uint64_t kActiveRatioMultiplier = 2;

PageQueues::PageQueues(zx_duration_t min_mru_rotate_time, zx_duration_t max_mru_rotate_time)
    : min_mru_rotate_time_(min_mru_rotate_time), max_mru_rotate_time_(max_mru_rotate_time) {
  for (uint32_t i = 0; i < PageQueueNumQueues; i++) {
    list_initialize(&page_queues_[i]);
  }
}

PageQueues::~PageQueues() {
  StopThreads();
  for (uint32_t i = 0; i < PageQueueNumQueues; i++) {
    DEBUG_ASSERT(list_is_empty(&page_queues_[i]));
  }
  for (size_t i = 0; i < page_queue_counts_.size(); i++) {
    DEBUG_ASSERT_MSG(page_queue_counts_[i] == 0, "i=%zu count=%zu", i,
                     page_queue_counts_[i].load());
  }
}

void PageQueues::StartThreads() {
  // Cannot perform all of thread creation under the lock as thread creation requires
  // allocations so we create in temporaries first and then stash.
  Thread* mru_thread = Thread::Create(
      "page-queue-mru-thread",
      [](void* arg) -> int {
        static_cast<PageQueues*>(arg)->MruThread();
        return 0;
      },
      this, LOW_PRIORITY);
  DEBUG_ASSERT(mru_thread);

  mru_thread->Resume();

  Thread* lru_thread = Thread::Create(
      "page-queue-lru-thread",
      [](void* arg) -> int {
        static_cast<PageQueues*>(arg)->LruThread();
        return 0;
      },
      this, LOW_PRIORITY);
  DEBUG_ASSERT(lru_thread);
  lru_thread->Resume();

  Guard<CriticalMutex> guard{&lock_};
  ASSERT(!mru_thread_);
  ASSERT(!lru_thread_);
  mru_thread_ = mru_thread;
  lru_thread_ = lru_thread;
}

void PageQueues::StopThreads() {
  // Cannot wait for threads to complete with the lock held, so update state and then perform any
  // joins outside the lock.
  Thread* mru_thread = nullptr;
  Thread* lru_thread = nullptr;
  {
    Guard<CriticalMutex> guard{&lock_};
    shutdown_threads_ = true;
    if (aging_disabled_.exchange(false)) {
      aging_token_.Signal();
    }
    aging_event_.Signal();
    lru_event_.Signal();
    mru_thread = mru_thread_;
    lru_thread = lru_thread_;
  }
  int retcode;
  if (mru_thread) {
    zx_status_t status = mru_thread->Join(&retcode, ZX_TIME_INFINITE);
    ASSERT(status == ZX_OK);
  }
  if (lru_thread) {
    zx_status_t status = lru_thread->Join(&retcode, ZX_TIME_INFINITE);
    ASSERT(status == ZX_OK);
  }
}

void PageQueues::MaybeTriggerAging() {
  Guard<CriticalMutex> guard{&lock_};
  MaybeTriggerAgingLocked();
}

void PageQueues::MaybeTriggerAgingLocked() {
  if (GetAgeReasonLocked() != AgeReason::None) {
    aging_event_.Signal();
  }
}

PageQueues::AgeReason PageQueues::GetAgeReason() const {
  Guard<CriticalMutex> guard{&lock_};
  return GetAgeReasonLocked();
}

PageQueues::AgeReason PageQueues::GetAgeReasonLocked() const {
  ActiveInactiveCounts active_count = GetActiveInactiveCountsLocked();
  if (active_count.active * kActiveRatioMultiplier > active_count.inactive) {
    return AgeReason::ActiveRatio;
  }
  return AgeReason::None;
}

void PageQueues::MaybeTriggerLruProcessing() {
  if (NeedsLruProcessing()) {
    lru_event_.Signal();
  }
}

bool PageQueues::NeedsLruProcessing() const {
  // Currently only reason to trigger lru processing is if the MRU needs space.
  // Performing this unlocked is equivalently correct as grabbing the lock, reading, and dropping
  // the lock. If a caller needs to know if the lru queue needs processing *and* then perform an
  // action before that status could change, it should externally hold lock_ over this method and
  // its action.
  if (mru_gen_.load(ktl::memory_order_relaxed) - lru_gen_.load(ktl::memory_order_relaxed) ==
      kNumPagerBacked - 1) {
    return true;
  }
  return false;
}

void PageQueues::DisableAging() {
  // Validate a double DisableAging is not happening.
  if (aging_disabled_.exchange(true)) {
    panic("Mismatched disable/enable pair");
  }

  // Take the aging token. This will both wait for the aging thread to complete any in progress
  // aging, and prevent it from aging until we return it.
  aging_token_.Wait();
}

void PageQueues::EnableAging() {
  // Validate a double EnableAging is not happening.
  if (!aging_disabled_.exchange(false)) {
    panic("Mismatched disable/enable pair");
  }
  // Return the aging token, allowing the aging thread to proceed if it was waiting.
  aging_token_.Signal();
}

const char* PageQueues::string_from_age_reason(PageQueues::AgeReason reason) {
  switch (reason) {
    case AgeReason::None:
      return "None";
    case AgeReason::ActiveRatio:
      return "Active ratio";
    case AgeReason::Timeout:
      return "Timeout";
    case AgeReason::Manual:
      return "Manual";
    default:
      panic("Unreachable");
  }
}

void PageQueues::Dump() {
  // Need to grab a copy of all the counts and generations. As the lock is needed to acquire the
  // active/inactive counts, also hold the lock over the copying of the counts to avoid needless
  // races.
  uint64_t mru_gen;
  uint64_t lru_gen;
  size_t counts[kNumPagerBacked] = {};
  size_t inactive_count;
  zx_time_t last_age_time;
  AgeReason last_age_reason;
  ActiveInactiveCounts activeinactive;
  {
    Guard<CriticalMutex> guard{&lock_};
    mru_gen = mru_gen_.load(ktl::memory_order_relaxed);
    lru_gen = lru_gen_.load(ktl::memory_order_relaxed);
    inactive_count =
        page_queue_counts_[PageQueuePagerBackedInactive].load(ktl::memory_order_relaxed);
    for (uint32_t i = 0; i < kNumPagerBacked; i++) {
      counts[i] = page_queue_counts_[PageQueuePagerBackedBase + i].load(ktl::memory_order_relaxed);
    }
    activeinactive = GetActiveInactiveCountsLocked();
    last_age_time = last_age_time_.load(ktl::memory_order_relaxed);
    last_age_reason = last_age_reason_;
  }
  // Small arbitrary number that should be more than large enough to hold the constructed string
  // without causing stack allocation pressure.
  constexpr size_t kBufSize = 50;
  // Start with the buffer null terminated. snprintf will always keep it null terminated.
  char buf[kBufSize] __UNINITIALIZED = "\0";
  size_t buf_len = 0;
  // This formats the counts of all buckets, not just those within the mru->lru range, even though
  // any buckets not in that range should always have a count of zero. The format this generates is
  // [active],[active],inactive,inactive,{last inactive},should-be-zero,should-be-zero
  // Although the inactive and should-be-zero use the same formatting, they are broken up by the
  // {last inactive}.
  for (uint64_t i = 0; i < kNumPagerBacked; i++) {
    PageQueue queue = gen_to_queue(mru_gen - i);
    ASSERT(buf_len < kBufSize);
    const size_t remain = kBufSize - buf_len;
    int write_len;
    if (i < kNumActiveQueues) {
      write_len =
          snprintf(buf + buf_len, remain, "[%zu],", counts[queue - PageQueuePagerBackedBase]);
    } else if (i == mru_gen - lru_gen) {
      write_len =
          snprintf(buf + buf_len, remain, "{%zu},", counts[queue - PageQueuePagerBackedBase]);
    } else {
      write_len = snprintf(buf + buf_len, remain, "%zu,", counts[queue - PageQueuePagerBackedBase]);
    }
    // Negative values are returned on encoding errors, which we never expect to get.
    ASSERT(write_len >= 0);
    if (static_cast<uint>(write_len) >= remain) {
      // Buffer too small, just use whatever we have constructed so far.
      break;
    }
    buf_len += write_len;
  }
  zx_time_t current = current_time();
  timespec age_time = zx_timespec_from_duration(zx_time_sub_time(current, last_age_time));
  printf("pq: MRU generation is %" PRIu64
         " set %ld.%lds ago due to \"%s\", LRU generation is %" PRIu64 "\n",
         mru_gen, age_time.tv_sec, age_time.tv_nsec, string_from_age_reason(last_age_reason),
         lru_gen);
  printf("pq: Pager buckets %s evict first: %zu, %s active/inactive totals: %zu/%zu\n", buf,
         inactive_count, activeinactive.cached ? "cached" : "live", activeinactive.active,
         activeinactive.inactive);
}

// This runs the aging thread. Aging, unlike lru processing, scanning or eviction, requires very
// little work and is more about coordination. As such this thread is heavy on checks and signalling
// but generally only needs to hold any locks for the briefest of times.
// There is, currently, one exception to that, which is the calls to scanner_wait_for_accessed_scan.
// The scanner will, eventually, be a separate thread that is synchronized with, but presently
// a full scan may happen inline in that method call, and get attributed directly to this thread.
void PageQueues::MruThread() {
  // Pretend that aging happens during startup to simplify the rest of the loop logic.
  last_age_time_ = current_time();
  while (!shutdown_threads_.load(ktl::memory_order_relaxed)) {
    // Wait for the min rotate time to pass.
    Thread::Current::Sleep(
        zx_time_add_duration(last_age_time_.load(ktl::memory_order_relaxed), min_mru_rotate_time_));

    // Wait on the aging_event_ in a loop to deal with spurious signals on the event. Will exit
    // the loop for either of
    // * A legitimate age reason returned by GetAgeReason
    // * Reached the maximum wait time
    // * Shutdown has been requested.
    // We specifically check GetAgeReason() and then wait on the aging event to catch scenarios
    // where we both timed out *and* found a pending age reason. This just allows us to log this
    // scenario, and is not necessary for correctness.
    zx_status_t result = ZX_OK;
    AgeReason age_reason = AgeReason::None;
    int iterations = 0;
    while (!shutdown_threads_.load(ktl::memory_order_relaxed) &&
           (age_reason = GetAgeReason()) == AgeReason::None && result != ZX_ERR_TIMED_OUT) {
      result = aging_event_.WaitDeadline(
          zx_time_add_duration(last_age_time_.load(ktl::memory_order_relaxed),
                               max_mru_rotate_time_),
          Interruptible::No);
      iterations++;
    }

    if (shutdown_threads_.load(ktl::memory_order_relaxed)) {
      break;
    }

    if (iterations == 0) {
      // If We did zero iterations then this means there was an age_reason waiting for us, meaning
      // we wanted to age prior to the min timeout clearing.
      pq_aging_reason_before_min_timeout.Add(1);
    } else if (iterations > 1) {
      // Every loop iteration above 1 indicates a spurious wake up.
      pq_aging_spurious_wakeup.Add(iterations - 1);
    }

    // If we didn't have an age reason then since we already handled the disable_aging_ case the
    // only other age cause must be a timeout.
    if (age_reason == AgeReason::None) {
      DEBUG_ASSERT(result == ZX_ERR_TIMED_OUT);
      age_reason = AgeReason::Timeout;
    } else {
      // Had an age reason, but check if a timeout also occurred. This could happen legitimately due
      // to races or delays in scheduling this thread, but we log this case in a counter anyway
      // since it happening regularly could indicate a bug.
      if (result == ZX_ERR_TIMED_OUT) {
        pq_aging_timeout_with_reason.Add(1);
      }
    }

    // Taken the aging token, potentially blocking if aging is disabled, make sure to return it when
    // we are done.
    aging_token_.Wait();
    auto return_token = fit::defer([this] { aging_token_.Signal(); });

    // Make sure the accessed information has been harvested since the last time we aged, otherwise
    // we are deliberately making the age information coarser, by effectively not using one of the
    // queues, at which point we might as well not have bothered rotating.
    // Currently this is redundant since we will explicitly harvest just after aging, however once
    // there are additional aging triggers and harvesting is more asynchronous, this will serve as
    // a synchronization point.
    scanner_wait_for_accessed_scan(last_age_time_);

    RotatePagerBackedQueues(age_reason);

    // Changing mru_gen_ could have impacted the eviction logic.
    MaybeTriggerLruProcessing();

    // To emulate previous behavior of the system, force an accessed scan to happen now that the
    // page queues have been rotated. Preserving the existing behavior is important, as there is
    // presently a single active queue, and so we need to immediately pull any accessed pages back
    // into that active queue to prevent them from being evicted.
    scanner_wait_for_accessed_scan(ZX_TIME_INFINITE);
  }
}

// This thread should, at some point, have some of its logic and signaling merged with the Evictor.
// Currently it might process the lru queue whilst the evictor is already trying to evict, which is
// not harmful but it's a bit wasteful as it doubles the work that happens.
// LRU processing, via ProcessLruQueue, is expensive and happens under the lock_. It is expected
// that ProcessLruQueue perform small units of work to avoid this thread causing excessive lock
// contention.
void PageQueues::LruThread() {
  while (!shutdown_threads_.load(ktl::memory_order_relaxed)) {
    lru_event_.WaitDeadline(ZX_TIME_INFINITE, Interruptible::No);
    // Take the lock so we can calculate (race free) a target mru-gen
    uint64_t target_gen;
    {
      Guard<CriticalMutex> guard{&lock_};
      if (!NeedsLruProcessing()) {
        pq_lru_spurious_wakeup.Add(1);
        continue;
      }
      target_gen = lru_gen_.load(ktl::memory_order_relaxed) + 1;
    }
    // With the lock dropped process the target. This is not racy as generations are monotonic, so
    // worst case someone else already processed this generation and this call will be a no-op.
    ProcessLruQueue(target_gen, false);
  }
}

void PageQueues::RotatePagerBackedQueues(AgeReason reason) {
  VM_KTRACE_DURATION(2, "RotatePagerBackedQueues");
  // We expect LRU processing to have already happened, so first poll the mru semaphore.
  if (mru_semaphore_.Wait(Deadline::no_slack(ZX_TIME_INFINITE_PAST)) == ZX_ERR_TIMED_OUT) {
    // We should not have needed to wait for lru processing here, as it should have already been
    // made available due to earlier triggers. Although this could reasonably happen due to races or
    // delays in scheduling we record in a counter as happening regularly could indicate a bug.
    pq_aging_blocked_on_lru.Add(1);

    MaybeTriggerLruProcessing();

    // The LRU thread could take an arbitrary amount of time to get scheduled and run, so we cannot
    // enforce a deadline. However, we can assume there might be a bug and start making noise to
    // inform the user if we have waited multiples of the expected maximum aging interval, since
    // that implies we are starting to lose the requested fidelity of age information.
    while (mru_semaphore_.Wait(Deadline::after(max_mru_rotate_time_, TimerSlack::none())) ==
           ZX_ERR_TIMED_OUT) {
      printf("[pq] WARNING: Waited %" PRIi64
             " seconds for LRU thread, aging is presently stalled\n",
             max_mru_rotate_time_ / ZX_SEC(1));
    }
  }

  ASSERT(mru_gen_.load(ktl::memory_order_relaxed) - lru_gen_.load(ktl::memory_order_relaxed) <
         kNumPagerBacked - 1);

  {
    // Acquire the lock to increment the mru_gen_. This allows other queue logic to not worry about
    // mru_gen_ changing whilst they hold the lock.
    Guard<CriticalMutex> guard{&lock_};
    mru_gen_.fetch_add(1, ktl::memory_order_relaxed);
    last_age_time_ = current_time();
    last_age_reason_ = reason;
    // Update the active/inactive counts. We could be a bit smarter here since we know exactly which
    // active bucket might have changed, but this will work.
    RecalculateActiveInactiveLocked();
  }
  // Keep a count of the different reasons we have rotated.
  switch (reason) {
    case AgeReason::None:
      pq_aging_reason_none.Add(1);
      break;
    case AgeReason::Timeout:
      pq_aging_reason_timeout.Add(1);
      break;
    case AgeReason::ActiveRatio:
      pq_aging_reason_active_ratio.Add(1);
      break;
    case AgeReason::Manual:
      pq_aging_reason_manual.Add(1);
      break;
    default:
      panic("Unknown age reason");
  }
}

ktl::optional<PageQueues::VmoBacklink> PageQueues::ProcessLruQueue(uint64_t target_gen, bool peek) {
  // This assertion is <=, and not strictly <, since to evict a some queue X, the target must be
  // X+1. Hence to preserve kNumActiveQueues, we can allow target_gen to become equal to the first
  // active queue, as this will process all the non-active queues.
  ASSERT(target_gen <= mru_gen_.load(ktl::memory_order_relaxed) - (kNumActiveQueues - 1));

  const PageQueue mru_queue = mru_gen_to_queue();

  // Processing the lru queue requires holding the page_queues_ lock_. The only other actions that
  // require this lock are inserting or removing pages from the page queues. To ensure these actions
  // can complete in a small bounded time kMaxQueueWork is chosen to be very small so that the lock
  // will be regularly dropped. As processing the lru queue is not time critical and can be somewhat
  // inefficient in its operation we err on the side of doing less work per lock acquisition.
  static constexpr uint32_t kMaxQueueWork = 32;

  for (uint64_t lru = lru_gen_.load(ktl::memory_order_relaxed); lru < target_gen;
       lru = lru_gen_.load(ktl::memory_order_relaxed)) {
    VM_KTRACE_DURATION(2, "ProcessLruQueue");
    Guard<CriticalMutex> guard{&lock_};
    PageQueue queue = gen_to_queue(lru);
    uint32_t work_remain = kMaxQueueWork;
    while (!list_is_empty(&page_queues_[queue]) && work_remain > 0) {
      work_remain--;
      // Process the list from its notional oldest (tail) to notional newest (head)
      vm_page_t* page = list_peek_tail_type(&page_queues_[queue], vm_page_t, queue_node);
      PageQueue page_queue =
          (PageQueue)page->object.get_page_queue_ref().load(ktl::memory_order_relaxed);
      DEBUG_ASSERT(page_queue >= PageQueuePagerBackedBase);
      // If the queue stored in the page does not match then we want to move it to its correct queue
      // with the caveat that its queue could be invalid. The queue would be invalid if MarkAccessed
      // had raced. Should this happen we know that the page is actually *very* old, and so we will
      // fall back to the case of forcibly changing its age to the new lru gen.
      if (page_queue != queue && queue_is_valid(page_queue, queue, mru_queue)) {
        list_delete(&page->queue_node);
        list_add_head(&page_queues_[page_queue], &page->queue_node);
      } else if (peek) {
        VmCowPages* cow = reinterpret_cast<VmCowPages*>(page->object.get_object());
        uint64_t page_offset = page->object.get_page_offset();
        DEBUG_ASSERT(cow);

        // We may be racing with destruction of VMO. As we currently hold our lock we know that our
        // back pointer is correct in so far as the VmCowPages has not yet had completed running its
        // destructor, so we know it is safe to attempt to upgrade it to a RefPtr. If upgrading
        // fails we assume the page is about to be removed from the page queue once the VMO
        // destructor gets a chance to run.
        return VmoBacklink{fbl::MakeRefPtrUpgradeFromRaw(cow, lock_), page, page_offset};
      } else {
        // Force it into our target queue, don't care about races. If we happened to access it at
        // the same time then too bad.
        PageQueue new_queue = gen_to_queue(lru + 1);
        PageQueue old_queue = (PageQueue)page->object.get_page_queue_ref().exchange(new_queue);
        DEBUG_ASSERT(old_queue >= PageQueuePagerBackedBase);
        page_queue_counts_[old_queue].fetch_sub(1, ktl::memory_order_relaxed);
        page_queue_counts_[new_queue].fetch_add(1, ktl::memory_order_relaxed);
        list_delete(&page->queue_node);
        list_add_head(&page_queues_[new_queue], &page->queue_node);
        // We should only have performed this step to move from one inactive bucket to the next,
        // so there should be no active/inactive count changes needed.
        DEBUG_ASSERT(!queue_is_active(new_queue, mru_gen_to_queue()));
      }
    }
    if (list_is_empty(&page_queues_[queue])) {
      lru_gen_.store(lru + 1, ktl::memory_order_relaxed);
      mru_semaphore_.Post();
      // Changing the lru_gen_ might  in the future trigger a scenario where need to age, but this
      // is presently a no-op.
      MaybeTriggerAgingLocked();
    }
  }

  return ktl::nullopt;
}

void PageQueues::UpdateActiveInactiveLocked(PageQueue old_queue, PageQueue new_queue) {
  // Short circuit the lock acquisition and logic if not dealing with active/inactive queues
  if (!queue_is_pager_backed(old_queue) && !queue_is_pager_backed(new_queue)) {
    return;
  }
  // This just blindly updates the active/inactive counts. If accessed scanning is happening, and
  // used use_cached_queue_counts_ is true, then we could be racing and setting these to garbage
  // values. That's fine as they will never get returned anywhere, and will get reset to correct
  // values once access scanning completes.
  PageQueue mru = mru_gen_to_queue();
  if (queue_is_active(old_queue, mru)) {
    active_queue_count_--;
  } else if (queue_is_inactive(old_queue, mru)) {
    inactive_queue_count_--;
  }
  if (queue_is_active(new_queue, mru)) {
    active_queue_count_++;
  } else if (queue_is_inactive(new_queue, mru)) {
    inactive_queue_count_++;
  }
  MaybeTriggerAgingLocked();
}

void PageQueues::MarkAccessed(vm_page_t* page) {
  Guard<CriticalMutex> guard{&lock_};

  auto queue_ref = page->object.get_page_queue_ref();

  // We need to check the current queue to see if it is in the pager backed range. Between checking
  // this and updating the queue it could change, however it would only change as a result of
  // MarkAccessedDeferredCount, which would only move it to another pager backed queue. No other
  // change is possible as we are holding lock_.
  if (queue_ref.load(ktl::memory_order_relaxed) < PageQueuePagerBackedInactive) {
    return;
  }

  PageQueue queue = mru_gen_to_queue();
  PageQueue old_queue = (PageQueue)queue_ref.exchange(queue, ktl::memory_order_relaxed);
  // Double check again that this was previously pager backed
  DEBUG_ASSERT(old_queue != PageQueueNone && old_queue >= PageQueuePagerBackedInactive);
  if (old_queue != queue) {
    page_queue_counts_[old_queue].fetch_sub(1, ktl::memory_order_relaxed);
    page_queue_counts_[queue].fetch_add(1, ktl::memory_order_relaxed);
    UpdateActiveInactiveLocked(old_queue, queue);
  }
}

void PageQueues::SetQueueLocked(vm_page_t* page, PageQueue queue) {
  SetQueueBacklinkLocked(page, nullptr, 0, queue);
}

void PageQueues::SetQueueBacklinkLocked(vm_page_t* page, void* object, uintptr_t page_offset,
                                        PageQueue queue) {
  DEBUG_ASSERT(page->state() == vm_page_state::OBJECT);
  DEBUG_ASSERT(!page->is_free());
  DEBUG_ASSERT(!list_in_list(&page->queue_node));
  page->object.set_object(object);
  page->object.set_page_offset(page_offset);
  DEBUG_ASSERT(page->object.get_page_queue_ref().load(ktl::memory_order_relaxed) == PageQueueNone);
  page->object.get_page_queue_ref().store(queue, ktl::memory_order_relaxed);
  list_add_head(&page_queues_[queue], &page->queue_node);
  page_queue_counts_[queue].fetch_add(1, ktl::memory_order_relaxed);
  UpdateActiveInactiveLocked(PageQueueNone, queue);
}

void PageQueues::MoveToQueueLocked(vm_page_t* page, PageQueue queue) {
  MoveToQueueBacklinkLocked(page, nullptr, 0, queue);
}

void PageQueues::MoveToQueueBacklinkLocked(vm_page_t* page, void* object, uintptr_t page_offset,
                                           PageQueue queue) {
  DEBUG_ASSERT(page->state() == vm_page_state::OBJECT);
  DEBUG_ASSERT(!page->is_free());
  DEBUG_ASSERT(list_in_list(&page->queue_node));
  uint32_t old_queue = page->object.get_page_queue_ref().exchange(queue, ktl::memory_order_relaxed);
  DEBUG_ASSERT(old_queue != PageQueueNone);
  page->object.set_object(object);
  page->object.set_page_offset(page_offset);
  list_delete(&page->queue_node);
  list_add_head(&page_queues_[queue], &page->queue_node);
  page_queue_counts_[old_queue].fetch_sub(1, ktl::memory_order_relaxed);
  page_queue_counts_[queue].fetch_add(1, ktl::memory_order_relaxed);
  UpdateActiveInactiveLocked((PageQueue)old_queue, queue);
}

void PageQueues::SetWired(vm_page_t* page) {
  Guard<CriticalMutex> guard{&lock_};
  SetQueueLocked(page, PageQueueWired);
}

void PageQueues::MoveToWired(vm_page_t* page) {
  Guard<CriticalMutex> guard{&lock_};
  MoveToQueueLocked(page, PageQueueWired);
}

void PageQueues::SetUnswappable(vm_page_t* page) {
  Guard<CriticalMutex> guard{&lock_};
  SetQueueLocked(page, PageQueueUnswappable);
}

void PageQueues::MoveToUnswappableLocked(vm_page_t* page) {
  MoveToQueueLocked(page, PageQueueUnswappable);
}

void PageQueues::MoveToUnswappable(vm_page_t* page) {
  Guard<CriticalMutex> guard{&lock_};
  MoveToUnswappableLocked(page);
}

void PageQueues::SetPagerBacked(vm_page_t* page, VmCowPages* object, uint64_t page_offset) {
  Guard<CriticalMutex> guard{&lock_};
  SetQueueBacklinkLocked(page, object, page_offset, mru_gen_to_queue());
}

void PageQueues::MoveToPagerBacked(vm_page_t* page, VmCowPages* object, uint64_t page_offset) {
  Guard<CriticalMutex> guard{&lock_};
  MoveToQueueBacklinkLocked(page, object, page_offset, mru_gen_to_queue());
}

void PageQueues::MoveToPagerBackedInactive(vm_page_t* page) {
  Guard<CriticalMutex> guard{&lock_};
  MoveToQueueBacklinkLocked(page, page->object.get_object(), page->object.get_page_offset(),
                            PageQueuePagerBackedInactive);
}

void PageQueues::SetUnswappableZeroFork(vm_page_t* page, VmCowPages* object, uint64_t page_offset) {
  Guard<CriticalMutex> guard{&lock_};
  SetQueueBacklinkLocked(page, object, page_offset, PageQueueUnswappableZeroFork);
}

void PageQueues::MoveToUnswappableZeroFork(vm_page_t* page, VmCowPages* object,
                                           uint64_t page_offset) {
  Guard<CriticalMutex> guard{&lock_};
  MoveToQueueBacklinkLocked(page, object, page_offset, PageQueueUnswappableZeroFork);
}

void PageQueues::RemoveLocked(vm_page_t* page) {
  // Directly exchange the old gen.
  uint32_t old_queue =
      page->object.get_page_queue_ref().exchange(PageQueueNone, ktl::memory_order_relaxed);
  DEBUG_ASSERT(old_queue != PageQueueNone);
  page_queue_counts_[old_queue].fetch_sub(1, ktl::memory_order_relaxed);
  UpdateActiveInactiveLocked((PageQueue)old_queue, PageQueueNone);
  page->object.set_object(nullptr);
  page->object.set_page_offset(0);
  list_delete(&page->queue_node);
}

void PageQueues::Remove(vm_page_t* page) {
  Guard<CriticalMutex> guard{&lock_};
  RemoveLocked(page);
}

void PageQueues::RemoveArrayIntoList(vm_page_t** pages, size_t count, list_node_t* out_list) {
  DEBUG_ASSERT(pages);
  Guard<CriticalMutex> guard{&lock_};
  for (size_t i = 0; i < count; i++) {
    DEBUG_ASSERT(pages[i]);
    RemoveLocked(pages[i]);
    list_add_tail(out_list, &pages[i]->queue_node);
  }
}

void PageQueues::BeginAccessScan() {
  Guard<CriticalMutex> guard{&lock_};
  ASSERT(!use_cached_queue_counts_.load(ktl::memory_order_relaxed));
  cached_active_queue_count_ = active_queue_count_;
  cached_inactive_queue_count_ = inactive_queue_count_;
  use_cached_queue_counts_.store(true, ktl::memory_order_relaxed);
}

void PageQueues::RecalculateActiveInactiveLocked() {
  uint64_t active = 0;
  uint64_t inactive = 0;

  uint32_t lru = lru_gen_.load(ktl::memory_order_relaxed);
  uint32_t mru = mru_gen_.load(ktl::memory_order_relaxed);

  for (uint32_t index = lru; index <= mru; index++) {
    uint64_t count = page_queue_counts_[gen_to_queue(index)].load(ktl::memory_order_relaxed);
    if (queue_is_active(gen_to_queue(index), gen_to_queue(mru))) {
      active += count;
    } else {
      // As we are only operating on pager backed queues, !active should imply inactive
      DEBUG_ASSERT(queue_is_inactive(gen_to_queue(index), gen_to_queue(mru)));
      inactive += count;
    }
  }
  inactive += page_queue_counts_[PageQueuePagerBackedInactive].load(ktl::memory_order_relaxed);

  // Update the counts.
  active_queue_count_ = active;
  inactive_queue_count_ = inactive;

  // New counts might mean we need to age.
  MaybeTriggerAgingLocked();
}

void PageQueues::EndAccessScan() {
  Guard<CriticalMutex> guard{&lock_};

  ASSERT(use_cached_queue_counts_.load(ktl::memory_order_relaxed));

  // First clear the cached counts. Although the uncached counts aren't correct right now, we hold
  // the lock so no one can observe the counts right now.
  cached_active_queue_count_ = 0;
  cached_inactive_queue_count_ = 0;
  use_cached_queue_counts_.store(false, ktl::memory_order_relaxed);

  RecalculateActiveInactiveLocked();
}

PageQueues::PagerCounts PageQueues::GetPagerQueueCounts() const {
  PagerCounts counts;

  // Grab the lock to prevent LRU processing, this lets us get a slightly less racy snapshot of the
  // queue counts, although we may still double count pages that move after we count them.
  // Specifically any parallel callers of MarkAccessed could move a page and change the counts,
  // causing us to either double count or miss count that page. As these counts are not load bearing
  // we accept the very small chance of potentially being off a few pages.
  Guard<CriticalMutex> guard{&lock_};
  uint32_t lru = lru_gen_.load(ktl::memory_order_relaxed);
  uint32_t mru = mru_gen_.load(ktl::memory_order_relaxed);

  counts.total = 0;
  for (uint32_t index = lru; index <= mru; index++) {
    // Distance to the MRU determines the bucket the count goes into, with 'newest' having a
    // distance of 0 and 'oldest' having a distance of kNumPagerBacked - 1.
    uint32_t age = mru - index;
    uint64_t count = page_queue_counts_[gen_to_queue(index)].load(ktl::memory_order_relaxed);
    if (age == 0) {
      counts.newest = count;
    } else if (age == kNumPagerBacked - 1) {
      counts.oldest = count;
    }
    counts.total += count;
  }
  // Account the inactive queue length under |oldest|, since (inactive + oldest LRU) pages are
  // eligible for reclamation first. |oldest| is meant to track pages eligible for eviction first.
  uint64_t inactive_count =
      page_queue_counts_[PageQueuePagerBackedInactive].load(ktl::memory_order_relaxed);
  counts.oldest += inactive_count;
  counts.total += inactive_count;
  return counts;
}

PageQueues::Counts PageQueues::QueueCounts() const {
  Counts counts = {};

  // Grab the lock to prevent LRU processing, this lets us get a slightly less racy snapshot of the
  // queue counts. We may still double count pages that move after we count them.
  Guard<CriticalMutex> guard{&lock_};
  uint32_t lru = lru_gen_.load(ktl::memory_order_relaxed);
  uint32_t mru = mru_gen_.load(ktl::memory_order_relaxed);

  for (uint32_t index = lru; index <= mru; index++) {
    counts.pager_backed[mru - index] =
        page_queue_counts_[gen_to_queue(index)].load(ktl::memory_order_relaxed);
  }
  counts.pager_backed_inactive =
      page_queue_counts_[PageQueuePagerBackedInactive].load(ktl::memory_order_relaxed);
  counts.unswappable = page_queue_counts_[PageQueueUnswappable].load(ktl::memory_order_relaxed);
  counts.wired = page_queue_counts_[PageQueueWired].load(ktl::memory_order_relaxed);
  counts.unswappable_zero_fork =
      page_queue_counts_[PageQueueUnswappableZeroFork].load(ktl::memory_order_relaxed);
  return counts;
}

bool PageQueues::DebugPageIsPagerBacked(const vm_page_t* page, size_t* queue) const {
  PageQueue q = (PageQueue)page->object.get_page_queue_ref().load(ktl::memory_order_relaxed);
  if (q >= PageQueuePagerBackedBase && q <= PageQueuePagerBackedLast) {
    if (queue) {
      *queue = queue_age(q, mru_gen_to_queue());
    }
    return true;
  }
  return false;
}

bool PageQueues::DebugPageIsPagerBackedInactive(const vm_page_t* page) const {
  return page->object.get_page_queue_ref().load(ktl::memory_order_relaxed) ==
         PageQueuePagerBackedInactive;
}

bool PageQueues::DebugPageIsUnswappable(const vm_page_t* page) const {
  return page->object.get_page_queue_ref().load(ktl::memory_order_relaxed) == PageQueueUnswappable;
}

bool PageQueues::DebugPageIsWired(const vm_page_t* page) const {
  return page->object.get_page_queue_ref().load(ktl::memory_order_relaxed) == PageQueueWired;
}

bool PageQueues::DebugPageIsUnswappableZeroFork(const vm_page_t* page) const {
  return page->object.get_page_queue_ref().load(ktl::memory_order_relaxed) ==
         PageQueueUnswappableZeroFork;
}

bool PageQueues::DebugPageIsAnyUnswappable(const vm_page_t* page) const {
  return DebugPageIsUnswappable(page) || DebugPageIsUnswappableZeroFork(page);
}

ktl::optional<PageQueues::VmoBacklink> PageQueues::PopUnswappableZeroFork() {
  Guard<CriticalMutex> guard{&lock_};

  vm_page_t* page =
      list_peek_tail_type(&page_queues_[PageQueueUnswappableZeroFork], vm_page_t, queue_node);
  if (!page) {
    return ktl::nullopt;
  }

  VmCowPages* cow = reinterpret_cast<VmCowPages*>(page->object.get_object());
  uint64_t page_offset = page->object.get_page_offset();
  DEBUG_ASSERT(cow);
  MoveToQueueLocked(page, PageQueueUnswappable);

  // We may be racing with destruction of VMO. As we currently hold our lock we know that our
  // back pointer is correct in so far as the VmCowPages has not yet had completed running its
  // destructor, so we know it is safe to attempt to upgrade it to a RefPtr. If upgrading fails
  // we assume the page is about to be removed from the page queue once the VMO destructor gets
  // a chance to run.
  return VmoBacklink{fbl::MakeRefPtrUpgradeFromRaw(cow, guard), page, page_offset};
}

ktl::optional<PageQueues::VmoBacklink> PageQueues::PeekPagerBacked(size_t lowest_queue) {
  // Peek the tail of the inactive queue first.
  while (true) {
    // Process a single page each time to keep the critical section for the lock small.
    Guard<CriticalMutex> guard{&lock_};
    if (list_is_empty(&page_queues_[PageQueuePagerBackedInactive])) {
      break;
    }
    vm_page_t* page =
        list_peek_tail_type(&page_queues_[PageQueuePagerBackedInactive], vm_page_t, queue_node);

    // Might need to fix up the queue for this page.
    PageQueue page_queue =
        (PageQueue)page->object.get_page_queue_ref().load(ktl::memory_order_relaxed);

    // The page is no longer inactive, we need to move this page out of the inactive queue.
    // It's possible for MarkAccessed to race and change the queue again from under us, but the
    // queue can't become PageQueuePagerBackedInactive since we need the lock for that.
    if (page_queue != PageQueuePagerBackedInactive) {
      // If page_queue is still valid, move it to that queue. Otherwise, this page is very old
      // and should be moved to the lru queue and page counts should be updated accordingly. It's
      // possible that the page is so old that the queues have wrapped again and its page_queue
      // appears to be valid. However there isn't a way to distinguish that here, so respect the
      // validity of the queue as returned by queue_is_valid.
      if (queue_is_valid(page_queue, lru_gen_to_queue(), mru_gen_to_queue())) {
        list_delete(&page->queue_node);
        list_add_head(&page_queues_[page_queue], &page->queue_node);
      } else {
        PageQueue new_queue = lru_gen_to_queue();
        PageQueue old_queue = (PageQueue)page->object.get_page_queue_ref().exchange(new_queue);
        page_queue_counts_[old_queue].fetch_sub(1, ktl::memory_order_relaxed);
        page_queue_counts_[new_queue].fetch_add(1, ktl::memory_order_relaxed);
        list_delete(&page->queue_node);
        list_add_head(&page_queues_[new_queue], &page->queue_node);
      }
      pq_inactive_skipped.Add(1);
    } else {
      // It's possible for MarkAccessed to race and change the queue from under us, i.e. if the page
      // is accessed exactly when we're trying to evict it. Ignore that race, and let eviction win.
      VmCowPages* cow = reinterpret_cast<VmCowPages*>(page->object.get_object());
      uint64_t page_offset = page->object.get_page_offset();
      DEBUG_ASSERT(cow);

      pq_inactive_evicted.Add(1);

      // We may be racing with destruction of VMO. As we currently hold our lock we know that our
      // back pointer is correct in so far as the VmCowPages has not yet had completed running its
      // destructor, so we know it is safe to attempt to upgrade it to a RefPtr. If upgrading fails
      // we assume the page is about to be removed from the page queue once the VMO destructor gets
      // a chance to run.
      return VmoBacklink{fbl::MakeRefPtrUpgradeFromRaw(cow, guard), page, page_offset};
    }
  }

  // Ignore any requests to evict from the active queues as this is never allowed.
  lowest_queue = ktl::max(lowest_queue, kNumActiveQueues);
  // The target gen is 1 larger than the lowest queue because evicting from queue X is done by
  // attempting to make the lru queue be X+1.
  return ProcessLruQueue(mru_gen_.load(ktl::memory_order_relaxed) - (lowest_queue - 1), true);
}

PageQueues::ActiveInactiveCounts PageQueues::GetActiveInactiveCounts() const {
  Guard<CriticalMutex> guard{&lock_};
  return GetActiveInactiveCountsLocked();
}

PageQueues::ActiveInactiveCounts PageQueues::GetActiveInactiveCountsLocked() const {
  if (use_cached_queue_counts_.load(ktl::memory_order_relaxed)) {
    return ActiveInactiveCounts{.cached = true,
                                .active = cached_active_queue_count_,
                                .inactive = cached_inactive_queue_count_};
  } else {
    // With use_cached_queue_counts_ false the counts should have been updated to remove any
    // negative values that might have been caused by races.
    ASSERT(active_queue_count_ >= 0);
    ASSERT(inactive_queue_count_ >= 0);
    return ActiveInactiveCounts{.cached = false,
                                .active = static_cast<uint64_t>(active_queue_count_),
                                .inactive = static_cast<uint64_t>(inactive_queue_count_)};
  }
}
