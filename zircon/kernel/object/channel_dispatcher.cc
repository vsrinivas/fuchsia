// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "object/channel_dispatcher.h"

#include <assert.h>
#include <err.h>
#include <lib/counters.h>
#include <platform.h>
#include <string.h>
#include <trace.h>
#include <zircon/rights.h>
#include <zircon/syscalls/object.h>
#include <zircon/types.h>

#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <kernel/event.h>
#include <object/handle.h>
#include <object/message_packet.h>
#include <object/process_dispatcher.h>
#include <object/thread_dispatcher.h>

#define LOCAL_TRACE 0

KCOUNTER(channel_packet_depth_1, "channel.depth.1")
KCOUNTER(channel_packet_depth_4, "channel.depth.4")
KCOUNTER(channel_packet_depth_16, "channel.depth.16")
KCOUNTER(channel_packet_depth_64, "channel.depth.64")
KCOUNTER(channel_packet_depth_256, "channel.depth.256")
KCOUNTER(channel_packet_depth_unbounded, "channel.depth.unbounded")
KCOUNTER(dispatcher_channel_create_count, "dispatcher.channel.create")
KCOUNTER(dispatcher_channel_destroy_count, "dispatcher.channel.destroy")

// Temporary hack to chase down bugs like fxbug.dev/47000 where upwards of 250MB of ipc
// memory is consumed. The bet is that even if each message is at max size there
// should be one or two channels with thousands of messages. If so, this check adds
// no overhead to the existing code. See fxbug.dev/47691.
// TODO(cpu): This limit can be lower but mojo's ChannelTest.PeerStressTest sends
// about 3K small messages. Switching to size limit is more reasonable.
constexpr size_t kMaxPendingMessageCount = 3500;

// static
zx_status_t ChannelDispatcher::Create(KernelHandle<ChannelDispatcher>* handle0,
                                      KernelHandle<ChannelDispatcher>* handle1,
                                      zx_rights_t* rights) {
  fbl::AllocChecker ac;
  auto holder0 = fbl::AdoptRef(new (&ac) PeerHolder<ChannelDispatcher>());
  if (!ac.check())
    return ZX_ERR_NO_MEMORY;
  auto holder1 = holder0;

  KernelHandle new_handle0(fbl::AdoptRef(new (&ac) ChannelDispatcher(ktl::move(holder0))));
  if (!ac.check())
    return ZX_ERR_NO_MEMORY;

  KernelHandle new_handle1(fbl::AdoptRef(new (&ac) ChannelDispatcher(ktl::move(holder1))));
  if (!ac.check())
    return ZX_ERR_NO_MEMORY;

  new_handle0.dispatcher()->Init(new_handle1.dispatcher());
  new_handle1.dispatcher()->Init(new_handle0.dispatcher());

  *rights = default_rights();
  *handle0 = ktl::move(new_handle0);
  *handle1 = ktl::move(new_handle1);

  return ZX_OK;
}

ChannelDispatcher::ChannelDispatcher(fbl::RefPtr<PeerHolder<ChannelDispatcher>> holder)
    : PeeredDispatcher(ktl::move(holder), ZX_CHANNEL_WRITABLE) {
  kcounter_add(dispatcher_channel_create_count, 1);
}

// This is called before either ChannelDispatcher is accessible from threads other than the one
// initializing the channel, so it does not need locking.
void ChannelDispatcher::Init(fbl::RefPtr<ChannelDispatcher> other) TA_NO_THREAD_SAFETY_ANALYSIS {
  peer_ = ktl::move(other);
  peer_koid_ = peer_->get_koid();
}

ChannelDispatcher::~ChannelDispatcher() {
  kcounter_add(dispatcher_channel_destroy_count, 1);

  // At this point the other endpoint no longer holds
  // a reference to us, so we can be sure we're discarding
  // any remaining messages safely.

  // It's not possible to do this safely in on_zero_handles()

  messages_.clear();

  switch (max_message_count_) {
    case 0 ... 1:
      kcounter_add(channel_packet_depth_1, 1);
      break;
    case 2 ... 4:
      kcounter_add(channel_packet_depth_4, 1);
      break;
    case 5 ... 16:
      kcounter_add(channel_packet_depth_16, 1);
      break;
    case 17 ... 64:
      kcounter_add(channel_packet_depth_64, 1);
      break;
    case 65 ... 256:
      kcounter_add(channel_packet_depth_256, 1);
      break;
    default:
      kcounter_add(channel_packet_depth_unbounded, 1);
      break;
  }
}

void ChannelDispatcher::RemoveWaiter(MessageWaiter* waiter) {
  Guard<Mutex> guard{get_lock()};
  if (!waiter->InContainer()) {
    return;
  }
  waiters_.erase(*waiter);
}

void ChannelDispatcher::on_zero_handles_locked() {
  canary_.Assert();

  // (3A) Abort any waiting Call operations
  // because we've been canceled by reason
  // of our local handle going away.
  // Remove waiter from list.
  while (!waiters_.is_empty()) {
    auto waiter = waiters_.pop_front();
    waiter->Cancel(ZX_ERR_CANCELED);
  }
}

void ChannelDispatcher::set_owner(zx_koid_t new_owner) {
  // Testing for ZX_KOID_INVALID is an optimization so we don't
  // pay the cost of grabbing the lock when the endpoint moves
  // from the process to channel; the one that we must get right
  // is from channel to new owner.
  if (new_owner == ZX_KOID_INVALID)
    return;

  Guard<Mutex> guard{get_lock()};
  owner_ = new_owner;
}

// This requires holding the shared channel lock. The thread analysis
// can reason about repeated calls to get_lock() on the shared object,
// but cannot reason about the aliasing between left->get_lock() and
// right->get_lock(), which occurs above in on_zero_handles.
void ChannelDispatcher::OnPeerZeroHandlesLocked() {
  canary_.Assert();

  UpdateStateLocked(ZX_CHANNEL_WRITABLE, ZX_CHANNEL_PEER_CLOSED);
  // (3B) Abort any waiting Call operations
  // because we've been canceled by reason
  // of the opposing endpoint going away.
  // Remove waiter from list.
  while (!waiters_.is_empty()) {
    auto waiter = waiters_.pop_front();
    waiter->Cancel(ZX_ERR_PEER_CLOSED);
  }
}

zx_status_t ChannelDispatcher::Read(zx_koid_t owner, uint32_t* msg_size, uint32_t* msg_handle_count,
                                    MessagePacketPtr* msg, bool may_discard) {
  canary_.Assert();

  auto max_size = *msg_size;
  auto max_handle_count = *msg_handle_count;

  Guard<Mutex> guard{get_lock()};

  if (owner != owner_)
    return ZX_ERR_BAD_HANDLE;

  if (messages_.is_empty()) {
    return peer_ ? ZX_ERR_SHOULD_WAIT : ZX_ERR_PEER_CLOSED;
  } else if (messages_.size() == kMaxPendingMessageCount / 2) {
    auto process = ProcessDispatcher::GetCurrent();
    char pname[ZX_MAX_NAME_LEN];
    process->get_name(pname);
    printf("KERN: warning! channel (%zu) has %zu messages (%s) (read).\n", get_koid(),
           messages_.size(), pname);
  }

  *msg_size = messages_.front().data_size();
  *msg_handle_count = messages_.front().num_handles();
  zx_status_t rv = ZX_OK;
  if (*msg_size > max_size || *msg_handle_count > max_handle_count) {
    if (!may_discard)
      return ZX_ERR_BUFFER_TOO_SMALL;
    rv = ZX_ERR_BUFFER_TOO_SMALL;
  }

  *msg = messages_.pop_front();

  if (messages_.is_empty())
    UpdateStateLocked(ZX_CHANNEL_READABLE, 0u);

  return rv;
}

zx_status_t ChannelDispatcher::Write(zx_koid_t owner, MessagePacketPtr msg) {
  canary_.Assert();

  AutoReschedDisable resched_disable;  // Must come before the lock guard.
  resched_disable.Disable();
  Guard<Mutex> guard{get_lock()};

  // Failing this test is only possible if this process has two threads racing:
  // one thread is issuing channel_write() and one thread is moving the handle
  // to another process.
  if (owner != owner_)
    return ZX_ERR_BAD_HANDLE;

  if (!peer_)
    return ZX_ERR_PEER_CLOSED;

  AssertHeld(*peer_->get_lock());
  peer_->WriteSelf(ktl::move(msg));

  return ZX_OK;
}

zx_status_t ChannelDispatcher::Call(zx_koid_t owner, MessagePacketPtr msg, zx_time_t deadline,
                                    MessagePacketPtr* reply) {
  canary_.Assert();

  auto waiter = ThreadDispatcher::GetCurrent()->GetMessageWaiter();
  if (unlikely(waiter->BeginWait(fbl::RefPtr(this)) != ZX_OK)) {
    // If a thread tries BeginWait'ing twice, the VDSO contract around retrying
    // channel calls has been violated.  Shoot the misbehaving process.
    ProcessDispatcher::GetCurrent()->Kill(ZX_TASK_RETCODE_VDSO_KILL);
    return ZX_ERR_BAD_STATE;
  }

  {
    AutoReschedDisable resched_disable;  // Must come before the lock guard.
    resched_disable.Disable();
    Guard<Mutex> guard{get_lock()};

    // See Write() for an explanation of this test.
    if (owner != owner_)
      return ZX_ERR_BAD_HANDLE;

    if (!peer_) {
      waiter->EndWait(reply);
      return ZX_ERR_PEER_CLOSED;
    }

    // Obtain a txid.  txid 0 is not allowed, and 1..0x7FFFFFFF are reserved
    // for userspace.  So, bump our counter and OR in the high bit.
  alloc_txid:
    zx_txid_t txid = (++txid_) | 0x80000000;

    // If there are waiting messages, ensure we have not allocated a txid
    // that's already in use.  This is unlikely.  It's atypical for multiple
    // threads to be invoking channel_call() on the same channel at once, so
    // the waiter list is most commonly empty.
    for (auto& waiter : waiters_) {
      if (waiter.get_txid() == txid) {
        goto alloc_txid;
      }
    }

    // Install our txid in the waiter and the outbound message
    waiter->set_txid(txid);
    msg->set_txid(txid);

    // (0) Before writing the outbound message and waiting, add our
    // waiter to the list.
    waiters_.push_back(waiter);

    // (1) Write outbound message to opposing endpoint.
    AssertHeld(*peer_->get_lock());
    peer_->WriteSelf(ktl::move(msg));
  }

  auto process = ProcessDispatcher::GetCurrent();
  const TimerSlack slack = process->GetTimerSlackPolicy();
  const Deadline slackDeadline(deadline, slack);

  // Reuse the code from the half-call used for retrying a Call after thread
  // suspend.
  return ResumeInterruptedCall(waiter, slackDeadline, reply);
}

zx_status_t ChannelDispatcher::ResumeInterruptedCall(MessageWaiter* waiter,
                                                     const Deadline& deadline,
                                                     MessagePacketPtr* reply) {
  canary_.Assert();

  // (2) Wait for notification via waiter's event or for the
  // deadline to hit.
  {
    ThreadDispatcher::AutoBlocked by(ThreadDispatcher::Blocked::CHANNEL);

    zx_status_t status = waiter->Wait(deadline);
    if (status == ZX_ERR_INTERNAL_INTR_RETRY) {
      // If we got interrupted, return out to usermode, but
      // do not clear the waiter.
      return status;
    }
  }

  // (3) see (3A), (3B) above or (3C) below for paths where
  // the waiter could be signaled and removed from the list.
  //
  // If the deadline hits, the waiter is not removed
  // from the list *but* another thread could still
  // cause (3A), (3B), or (3C) before the lock below.
  {
    Guard<Mutex> guard{get_lock()};

    // (4) If any of (3A), (3B), or (3C) have occurred,
    // we were removed from the waiters list already
    // and EndWait() returns a non-ZX_ERR_TIMED_OUT status.
    // Otherwise, the status is ZX_ERR_TIMED_OUT and it
    // is our job to remove the waiter from the list.
    zx_status_t status = waiter->EndWait(reply);
    if (status == ZX_ERR_TIMED_OUT)
      waiters_.erase(*waiter);
    return status;
  }
}

void ChannelDispatcher::WriteSelf(MessagePacketPtr msg) {
  canary_.Assert();

  if (!waiters_.is_empty()) {
    // If the far side is waiting for replies to messages
    // send via "call", see if this message has a matching
    // txid to one of the waiters, and if so, deliver it.
    zx_txid_t txid = msg->get_txid();
    for (auto& waiter : waiters_) {
      // (3C) Deliver message to waiter.
      // Remove waiter from list.
      if (waiter.get_txid() == txid) {
        waiters_.erase(waiter);
        waiter.Deliver(ktl::move(msg));
        return;
      }
    }
  }
  messages_.push_back(ktl::move(msg));
  if (messages_.size() > max_message_count_) {
    max_message_count_ = messages_.size();
  }

  if (messages_.size() == kMaxPendingMessageCount / 2) {
    // TODO(cpu): Remove this hack. See comment in kMaxPendingMessageCount definition.
    auto process = ProcessDispatcher::GetCurrent();
    char pname[ZX_MAX_NAME_LEN];
    process->get_name(pname);
    printf("KERN: warning! channel (%zu) has %zu messages (%s) (write).\n", get_koid(),
           messages_.size(), pname);
  } else if (messages_.size() > kMaxPendingMessageCount) {
    auto process = ProcessDispatcher::GetCurrent();
    char pname[ZX_MAX_NAME_LEN];
    process->get_name(pname);
    printf("KERN: channel (%zu) has %zu messages (%s) (write). Raising exception\n", get_koid(),
           messages_.size(), pname);
    Thread::Current::SignalPolicyException();
  }

  UpdateStateLocked(0u, ZX_CHANNEL_READABLE);
}

zx_status_t ChannelDispatcher::UserSignalSelf(uint32_t clear_mask, uint32_t set_mask) {
  canary_.Assert();
  UpdateStateLocked(clear_mask, set_mask);
  return ZX_OK;
}

ChannelDispatcher::MessageWaiter::~MessageWaiter() {
  if (unlikely(channel_)) {
    channel_->RemoveWaiter(this);
  }
  DEBUG_ASSERT(!InContainer());
}

zx_status_t ChannelDispatcher::MessageWaiter::BeginWait(fbl::RefPtr<ChannelDispatcher> channel) {
  if (unlikely(channel_)) {
    return ZX_ERR_BAD_STATE;
  }
  DEBUG_ASSERT(!InContainer());

  status_ = ZX_ERR_TIMED_OUT;
  channel_ = ktl::move(channel);
  event_.Unsignal();
  return ZX_OK;
}

void ChannelDispatcher::MessageWaiter::Deliver(MessagePacketPtr msg) {
  DEBUG_ASSERT(channel_);

  msg_ = ktl::move(msg);
  status_ = ZX_OK;
  event_.Signal(ZX_OK);
}

void ChannelDispatcher::MessageWaiter::Cancel(zx_status_t status) {
  DEBUG_ASSERT(!InContainer());
  DEBUG_ASSERT(channel_);
  status_ = status;
  event_.Signal(status);
}

zx_status_t ChannelDispatcher::MessageWaiter::Wait(const Deadline& deadline) {
  if (unlikely(!channel_)) {
    return ZX_ERR_BAD_STATE;
  }
  return event_.Wait(deadline);
}

// Returns any delivered message via out and the status.
zx_status_t ChannelDispatcher::MessageWaiter::EndWait(MessagePacketPtr* out) {
  if (unlikely(!channel_)) {
    return ZX_ERR_BAD_STATE;
  }
  *out = ktl::move(msg_);
  channel_ = nullptr;
  return status_;
}
