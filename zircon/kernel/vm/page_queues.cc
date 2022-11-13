// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/counters.h>
#include <lib/fit/defer.h>
#include <lib/zircon-internal/macros.h>

#include <fbl/ref_counted_upgradeable.h>
#include <kernel/auto_preempt_disabler.h>
#include <object/thread_dispatcher.h>
#include <vm/page.h>
#include <vm/page_queues.h>
#include <vm/pmm.h>
#include <vm/scanner.h>
#include <vm/stack_owned_loaned_pages_interval.h>
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

}  // namespace

PageQueues::PageQueues()
    : min_mru_rotate_time_(kDefaultMinMruRotateTime),
      max_mru_rotate_time_(kDefaultMaxMruRotateTime),
      active_ratio_multiplier_(kDefaultActiveRatioMultiplier) {
  for (uint32_t i = 0; i < PageQueueNumQueues; i++) {
    list_initialize(&page_queues_[i]);
  }
  list_initialize(&dont_need_processing_list_);
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

void PageQueues::StartThreads(zx_duration_t min_mru_rotate_time,
                              zx_duration_t max_mru_rotate_time) {
  // Clamp the max rotate to the minimum.
  max_mru_rotate_time = ktl::max(min_mru_rotate_time, max_mru_rotate_time);
  // Prevent a rotation rate that is too small.
  max_mru_rotate_time = ktl::max(max_mru_rotate_time, ZX_SEC(1));

  min_mru_rotate_time_ = min_mru_rotate_time;
  max_mru_rotate_time_ = max_mru_rotate_time;

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

void PageQueues::SetActiveRatioMultiplier(uint32_t multiplier) {
  Guard<CriticalMutex> guard{&lock_};
  active_ratio_multiplier_ = multiplier;
  // The change in multiplier might have caused us to need to age.
  MaybeTriggerAgingLocked();
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
  if (active_count.active * active_ratio_multiplier_ > active_count.inactive) {
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
      kNumReclaim - 1) {
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
  size_t counts[kNumReclaim] = {};
  size_t inactive_count;
  zx_time_t last_age_time;
  AgeReason last_age_reason;
  ActiveInactiveCounts activeinactive;
  {
    Guard<CriticalMutex> guard{&lock_};
    mru_gen = mru_gen_.load(ktl::memory_order_relaxed);
    lru_gen = lru_gen_.load(ktl::memory_order_relaxed);
    inactive_count = page_queue_counts_[PageQueueReclaimDontNeed].load(ktl::memory_order_relaxed);
    for (uint32_t i = 0; i < kNumReclaim; i++) {
      counts[i] = page_queue_counts_[PageQueueReclaimBase + i].load(ktl::memory_order_relaxed);
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
  for (uint64_t i = 0; i < kNumReclaim; i++) {
    PageQueue queue = gen_to_queue(mru_gen - i);
    ASSERT(buf_len < kBufSize);
    const size_t remain = kBufSize - buf_len;
    int write_len;
    if (i < kNumActiveQueues) {
      write_len = snprintf(buf + buf_len, remain, "[%zu],", counts[queue - PageQueueReclaimBase]);
    } else if (i == mru_gen - lru_gen) {
      write_len = snprintf(buf + buf_len, remain, "{%zu},", counts[queue - PageQueueReclaimBase]);
    } else {
      write_len = snprintf(buf + buf_len, remain, "%zu,", counts[queue - PageQueueReclaimBase]);
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
    scanner_wait_for_accessed_scan(last_age_time_, true);

    RotateReclaimQueues(age_reason);

    // Changing mru_gen_ could have impacted the eviction logic.
    MaybeTriggerLruProcessing();
  }
}

// This thread should, at some point, have some of its logic and signaling merged with the Evictor.
// Currently it might process the lru queue whilst the evictor is already trying to evict, which is
// not harmful but it's a bit wasteful as it doubles the work that happens.
// LRU processing, via ProcessDontNeedAndLruQueues, is expensive and happens under the lock_. It is
// expected that ProcessDontNeedAndLruQueues perform small units of work to avoid this thread
// causing excessive lock contention.
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
    ProcessDontNeedAndLruQueues(target_gen, false);
  }
}

void PageQueues::RotateReclaimQueues(AgeReason reason) {
  VM_KTRACE_DURATION(2, "RotatePagerBackedQueues");
  // We expect LRU processing to have already happened, so first poll the mru semaphore.
  if (mru_semaphore_.Wait(Deadline::infinite_past()) == ZX_ERR_TIMED_OUT) {
    // We should not have needed to wait for lru processing here, as it should have already been
    // made available due to earlier triggers. Although this could reasonably happen due to races or
    // delays in scheduling we record in a counter as happening regularly could indicate a bug.
    pq_aging_blocked_on_lru.Add(1);

    MaybeTriggerLruProcessing();

    // The LRU thread could take an arbitrary amount of time to get scheduled and run, so we cannot
    // enforce a deadline. However, we can assume there might be a bug and start making noise to
    // inform the user if we have waited multiples of the expected maximum aging interval, since
    // that implies we are starting to lose the requested fidelity of age information.
    int64_t timeouts = 0;
    while (mru_semaphore_.Wait(Deadline::after(max_mru_rotate_time_, TimerSlack::none())) ==
           ZX_ERR_TIMED_OUT) {
      timeouts++;
      printf("[pq] WARNING: Waited %" PRIi64 " seconds for LRU thread, MRU semaphore %" PRIu64
             ",aging is presently stalled\n",
             (max_mru_rotate_time_ * timeouts) / ZX_SEC(1), mru_semaphore_.count());
      Dump();
    }
  }

  ASSERT(mru_gen_.load(ktl::memory_order_relaxed) - lru_gen_.load(ktl::memory_order_relaxed) <
         kNumReclaim - 1);

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

ktl::optional<PageQueues::VmoBacklink> PageQueues::ProcessQueueHelper(
    ProcessingQueue processing_queue, uint64_t target_gen, bool peek) {
  VM_KTRACE_DURATION(2, "ProcessQueue");

  // Processing the DontNeed/LRU queue requires holding the page_queues_ lock_. The only other
  // actions that require this lock are inserting or removing pages from the page queues. To ensure
  // these actions can complete in a small bounded time kMaxQueueWork is chosen to be very small so
  // that the lock will be regularly dropped. As processing the DontNeed/LRU queue is not time
  // critical and can be somewhat inefficient in its operation we err on the side of doing less work
  // per lock acquisition.
  //
  // Also, we need to limit the number to avoid sweep_to_loaned taking up excessive stack space.
  static constexpr uint32_t kMaxQueueWork = 16;

  // Each page in this list can potentially be replaced with a loaned page, but we have to do this
  // replacement outside the PageQueues::lock_, so we accumulate pages and then try to replace the
  // pages after lock_ is released.
  VmoBacklink sweep_to_loaned[kMaxQueueWork];
  uint32_t sweep_to_loaned_count = 0;
  // Only accumulate pages to try to replace with loaned pages if loaned pages are available and
  // we're allowed to borrow at this code location.
  bool do_sweeping = (pmm_count_loaned_free_pages() != 0) &&
                     pmm_physical_page_borrowing_config()->is_borrowing_on_mru_enabled();
  auto sweep_to_loaned_outside_lock =
      fit::defer([do_sweeping, &sweep_to_loaned, &sweep_to_loaned_count] {
        DEBUG_ASSERT(do_sweeping || sweep_to_loaned_count == 0);
        for (uint32_t i = 0; i < sweep_to_loaned_count; ++i) {
          DEBUG_ASSERT(sweep_to_loaned[i].cow);
          // We ignore the return value because the page may have moved, become pinned, we may not
          // have any free loaned pages any more, or the VmCowPages may not be able to borrow.
          sweep_to_loaned[i].cow->ReplacePageWithLoaned(sweep_to_loaned[i].page,
                                                        sweep_to_loaned[i].offset);
        }
      });

  Guard<CriticalMutex> guard{&lock_};
  const uint64_t mru = mru_gen_.load(ktl::memory_order_relaxed);
  const uint64_t lru = lru_gen_.load(ktl::memory_order_relaxed);

  // If we're processing the lru queue and it has already hit the target gen, return early.
  if (processing_queue == ProcessingQueue::Lru && lru >= target_gen) {
    return ktl::nullopt;
  }

  uint32_t work_remain = kMaxQueueWork;
  PageQueue queue;
  list_node* operating_queue;
  if (processing_queue == ProcessingQueue::Lru) {
    queue = gen_to_queue(lru);
    operating_queue = &page_queues_[queue];
  } else {
    // Processing the DontNeed queue typically involves moving from the Processing to the regular
    // DontNeed queue, unless we're peeking in which case we can also grab from the regular. When
    // peeking we prefer to grab from the dont_need_processign_list_ first, as its pages are older,
    // or at least were moved to the DontNeed queue further in the past.
    queue = PageQueueReclaimDontNeed;
    if (peek && list_is_empty(&dont_need_processing_list_)) {
      operating_queue = &page_queues_[queue];
    } else {
      operating_queue = &dont_need_processing_list_;
    }
  }

  while (!list_is_empty(operating_queue) && work_remain > 0) {
    work_remain--;
    // When moving pages we want to maintain relative page age as far as possible. For pages in the
    // LRU queue this means the pages we are moving have overall been touched more recently than
    // their destination, and so we want to take from the oldest (tail). The opposite is true for
    // the don't need process queue, as they are less recently used than their target queue.
    // When not moving pages, aka peeking, we always want the oldest pages though.
    vm_page_t* page;
    if (processing_queue == ProcessingQueue::Lru || peek) {
      page = list_peek_tail_type(operating_queue, vm_page_t, queue_node);
    } else {
      page = list_peek_head_type(operating_queue, vm_page_t, queue_node);
    }
    PageQueue page_queue =
        (PageQueue)page->object.get_page_queue_ref().load(ktl::memory_order_relaxed);

    if (processing_queue == ProcessingQueue::DontNeed) {
      DEBUG_ASSERT(page_queue >= PageQueueReclaimDontNeed);
    } else {
      DEBUG_ASSERT(page_queue >= PageQueueReclaimBase);
    }

    // If the queue stored in the page does not match then we want to move it to its correct queue
    // with the caveat that its queue could be invalid. The queue would be invalid if MarkAccessed
    // had raced. Should this happen we know that the page is actually *very* old, and so we will
    // fall back to the case of forcibly changing its age to the new lru gen.
    const PageQueue mru_queue = gen_to_queue(mru);
    // A page in the DontNeed queue will never be allowed to age to the point that its queue
    // becomes invalid, because we process *all* the DontNeed pages each time we change the lru.
    // In other words, no DontNeed page will be left behind when lru advances.
    DEBUG_ASSERT(page_queue == queue || processing_queue != ProcessingQueue::DontNeed ||
                 queue_is_valid(page_queue, gen_to_queue(lru), mru_queue));
    if (page_queue != queue && queue_is_valid(page_queue, gen_to_queue(lru), mru_queue)) {
      list_delete(&page->queue_node);
      list_add_head(&page_queues_[page_queue], &page->queue_node);

      if (queue_is_active(page_queue, mru_queue) && do_sweeping && !page->is_loaned()) {
        DEBUG_ASSERT(sweep_to_loaned_count < kMaxQueueWork);
        VmCowPages* cow = reinterpret_cast<VmCowPages*>(page->object.get_object());
        uint64_t page_offset = page->object.get_page_offset();
        DEBUG_ASSERT(cow);
        sweep_to_loaned[sweep_to_loaned_count] =
            VmoBacklink{fbl::MakeRefPtrUpgradeFromRaw(cow, lock_), page, page_offset};
        if (sweep_to_loaned[sweep_to_loaned_count].cow) {
          ++sweep_to_loaned_count;
        }
      }
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
      PageQueue new_queue = processing_queue == ProcessingQueue::DontNeed ? PageQueueReclaimDontNeed
                                                                          : gen_to_queue(lru + 1);
      PageQueue old_queue = (PageQueue)page->object.get_page_queue_ref().exchange(new_queue);
      if (processing_queue == ProcessingQueue::DontNeed) {
        DEBUG_ASSERT(old_queue >= PageQueueReclaimDontNeed);
      } else {
        DEBUG_ASSERT(old_queue >= PageQueueReclaimBase);
      }

      page_queue_counts_[old_queue].fetch_sub(1, ktl::memory_order_relaxed);
      page_queue_counts_[new_queue].fetch_add(1, ktl::memory_order_relaxed);
      list_delete(&page->queue_node);
      if (processing_queue == ProcessingQueue::DontNeed) {
        // When pulling from the DontNeedProcessing queue we pulled from the head, and so we
        // conversely need to place them in the tail to preserve order (see comment at start of the
        // loop).
        list_add_tail(&page_queues_[new_queue], &page->queue_node);
      } else {
        list_add_head(&page_queues_[new_queue], &page->queue_node);
      }
      // We should only have performed this step to move from one inactive bucket to the next,
      // so there should be no active/inactive count changes needed.
      DEBUG_ASSERT(!queue_is_active(new_queue, mru_gen_to_queue()));
    }
  }

  if (list_is_empty(operating_queue)) {
    if (processing_queue == ProcessingQueue::Lru) {
      lru_gen_.store(lru + 1, ktl::memory_order_relaxed);
      mru_semaphore_.Post();
      // Changing the lru_gen_ might  in the future trigger a scenario where need to age, but this
      // is presently a no-op.
      MaybeTriggerAgingLocked();
    }
  }

  return ktl::nullopt;
}

ktl::optional<PageQueues::VmoBacklink> PageQueues::ProcessDontNeedAndLruQueues(uint64_t target_gen,
                                                                               bool peek) {
  // This assertion is <=, and not strictly <, since to evict a some queue X, the target must be
  // X+1. Hence to preserve kNumActiveQueues, we can allow target_gen to become equal to the first
  // active queue, as this will process all the non-active queues. Although we might refresh our
  // value for the mru_queue, since the mru_gen_ is monotonic increasing, if this assert passes once
  // it should continue to be true.
  ASSERT(target_gen <= mru_gen_.load(ktl::memory_order_relaxed) - (kNumActiveQueues - 1));

  // Calculate a truly worst case loop iteration count based on every page being in either the LRU
  // queue or the wrong DontNeed queue and needing to iterate the LRU multiple steps to the
  // target_gen. Instead of reading the LRU and comparing the target_gen, just add a buffer of the
  // maximum number of page queues.
  ActiveInactiveCounts active_inactive = GetActiveInactiveCounts();
  const uint64_t max_dont_need_iterations = page_queue_counts_[PageQueueReclaimDontNeed] + 1;
  const uint64_t max_lru_iterations =
      active_inactive.active + active_inactive.inactive + kNumReclaim;
  // Loop iteration counting is just for diagnostic purposes.
  uint64_t loop_iterations = 0;

  ktl::optional<Guard<Mutex>> dont_need_processing_guard;
  if (!peek) {
    // If not peeking then we will need to properly process the DontNeed queue, and so we must take
    // the processing lock and move the existing pages into the processing list.
    dont_need_processing_guard.emplace(&dont_need_processing_lock_);
    Guard<CriticalMutex> guard{&lock_};
    ASSERT(list_is_empty(&dont_need_processing_list_));
    list_move(&page_queues_[PageQueueReclaimDontNeed], &dont_need_processing_list_);
  }

  while (true) {
    VM_KTRACE_DURATION(2, "ProcessDontNeedQueue");
    if (loop_iterations++ == max_dont_need_iterations) {
      printf("[pq]: WARNING: %s exceeded expected max DontNeed loop iterations %" PRIu64 "\n",
             __FUNCTION__, max_dont_need_iterations);
    }
    auto optional_backlink = ProcessQueueHelper(ProcessingQueue::DontNeed, 0, peek);
    if (optional_backlink != ktl::nullopt) {
      DEBUG_ASSERT(peek);
      return optional_backlink;
    }

    Guard<CriticalMutex> guard{&lock_};
    // If we have been asked to peek, we want to keep processing until both the DontNeed queues
    // are empty. ProcessQueueHelper will process one queue and then move on to the other when
    // the first is empty, so that both get processed.
    if (list_is_empty(&dont_need_processing_list_) &&
        (!peek || list_is_empty(&page_queues_[PageQueueReclaimDontNeed]))) {
      break;
    }
  }
  // Not strictly required, but we're done with the dontneed processing lock.
  dont_need_processing_guard.reset();

  // Reset diagnostic counter for the new loop.
  loop_iterations = 0;
  // Process the lru queue to reach target_gen.
  while (lru_gen_.load(ktl::memory_order_relaxed) < target_gen) {
    VM_KTRACE_DURATION(2, "ProcessLruQueue");
    if (loop_iterations++ == max_lru_iterations) {
      printf("[pq]: WARNING: %s exceeded expected max LRU loop iterations %" PRIu64 "\n",
             __FUNCTION__, max_lru_iterations);
    }
    auto optional_backlink = ProcessQueueHelper(ProcessingQueue::Lru, target_gen, peek);
    if (optional_backlink != ktl::nullopt) {
      return optional_backlink;
    }
  }

  return ktl::nullopt;
}

void PageQueues::UpdateActiveInactiveLocked(PageQueue old_queue, PageQueue new_queue) {
  // Short circuit the lock acquisition and logic if not dealing with active/inactive queues
  if (!queue_is_reclaim(old_queue) && !queue_is_reclaim(new_queue)) {
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

  // The page can be the zero page, but in that case we'll return early below.
  DEBUG_ASSERT(page != vm_get_zero_page() ||
               queue_ref.load(ktl::memory_order_relaxed) < PageQueueReclaimDontNeed);

  // We need to check the current queue to see if it is in the reclaimable range. Between checking
  // this and updating the queue it could change, however it would only change as a result of
  // MarkAccessedDeferredCount, which would only move it to another reclaimable queue. No other
  // change is possible as we are holding lock_.
  if (queue_ref.load(ktl::memory_order_relaxed) < PageQueueReclaimDontNeed) {
    return;
  }

  PageQueue queue = mru_gen_to_queue();
  PageQueue old_queue = (PageQueue)queue_ref.exchange(queue, ktl::memory_order_relaxed);
  // Double check again that this was previously reclaimable
  DEBUG_ASSERT(old_queue != PageQueueNone && old_queue >= PageQueueReclaimDontNeed);
  if (old_queue != queue) {
    page_queue_counts_[old_queue].fetch_sub(1, ktl::memory_order_relaxed);
    page_queue_counts_[queue].fetch_add(1, ktl::memory_order_relaxed);
    UpdateActiveInactiveLocked(old_queue, queue);
  }
}

void PageQueues::SetQueueBacklinkLocked(vm_page_t* page, void* object, uintptr_t page_offset,
                                        PageQueue queue) {
  DEBUG_ASSERT(page->state() == vm_page_state::OBJECT);
  DEBUG_ASSERT(!page->is_free());
  DEBUG_ASSERT(!list_in_list(&page->queue_node));
  DEBUG_ASSERT(object);
  DEBUG_ASSERT(!page->object.get_object());
  DEBUG_ASSERT(page->object.get_page_offset() == 0);

  page->object.set_object(object);
  page->object.set_page_offset(page_offset);

  DEBUG_ASSERT(page->object.get_page_queue_ref().load(ktl::memory_order_relaxed) == PageQueueNone);
  page->object.get_page_queue_ref().store(queue, ktl::memory_order_relaxed);
  list_add_head(&page_queues_[queue], &page->queue_node);
  page_queue_counts_[queue].fetch_add(1, ktl::memory_order_relaxed);
  UpdateActiveInactiveLocked(PageQueueNone, queue);
}

void PageQueues::MoveToQueueLocked(vm_page_t* page, PageQueue queue) {
  DEBUG_ASSERT(page->state() == vm_page_state::OBJECT);
  DEBUG_ASSERT(!page->is_free());
  DEBUG_ASSERT(list_in_list(&page->queue_node));
  DEBUG_ASSERT(page->object.get_object());
  uint32_t old_queue = page->object.get_page_queue_ref().exchange(queue, ktl::memory_order_relaxed);
  DEBUG_ASSERT(old_queue != PageQueueNone);

  list_delete(&page->queue_node);
  list_add_head(&page_queues_[queue], &page->queue_node);
  page_queue_counts_[old_queue].fetch_sub(1, ktl::memory_order_relaxed);
  page_queue_counts_[queue].fetch_add(1, ktl::memory_order_relaxed);
  UpdateActiveInactiveLocked(static_cast<PageQueue>(old_queue), queue);
}

void PageQueues::SetWired(vm_page_t* page, VmCowPages* object, uint64_t page_offset) {
  Guard<CriticalMutex> guard{&lock_};
  DEBUG_ASSERT(object);
  SetQueueBacklinkLocked(page, object, page_offset, PageQueueWired);
}

void PageQueues::MoveToWired(vm_page_t* page) {
  Guard<CriticalMutex> guard{&lock_};
  MoveToQueueLocked(page, PageQueueWired);
}

void PageQueues::SetAnonymous(vm_page_t* page, VmCowPages* object, uint64_t page_offset) {
  Guard<CriticalMutex> guard{&lock_};
  DEBUG_ASSERT(object);
  SetQueueBacklinkLocked(page, object, page_offset,
                         kAnonymousIsReclaimable ? mru_gen_to_queue() : PageQueueAnonymous);
}

void PageQueues::MoveToAnonymous(vm_page_t* page) {
  Guard<CriticalMutex> guard{&lock_};
  MoveToQueueLocked(page, kAnonymousIsReclaimable ? mru_gen_to_queue() : PageQueueAnonymous);
}

void PageQueues::SetPagerBacked(vm_page_t* page, VmCowPages* object, uint64_t page_offset) {
  Guard<CriticalMutex> guard{&lock_};
  DEBUG_ASSERT(object);
  SetQueueBacklinkLocked(page, object, page_offset, mru_gen_to_queue());
}

void PageQueues::MoveToPagerBacked(vm_page_t* page) {
  Guard<CriticalMutex> guard{&lock_};
  MoveToQueueLocked(page, mru_gen_to_queue());
}

void PageQueues::MoveToPagerBackedDontNeed(vm_page_t* page) {
  Guard<CriticalMutex> guard{&lock_};
  MoveToQueueLocked(page, PageQueueReclaimDontNeed);
}

void PageQueues::SetPagerBackedDirty(vm_page_t* page, VmCowPages* object, uint64_t page_offset) {
  Guard<CriticalMutex> guard{&lock_};
  DEBUG_ASSERT(object);
  SetQueueBacklinkLocked(page, object, page_offset, PageQueuePagerBackedDirty);
}

void PageQueues::MoveToPagerBackedDirty(vm_page_t* page) {
  Guard<CriticalMutex> guard{&lock_};
  MoveToQueueLocked(page, PageQueuePagerBackedDirty);
}

void PageQueues::SetAnonymousZeroFork(vm_page_t* page, VmCowPages* object, uint64_t page_offset) {
  Guard<CriticalMutex> guard{&lock_};
  SetQueueBacklinkLocked(page, object, page_offset,
                         kZeroForkIsReclaimable ? mru_gen_to_queue() : PageQueueAnonymousZeroFork);
}

void PageQueues::MoveToAnonymousZeroFork(vm_page_t* page) {
  Guard<CriticalMutex> guard{&lock_};
  MoveToQueueLocked(page, kZeroForkIsReclaimable ? mru_gen_to_queue() : PageQueueAnonymousZeroFork);
}

void PageQueues::ChangeObjectOffset(vm_page_t* page, VmCowPages* object, uint64_t page_offset) {
  Guard<CriticalMutex> guard{&lock_};
  ChangeObjectOffsetLocked(page, object, page_offset);
}

void PageQueues::ChangeObjectOffsetLocked(vm_page_t* page, VmCowPages* object,
                                          uint64_t page_offset) {
  DEBUG_ASSERT(page->state() == vm_page_state::OBJECT);
  DEBUG_ASSERT(!page->is_free());
  DEBUG_ASSERT(list_in_list(&page->queue_node));
  DEBUG_ASSERT(object);
  DEBUG_ASSERT(page->object.get_object());
  page->object.set_object(object);
  page->object.set_page_offset(page_offset);
}

void PageQueues::RemoveLocked(vm_page_t* page) {
  // Directly exchange the old gen.
  uint32_t old_queue =
      page->object.get_page_queue_ref().exchange(PageQueueNone, ktl::memory_order_relaxed);
  DEBUG_ASSERT(old_queue != PageQueueNone);
  page_queue_counts_[old_queue].fetch_sub(1, ktl::memory_order_relaxed);
  UpdateActiveInactiveLocked((PageQueue)old_queue, PageQueueNone);
  page->object.clear_object();
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

  uint64_t lru = lru_gen_.load(ktl::memory_order_relaxed);
  uint64_t mru = mru_gen_.load(ktl::memory_order_relaxed);

  for (uint64_t index = lru; index <= mru; index++) {
    uint64_t count = page_queue_counts_[gen_to_queue(index)].load(ktl::memory_order_relaxed);
    if (queue_is_active(gen_to_queue(index), gen_to_queue(mru))) {
      active += count;
    } else {
      // As we are only operating on reclaimable queues, !active should imply inactive
      DEBUG_ASSERT(queue_is_inactive(gen_to_queue(index), gen_to_queue(mru)));
      inactive += count;
    }
  }
  inactive += page_queue_counts_[PageQueueReclaimDontNeed].load(ktl::memory_order_relaxed);

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

PageQueues::ReclaimCounts PageQueues::GetReclaimQueueCounts() const {
  ReclaimCounts counts;

  // Grab the lock to prevent LRU processing, this lets us get a slightly less racy snapshot of
  // the queue counts, although we may still double count pages that move after we count them.
  // Specifically any parallel callers of MarkAccessed could move a page and change the counts,
  // causing us to either double count or miss count that page. As these counts are not load
  // bearing we accept the very small chance of potentially being off a few pages.
  Guard<CriticalMutex> guard{&lock_};
  uint64_t lru = lru_gen_.load(ktl::memory_order_relaxed);
  uint64_t mru = mru_gen_.load(ktl::memory_order_relaxed);

  counts.total = 0;
  for (uint64_t index = lru; index <= mru; index++) {
    uint64_t count = page_queue_counts_[gen_to_queue(index)].load(ktl::memory_order_relaxed);
    // Distance to the MRU, and not the LRU, determines the bucket the count goes into. This is to
    // match the logic in PeekPagerBacked, which is also based on distance to MRU.
    if (index > mru - kNumActiveQueues) {
      counts.newest += count;
    } else if (index <= mru - (kNumReclaim - kNumOldestQueues)) {
      counts.oldest += count;
    }
    counts.total += count;
  }
  // Account the DontNeed queue length under |oldest|, since (DontNeed + oldest LRU) pages are
  // eligible for reclamation first. |oldest| is meant to track pages eligible for eviction first.
  uint64_t inactive_count =
      page_queue_counts_[PageQueueReclaimDontNeed].load(ktl::memory_order_relaxed);
  counts.oldest += inactive_count;
  counts.total += inactive_count;
  return counts;
}

PageQueues::Counts PageQueues::QueueCounts() const {
  Counts counts = {};

  // Grab the lock to prevent LRU processing, this lets us get a slightly less racy snapshot of
  // the queue counts. We may still double count pages that move after we count them.
  Guard<CriticalMutex> guard{&lock_};
  uint64_t lru = lru_gen_.load(ktl::memory_order_relaxed);
  uint64_t mru = mru_gen_.load(ktl::memory_order_relaxed);

  for (uint64_t index = lru; index <= mru; index++) {
    counts.reclaim[mru - index] =
        page_queue_counts_[gen_to_queue(index)].load(ktl::memory_order_relaxed);
  }
  counts.reclaim_dont_need =
      page_queue_counts_[PageQueueReclaimDontNeed].load(ktl::memory_order_relaxed);
  counts.anonymous = page_queue_counts_[PageQueueAnonymous].load(ktl::memory_order_relaxed);
  counts.wired = page_queue_counts_[PageQueueWired].load(ktl::memory_order_relaxed);
  counts.anonymous_zero_fork =
      page_queue_counts_[PageQueueAnonymousZeroFork].load(ktl::memory_order_relaxed);
  return counts;
}

template <typename F>
bool PageQueues::DebugPageIsSpecificReclaim(const vm_page_t* page, F validator,
                                            size_t* queue) const {
  fbl::RefPtr<VmCowPages> cow_pages;
  {
    Guard<CriticalMutex> guard{&lock_};
    PageQueue q = (PageQueue)page->object.get_page_queue_ref().load(ktl::memory_order_relaxed);
    if (q < PageQueueReclaimBase || q > PageQueueReclaimLast) {
      return false;
    }
    if (queue) {
      *queue = queue_age(q, mru_gen_to_queue());
    }
    VmCowPages* cow = reinterpret_cast<VmCowPages*>(page->object.get_object());
    DEBUG_ASSERT(cow);
    cow_pages = fbl::MakeRefPtrUpgradeFromRaw(cow, guard);
  }
  return cow_pages && validator(cow_pages);
}

template <typename F>
bool PageQueues::DebugPageIsSpecificQueue(const vm_page_t* page, PageQueue queue,
                                          F validator) const {
  fbl::RefPtr<VmCowPages> cow_pages;
  {
    Guard<CriticalMutex> guard{&lock_};
    PageQueue q = (PageQueue)page->object.get_page_queue_ref().load(ktl::memory_order_relaxed);
    if (q != queue) {
      return false;
    }
    VmCowPages* cow = reinterpret_cast<VmCowPages*>(page->object.get_object());
    DEBUG_ASSERT(cow);
    cow_pages = fbl::MakeRefPtrUpgradeFromRaw(cow, guard);
  }
  return cow_pages && validator(cow_pages);
}

bool PageQueues::DebugPageIsPagerBacked(const vm_page_t* page, size_t* queue) const {
  return DebugPageIsSpecificReclaim(
      page, [](auto cow) { return cow->can_evict(); }, queue);
}

bool PageQueues::DebugPageIsPagerBackedDontNeed(const vm_page_t* page) const {
  return DebugPageIsSpecificQueue(page, PageQueueReclaimDontNeed,
                                  [](auto cow) { return cow->can_evict(); });
}

bool PageQueues::DebugPageIsPagerBackedDirty(const vm_page_t* page) const {
  return page->object.get_page_queue_ref().load(ktl::memory_order_relaxed) ==
         PageQueuePagerBackedDirty;
}

bool PageQueues::DebugPageIsAnonymous(const vm_page_t* page) const {
  if (ReclaimIsOnlyPagerBacked()) {
    return page->object.get_page_queue_ref().load(ktl::memory_order_relaxed) == PageQueueAnonymous;
  }
  return DebugPageIsSpecificReclaim(
      page, [](auto cow) { return !cow->can_evict(); }, nullptr);
}

bool PageQueues::DebugPageIsWired(const vm_page_t* page) const {
  return page->object.get_page_queue_ref().load(ktl::memory_order_relaxed) == PageQueueWired;
}

bool PageQueues::DebugPageIsAnonymousZeroFork(const vm_page_t* page) const {
  if (ReclaimIsOnlyPagerBacked()) {
    return page->object.get_page_queue_ref().load(ktl::memory_order_relaxed) ==
           PageQueueAnonymousZeroFork;
  }
  return DebugPageIsSpecificReclaim(
      page, [](auto cow) { return !cow->can_evict(); }, nullptr);
}

bool PageQueues::DebugPageIsAnyAnonymous(const vm_page_t* page) const {
  return DebugPageIsAnonymous(page) || DebugPageIsAnonymousZeroFork(page);
}

ktl::optional<PageQueues::VmoBacklink> PageQueues::PopAnonymousZeroFork() {
  Guard<CriticalMutex> guard{&lock_};

  vm_page_t* page =
      list_peek_tail_type(&page_queues_[PageQueueAnonymousZeroFork], vm_page_t, queue_node);
  if (!page) {
    return ktl::nullopt;
  }

  VmCowPages* cow = reinterpret_cast<VmCowPages*>(page->object.get_object());
  uint64_t page_offset = page->object.get_page_offset();
  DEBUG_ASSERT(cow);
  MoveToQueueLocked(page, PageQueueAnonymous);

  // We may be racing with destruction of VMO. As we currently hold our lock we know that our
  // back pointer is correct in so far as the VmCowPages has not yet had completed running its
  // destructor, so we know it is safe to attempt to upgrade it to a RefPtr. If upgrading fails
  // we assume the page is about to be removed from the page queue once the VMO destructor gets
  // a chance to run.
  return VmoBacklink{fbl::MakeRefPtrUpgradeFromRaw(cow, guard), page, page_offset};
}

ktl::optional<PageQueues::VmoBacklink> PageQueues::PeekReclaim(size_t lowest_queue) {
  // Ignore any requests to evict from the active queues as this is never allowed.
  lowest_queue = ktl::max(lowest_queue, kNumActiveQueues);
  // The target gen is 1 larger than the lowest queue because evicting from queue X is done by
  // attempting to make the lru queue be X+1.
  return ProcessDontNeedAndLruQueues(mru_gen_.load(ktl::memory_order_relaxed) - (lowest_queue - 1),
                                     true);
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

ktl::optional<PageQueues::VmoContainerBacklink> PageQueues::GetCowWithReplaceablePage(
    vm_page_t* page, VmCowPages* owning_cow) {
  // Wait for the page to not be in a transient state.  This is in a loop, since the wait happens
  // outside the lock, so another thread doing commit/decommit on owning_cow can cause the page
  // state to change, potentially multiple times.
  //
  // While it's possible for another thread that's concurrently committing/decommitting this page
  // to/from owning_cow, or moving the page from one VmCowPages to another without going through
  // FREE, to interfere to some extent with this thread's progress toward a terminal state in this
  // loop (and the caller's loop), this interference is fairly similar to page eviction interfering
  // with progress of commit of a pager-backed range.  That said, we mitigate here by tracking which
  // cases we've seen that we only expect to see once in the absence of commit/decommit interference
  // by another thread.  Thanks to loan_cancelled, we can limit all the wait required cases to a max
  // of once.  This mitigation doesn't try to maximally detect interference and minimize iterations
  // but the mitigation does limit iterations to a finite number.
  //
  // TODO(dustingreen):
  //  * complain on excessive loop iterations / duration looping
  //  * complain on excessive lifetime duration of StackOwnedLoanedPagesInterval, probably during
  //    destructor, but consider if there's any cheap and simple enough way to complain if it's just
  //    existing too long without any pre-existing calls on it.
  uint loop_iterations = 0;
  while (true) {
    // Warn on excessive iterations. The threshold is chosen to be quite high since this isn't
    // intending to check some strict finite bound, but rather to find pathological bugs where this
    // is infinite looping and monopolizing the lock_.
    if (loop_iterations++ == 200) {
      printf("[pq]: WARNING: %s appears to be looping excessively\n", __FUNCTION__);
    }
    // This is just for asserting that we don't end up trying to wait when we didn't intend to.
    bool wait_on_stack_ownership = false;
    {  // scope guard
      Guard<CriticalMutex> guard{&lock_};
      // While holding lock_, we can safely add an event to be notified, if needed.  While a page
      // state transition from ALLOC to OBJECT, and from OBJECT with no VmCowPages to OBJECT with a
      // VmCowPages, are both guarded by lock_, a transition to FREE is not.  So we must check
      // again, in an ordered fashion (using PmmNode lock not just "relaxed" atomic) for the page
      // being in FREE state after we add an event, to ensure the transition to FREE doesn't miss
      // the added event.  If a page transitions back out of FREE due to actions by other threads,
      // the lock_ protects the page's object field from being overwritten by an event being added.
      vm_page_state state = page->state();
      // If owning_cow, we know the owning_cow destructor can't run, so the only valid page
      // states while FREE or borrowed by a VmCowPages and not pinned are FREE, ALLOC, OBJECT.
      //
      // If !owning_cow, the set of possible states isn't constrained, and we don't try to wait for
      // the page.
      switch (state) {
        case vm_page_state::FREE:
          // No cow, but still success.  The fact that we were holding lock_ while reading page
          // state isn't relevant to the transition to FREE; we just care that we'll notice FREE
          // somewhere in the loop.
          //
          // We care that we will notice transition _to_ FREE that stays FREE indefinitely via this
          // check.  Other threads doing commit/decommit on owning_cow can cause this check to miss
          // a transient FREE state, but we avoid getting stuck waiting indefinitely.
          return ktl::nullopt;
        case vm_page_state::OBJECT: {
          // Sub-cases:
          //  * Using cow.
          //  * Loaning cow.
          //  * No cow (page moving from cow to cow).
          VmCowPages* cow = reinterpret_cast<VmCowPages*>(page->object.get_object());
          if (!cow) {
            if (!owning_cow) {
              // If there's not a specific owning_cow, then we can't be as certain of the states the
              // page may reach.  For example the page may get used by something other than a
              // VmCowPages, which wouldn't trigger the event.  So we can't use the event mechanism.
              //
              // This is a success case.  We checked if there was a using cow at the moment, and
              // there wasn't.
              return ktl::nullopt;
            }
            // Page is moving from cow to cow, and/or is on the way to FREE, so wait below for
            // page to get a new VmCowPages or become FREE.  We still have to synchronize further
            // below using thread_lock, since OBJECT to FREE doesn't hold PageQueues lock_.
            wait_on_stack_ownership = true;
            break;
          } else if (cow == owning_cow) {
            // This should be impossible, since PageSource guarantees that a given page will only be
            // actively reclaimed by up to one thread at a time.  If this happens, things are broken
            // enough that we shouldn't continue.
            panic("Requested page alraedy in owning_cow; unexpected\n");
          } else {
            // At this point the page may have pin_count != 0.  We have to check in terms of which
            // queue here, since we can't acquire the VmCowPages lock (wrong order).
            if (!owning_cow) {
              if (page->object.get_page_queue_ref().load(ktl::memory_order_relaxed) ==
                  PageQueueWired) {
                // A pinned page is not replaceable.
                return ktl::nullopt;
              }
            }
            // There is a using/borrowing cow, but we may not be able to get a ref to it, if it's
            // already destructing.  We actually try to get a ref to the VmCowPagesContainer instead
            // since that allows us to get a ref successfully up until _after_ the page has moved to
            // FREE, avoiding any need to wait, and avoiding stuff needed to support such a wait.
            //
            // We're under PageQueues lock, so this value is stable at the moment, but by the time
            // the caller acquires the cow lock this page could potentially be elsewhere, depending
            // on whether the page is allowed to move to a different VmCowPages or to a different
            // location in this VmCowPages, without going through FREE.
            //
            // The cow->RemovePageForEviction() does a re-check that this page is still at this
            // offset.  The caller's loop takes care of chasing down the page if it moves between
            // VmCowPages or to a different offset in the same VmCowPages without going through
            // FREE.
            uint64_t page_offset = page->object.get_page_offset();
            // We may be racing with destruction of VMO. As we currently hold PageQueues lock we
            // know that our back pointer is correct as the VmCowPages has not yet completed
            // running fbl_recycle() (which has to acquire PageQueues lock to remove the backlink
            // and then release the ref on its VmCowPagesContainer), so we know it is safe to
            // attempt to upgrade from a raw VmCowPagesContainer pointer to a VmCowPagesContainer
            // RefPtr, and that this upgrade will succeed until _after_ the page is FREE.  If
            // upgrading fails we know the page has already become FREE; in that case we just go
            // back around the loop since that's almost as efficient and less code than handling
            // here.
            VmCowPagesContainer* raw_cow_container = cow->raw_container();
            VmoContainerBacklink backlink{fbl::MakeRefPtrUpgradeFromRaw(raw_cow_container, guard),
                                          page, page_offset};
            if (!backlink.cow_container) {
              // Existing cow is at least at the end of fbl_recycle().  The page has already become
              // FREE.  Let the loop handle that since it's less code than handling here and not
              // significantly more expensive to handle with the loop.
              DEBUG_ASSERT(page->is_free());
              continue;
            } else {
              // We AddRef(ed) the using cow_container.  Success.  Return the backlink.  The caller
              // can use this to call cow->RemovePageForEviction(), which is ok to call on a cow
              // with refcount 0 as long as the caller is holding the backlink's cow_container
              // VmCowPagesContainer ref.
              return backlink;
            }
          }
          break;
        }
        case vm_page_state::ALLOC:
          if (!owning_cow) {
            // When there's not an owning_cow, we don't know what use the page may be put to, so
            // we don't know if the page has a StackOwnedLoanedPagesInterval, since those are only
            // required for intervals involving stack ownership of loaned pages.  Since the caller
            // isn't strictly required to succeed at replacing a page when !owning_cow, the caller
            // is ok with a successful "none" here since the page isn't immediately replaceable.
            return ktl::nullopt;
          }
          // Wait for ALLOC to become OBJECT or FREE.
          wait_on_stack_ownership = true;
          break;
        default:
          // If owning_cow, we know the owning_cow destructor can't run, so the only valid page
          // states while FREE or borrowed by a VmCowPages and not pinned are FREE, ALLOC, OBJECT.
          DEBUG_ASSERT(!owning_cow);
          // When !owning_cow, the possible page states include all page states.  The caller is only
          // interested in pages that are both used by a VmCowPages (not transiently stack owned)
          // and which the caller can immediately replace with a different page, so WIRED state goes
          // along with the list of other states where the caller can't just replace the page.
          //
          // There is no cow with this page as an immediately-replaceable page.
          return ktl::nullopt;
      }
    }  // ~guard
    // If we get here, we know that wait_on_stack_ownership is true, and we know that never happens
    // when !owning_cow.
    DEBUG_ASSERT(wait_on_stack_ownership);
    DEBUG_ASSERT(owning_cow);

    StackOwnedLoanedPagesInterval::WaitUntilContiguousPageNotStackOwned(page);

    // At this point, the state of the page has changed, but we don't know how much.  Another thread
    // doing commit on owning_cow may have finished moving the page into owning_cow.  Yet another
    // thread may have decommitted the page again, and yet another thread may be using the loaned
    // page again now despite loan_cancelled having been used.  The page may have been moved to a
    // destination cow, but may now be moving again.  What we do still know is that the page still
    // has owning_cow as its underlying owner (owning_cow is a contiguous VmCowPages), thanks to
    // the ref on owning_cow held by the caller, and how contiguous VmCowPages keep the same
    // physical pages from creation to fbl_recycle().
    //
    // It's still the goal of this method to return the borrowing cow if there is one, or return
    // success without a borrowing cow if the page is verified to be reclaim-able by the owning_cow
    // at some point during this method (regardless of whether that remains true).
    //
    // Go around again to observe new page state.
    //
    // ~thread_lock_guard
  }
}
