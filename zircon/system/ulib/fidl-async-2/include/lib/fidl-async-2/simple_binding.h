// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_ASYNC_2_SIMPLE_BINDING_H_
#define LIB_FIDL_ASYNC_2_SIMPLE_BINDING_H_

//#include <ddk/debug.h>
#include <fbl/intrusive_double_list.h>
//#include <lib/async/cpp/task.h>
//#include <lib/async/dispatcher.h>
#include <lib/async/wait.h>
//#include <lib/fidl-utils/bind.h>
#include <lib/fit/defer.h>
#include <lib/fit/function.h>
#include <lib/zx/channel.h>
#include <zircon/fidl.h>
//#include <zircon/syscalls.h>
#include <stdarg.h>
#include <zircon/types.h>
//#include <memory>

// SimpleBinding
//
// This class helps dispatch messages received on a FIDL channel, with the help
// of FIDL generated C code.
//
// This class can tolerate an async_dispatcher_t that uses more than one thread
// to call wait handlers, but this class will only have one wait in progress on
// the channel at a time, and call-outs to client code (Dispatch() and error
// handler) will only be called on one thread at a time in a serial fashion.
// Dispatch calls will occur in the same order as messages are read from the
// channel.
//
// Method calls to this class must be performed on a serial execution domain
// that's the same serial execution domain on which call-outs from this class
// occur.  Typically this will be a single-threaded async_dispatcher_t.  As long
// as calling in on the same serial execution domain, the calls in don't need to
// be during calls out (as in, on the same stack).  Calls in include running the
// destructor.
//
// Once Bind() succeeds, the error handler will run if the channel sees an error
// such as PEER_CLOSED or any other failure.  If on the other hand the client
// code calls Close() first, the error hander will not run.
//
// After the channel fails (can be detected by error_handler_ running) or
// Close() is called directly or via ~SimpleBinding (error_handler_ won't run in
// these cases), the client code is responsible for dropping ownership
// (~unique_ptr<Txn>) on any in-flight requests (fairly quickly).  In this case,
// calling _reply() on those Txn(s) first is optional.
//
// Client code is permitted to ~SimpleBinding and then attempt late responses
// via _reply() on a Txn without any harm.
//
// If the calling code calls Close() or causes ~SimpleBinding, it's the calling
// code's responsibility to shortly after delete any Txn(s) associated with this
// binding.  Calling _reply() on those Txn(s) first is optional, and not
// harmful.
//
// Calling _reply() on a Txn before ~Txn is required unless
// !Binding::is_bound().  In other words, just deleting a Txn without
// replying is only allowed if !is_bound().
//
// |Stub| A Stub* is passed to each function in the Ops table.  This makes Stub*
// usable with Binder<Stub>::BindMember<&Stub::FunctionImplementation> to
// provide the implementation of a function pointer in ops.  This template
// parameter is just to make the "ops_ctx" parameter of Create() typesafe.
// Note that Binder<Stub>::BindOps() is _not_ to be used with this class, as
// bind_fidl() (called by BindOps()) is replaced with Create()+Bind().
//
// |Ops| is the type of the FIDL dispatch table for the FIDL interface of this
// binding.  This will be something like fuchsia_sysmem_InterfaceName_ops.
//
// |Dispatch| is the generated FIDL dispatch function for the interface.  This
// will be something like fuchsia_sysmem_InterfaceName_dispatch.
template <typename Stub, typename Ops, auto Dispatch>
class SimpleBinding {
 public:
  using Binding = SimpleBinding<Stub, Ops, Dispatch>;
  using ErrorHandler = fit::function<void(zx_status_t)>;
  class Txn;
  using TxnPtr = std::unique_ptr<Txn>;

  // A concurrency_cap of std::numeric_limits<uint32_t>::max() is accepted and
  // means unlimited, but an unlimited cap is not recommended.
  SimpleBinding(async_dispatcher_t* dispatcher, Stub* ops_ctx, const Ops* ops,
                uint32_t concurrency_cap)
      : dispatcher_(dispatcher), ops_ctx_(ops_ctx), ops_(ops), concurrency_cap_(concurrency_cap) {
    static_assert(
        std::is_same<decltype(Dispatch), zx_status_t (*)(void*, fidl_txn_t*, fidl_incoming_msg_t*,
                                                         const Ops* ops)>::value,
        "Invalid dispatch function");
    ZX_DEBUG_ASSERT(dispatcher_);
    ZX_DEBUG_ASSERT(ops_ctx_);
    ZX_DEBUG_ASSERT(ops_);
    // A concurrency cap of 0 is invalid, not a special value.
    ZX_DEBUG_ASSERT(concurrency_cap_);
  }

  // Client code that wants to clean up all its in-flight txns immediately can
  // choose to Close() the binding, or can choose to ~SimpleBinding.  Either
  // way, ~Txn is then permitted without having called _reply() on that txn.
  ~SimpleBinding() {
    Close();
    while (!txn_list_.is_empty()) {
      // This way, _reply() on this Txn will fail, but no harm done by the
      // _reply().
      txn_list_.pop_front()->binding_ = nullptr;
    }
    if (binding_is_gone_canary_) {
      // This lets the stack frame calling Dispatch() know that ~Binding
      // ran.
      *binding_is_gone_canary_ = true;
    }
  }

  // Required before Bind().
  void SetErrorHandler(ErrorHandler error_handler) {
    // Once only.
    ZX_DEBUG_ASSERT(error_handler);
    ZX_DEBUG_ASSERT(!error_handler_);
    error_handler_ = std::move(error_handler);
  }

  // SetErrorHandler() is required before Bind().
  void Bind(zx::channel server_channel) {
    // SetErrorHandler() is always required before Bind().
    ZX_DEBUG_ASSERT(error_handler_);
    ZX_DEBUG_ASSERT(server_channel.is_valid());
    ZX_DEBUG_ASSERT(!channel_.is_valid());
    channel_ = std::move(server_channel);
    ZX_DEBUG_ASSERT(is_bound());
    wait_.handler = AsyncWaitHandlerRaw;
    wait_.object = channel_.get();
    wait_.trigger = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED;
    zx_status_t status = async_begin_wait(dispatcher_, &wait_);
    if (status != ZX_OK) {
      RunErrorHandler(status);
      return;
    }
  }

  // Close() is intentionally idempotent.
  ErrorHandler Close() {
    if (!is_bound()) {
      ZX_DEBUG_ASSERT(!error_handler_);
      return nullptr;
    }

    zx_status_t cancel_status = async_cancel_wait(dispatcher_, &wait_);
    // It's fine if async_cancel_wait() returns ZX_ERR_NOT_FOUND, but we
    // don't expect any other errors.
    ZX_DEBUG_ASSERT(cancel_status == ZX_OK || cancel_status == ZX_ERR_NOT_FOUND);
    // Just to keep things tidy - not fundamentally needed.
    wait_.object = ZX_HANDLE_INVALID;
    // Won't be using this any more, so keep things tidy.
    dispatcher_ = nullptr;

    // The error handler will only run if the channel is valid; this
    // prevents the error handler from running if RunErrorHandler() is
    // called later.
    channel_.reset();

    // The caller can run the error_handler if the caller wants to, or the
    // caller can just delete the error handler if the caller prefers the
    // error handler not run when calling Close().
    return std::move(error_handler_);
  }

  bool is_bound() { return channel_.is_valid(); }

  // This class is unique_ptr<>-managed, but there's a trick involved where
  // during _dispatch() we don't know up front if client code will take
  // ownership.  To permit client code to optionally take ownership during
  // _dispatch(), we use a temporary pointer to the unique_ptr<> on the stack
  // of the caller of _dispatch(), which the client code can (indirectly)
  // move to its own unique_ptr<> for async _reply() later.
  class Txn {
    friend Binding;

   public:
    ~Txn() {
      // It's not allowed to just delete an in-flight txn without
      // responding, unless the channel is already !is_bound() or the
      // channel is gone.
      //
      // However, the FIDL C _dispatch() mechanism doesn't seem to offer
      // any way to tell whether a Txn was really needed vs. just ignored
      // by a one-way message.
      //
      // The !binding_ part of the condition allows for:
      //   * deletion of stack-based moved-out Txn instances whose
      //     heap-based replacment (move target) will complete separately.
      //   * deleteion of any Txn instance whose binding_ has already been
      //     deleted - ~Binding() clears binding_.
      //
      // We don't intend for the logical Txn to ever be moved outside the
      // up-to-one move from stack to heap.
      ZX_DEBUG_ASSERT_COND(!is_moved_out_ || !binding_);
      ZX_DEBUG_ASSERT_COND(!(is_moved_out_ && is_moved_in_));
      ZX_DEBUG_ASSERT(!is_recognized_ || is_completed_ || (!binding_ || !binding_->is_bound()));
      if (binding_) {
        binding_->txn_list_.erase(*this);
        // If there's no binding_, it means either Txn was moved out in
        // which case the heap-based Txn will decrement concurrency_, or
        // the Binding has been deleted so doesn't need its concurrency_
        // decremented.
        ZX_DEBUG_ASSERT(binding_->concurrency_ != 0);
        --binding_->concurrency_;
      }
    }

    // Let the dispatcher know that this txn ended up at a handler that takes a txn.
    static void RecognizeTxn(fidl_txn_t* raw_txn) {
      ZX_DEBUG_ASSERT(raw_txn);
      Txn* stack_txn =
          reinterpret_cast<Txn*>(reinterpret_cast<uint8_t*>(raw_txn) - offsetof(Txn, raw_txn_));
      ZX_DEBUG_ASSERT(&stack_txn->raw_txn_ == raw_txn);
      // Shouldn't be moved out yet - recognize should be done extremely
      // near the start of any dispatch method with a fidl_txn_t*
      // parameter.
      ZX_DEBUG_ASSERT_COND(!stack_txn->is_moved_out_);
      // Needs to be a stack-based Txn not a heap-based Txn.
      ZX_DEBUG_ASSERT_COND(!stack_txn->is_moved_in_);
      // RecognizeTxn() is only valid during initial dispatch of this txn,
      // and only valid on stack-based Txn instances not heap-based Txn
      // instances.
      ZX_DEBUG_ASSERT_COND(stack_txn->binding_ && stack_txn->binding_->stack_txn_during_dispatch_ &&
                           stack_txn->binding_->stack_txn_during_dispatch_ == stack_txn);
      // A Txn should only be recognized once, since it's easy enough to
      // just call RecognizeTxn() at the start of every handler that takes
      // a fidl_txn_t* parameter.
      ZX_DEBUG_ASSERT(!stack_txn->is_recognized_);
      // All Txn(s) must be recognized before being completed, to help
      // ensure we're able to detect a (recognized) Txn that's deleted
      // without being completed.
      stack_txn->is_recognized_ = true;
    }

    // Client code will be called via Dispatch().  The client code method
    // will be passed a fidl_txn_t _if_ the message needs a reply.  Because
    // there's no way for Dispatch() to report back whether txn ownership
    // ended up with the client code, TakeTxn() uses a stashed pointer to
    // a unique_ptr<Txn> held by the stack frame calling Dispatch(), to
    // ensure that unless client code explicitly calls TakeTxn(), the frame
    // calling Dispatch() will ~Txn.
    static TxnPtr TakeTxn(fidl_txn_t* raw_txn) {
      ZX_DEBUG_ASSERT(raw_txn);
      Txn* stack_txn =
          reinterpret_cast<Txn*>(reinterpret_cast<uint8_t*>(raw_txn) - offsetof(Txn, raw_txn_));
      ZX_DEBUG_ASSERT(&stack_txn->raw_txn_ == raw_txn);
      // Needs to be a stack-based Txn not a heap-based Txn.
      ZX_DEBUG_ASSERT_COND(!stack_txn->is_moved_in_);
      // TakeTxn() is only valid during initial dispatch of this txn.
      ZX_DEBUG_ASSERT_COND(stack_txn->binding_ && stack_txn->binding_->stack_txn_during_dispatch_ &&
                           stack_txn->binding_->stack_txn_during_dispatch_ == stack_txn);
      // Move the stack-based Txn to the heap, managed by a
      // unique_ptr<Txn>. By allocating on the stack initially, and moving
      // here, we can complete a request sync without any heap allocation.
      // The caller of TakeTxn() wants a heap-based Txn to complete
      // potentially async (sync completion of the Txn returned from this
      // method is still allowed, but is less efficient than completing
      // the stack-based Txn sync by not calling TakeTxn() and just
      // completing the raw_txn instead (only if the caller isn't trying
      // to process the Txn async)).
      return TxnPtr(new Txn(std::move(*stack_txn)));
    }

    // Client code can use this to be able to call the relevant _reply().
    // After return from _reply(), the client code will delete its
    // unique_ptr<Txn>.
    fidl_txn_t& raw_txn() {
      ZX_DEBUG_ASSERT_COND(!is_moved_out_);
      return raw_txn_;
    }

   private:
    // This is used to create on the stack, which is where all logical
    // Txn(s) are initially created.
    Txn(Binding* binding, uint32_t txid)
        : binding_(binding), raw_txn_({.reply = FidlReplyRaw}), txid_(txid) {
      binding_->txn_list_.push_front(this);
    }

    // This move is only meant to be used to move a logical Txn from the
    // stack to the heap up to once.
    //
    // If client code calls TakeTxn(), the logical Txn is moved from the
    // stack to the heap, with the caller of TakeTxn() getting a
    // unique_ptr<Txn> to allow completing the Txn async.  The stack frame
    // running Dispatch() will run ~Txn on the move source (to_move), but
    // that ~Txn doesn't matter - the logical Txn is the move destination.
    //
    // One-way messages to the server don't give the client code a
    // fidl_txn_t*, so for those the stack-based Txn is never moved.  For
    // two-way requests to the server, the client may _reply() synchronously
    // in which case no move ever occurs, or the client may move the Txn
    // from stack to heap and _reply() later using the move target
    // Txn.raw_txn().
    //
    // In all cases, it's permitted to just ~Txn if the binding_ is gone or
    // no longer bound.
    Txn(Txn&& to_move)
        : binding_(to_move.binding_),
          // struct copy
          raw_txn_(to_move.raw_txn_),
          txid_(to_move.txid_),
          is_recognized_(to_move.is_recognized_),
          is_completed_(to_move.is_completed_) {
#if ZX_DEBUG_ASSERT_IMPLEMENTED
      ZX_DEBUG_ASSERT(!to_move.is_moved_out_);
      // The intent of having this move constructor is to allow up to one
      // move from the stack to the heap, not multiple moves.
      ZX_DEBUG_ASSERT(!to_move.is_moved_in_);
      is_moved_in_ = true;
      to_move.is_moved_out_ = true;
#endif
      if (binding_) {
        // The binding_ being non-nullptr means the binding_ still
        // exists and as long as binding_ exists it'll contain all the
        // Txn(s) that were created by incoming messages of the bidning_
        // that haven't been destructed yet.
        ZX_DEBUG_ASSERT(to_move.txn_list_node_state_.InContainer());
        // This un-links to_move from binding_.txn_list_ and links in
        // this instead.
        binding_->txn_list_.replace(to_move, this);
        // This prevents ~Txn from complaining about ~Txn of to_move
        // without to_move.is_completed_.
        to_move.binding_ = nullptr;
      }
      ZX_DEBUG_ASSERT(!to_move.binding_);
      // We leave the rest of the fields of to_move as-is.  The ~to_move
      // (when destructing the stack-based move source) will not assert
      // because to_move.binding_ is nullptr.  If an attempt to .reply()
      // using to_move is made, that'll assert because
      // to_move.is_moved_out_.
    }

    // intentionally move-only
    Txn(const Txn& to_copy) = delete;
    Txn& operator=(const Txn& to_copy) = delete;

    // This function is our fidl_txn_t.reply() function.
    static zx_status_t FidlReplyRaw(fidl_txn_t* raw_txn, const fidl_outgoing_msg_t* msg) {
      Txn* txn =
          reinterpret_cast<Txn*>(reinterpret_cast<uint8_t*>(raw_txn) - offsetof(Txn, raw_txn_));
      ZX_DEBUG_ASSERT(&txn->raw_txn_ == raw_txn);
      return txn->FidlReplyCooked(msg);
    }

    zx_status_t FidlReplyCooked(const fidl_outgoing_msg_t* msg) {
      // Client code must not pass in nullptr.
      ZX_DEBUG_ASSERT(msg);
      // Client code must be sending a message that isn't broken.
      ZX_DEBUG_ASSERT(msg->num_bytes >= sizeof(fidl_message_header_t));
      // To complete a Txn, the Txn must be recognized first.  This helps
      // ensure that all handlers that take a fidl_txn_t remember to
      // recognize their Txn.
      ZX_DEBUG_ASSERT(is_recognized_);
      // The txn being completed must not be a stack-based Txn that has
      // been logically moved to the heap.  Complete the heap-based
      // Txn.raw_txn() instead.
      ZX_DEBUG_ASSERT_COND(!is_moved_out_);
      // Each txn can be completed a maximum of 1 time.
      ZX_DEBUG_ASSERT(!is_completed_);
      // Regardless of what this method does, this method is completing
      // the txn.
      is_completed_ = true;

      // This method ensures that the handles sent in here are closed
      // unless successfully transferred into the channel.
      auto close_handles = fit::defer([msg] {
        zx_status_t close_status = zx_handle_close_many(msg->handles, msg->num_handles);
        ZX_DEBUG_ASSERT(close_status == ZX_OK);
      });

      if (!binding_ || !binding_->is_bound()) {
        // Can't zx_channel_write(), so don't try.  It's legal for
        // client code to _reply() after Close() and/or ~Binding.  The
        // caller may ignore this return value depending on the caller's
        // overall strategy for discovering that the channel has failed.
        return ZX_ERR_BAD_STATE;
      }

      // The caller should ensure this is true.  It's a bug in the caller
      // if not.
      ZX_DEBUG_ASSERT(msg->num_bytes >= sizeof(fidl_message_header_t));
      fidl_message_header_t* hdr = (fidl_message_header_t*)msg->bytes;
      // The caller shouldn't attempt to fill out the txid, as the txid is
      // private to Txn.
      ZX_DEBUG_ASSERT(hdr->txid == 0u);
      // Best-effort attempt to detect double-reply.  Since |this| is soon
      // to be deleted, this is only best-effort.
      ZX_DEBUG_ASSERT(txid_ != 0u);
      hdr->txid = txid_;
      // Part of best-effort attempt to detect double-reply.
      txid_ = 0;
      // zx_channel_write() will close all the handles on any failure, and
      // will transfer them on success.  So this method shouldn't close
      // the handles.
      close_handles.cancel();
      return zx_channel_write(binding_->channel(), 0, msg->bytes, msg->num_bytes, msg->handles,
                              msg->num_handles);
      // ~self_delete
    }

    // This will be set to nullptr during ~Binding, which allows client
    // code to safely attempt _reply() on a txn after ~Binding.  The
    // _reply() will fail in that case, but no harm will be done.  See
    // futher comment on txn_list_node_state_.
    Binding* binding_ = nullptr;
    fidl_txn_t raw_txn_{};
    uint32_t txid_ = 0;
    // Becomes true when _dispatch() calls a handler that actually takes a
    // fidl_txn_t as an input parameter, via that handler calling
    // RecognizeTxn().
    bool is_recognized_ = false;
    bool is_completed_ = false;

#if ZX_DEBUG_ASSERT_IMPLEMENTED
    // This instance has been moved in from another instance.
    bool is_moved_in_ = false;
    // This instance is no longer valid because it was moved out.
    bool is_moved_out_ = false;
#endif

    // Membership in this list allows for binding_ to act similar to a
    // weak_ptr<Binding>, but without forcing use of
    // shared_ptr<Binding>.
    fbl::DoublyLinkedListNodeState<Txn*> txn_list_node_state_;
  };

 private:
  zx_handle_t channel() {
    ZX_DEBUG_ASSERT(channel_.is_valid());
    return channel_.get();
  }

  // In general, deletes |this|.
  void RunErrorHandler(zx_status_t status) {
    // If the channel_ is already !is_valid(), then skip calling the
    // error_handler because this only happens if the client code calls
    // Close() (or ~Binding) before the error, in which case we don't
    // call the error handler.
    if (!is_bound()) {
      return;
    }
    // The error_handler_ is only meant to run up to once.  The client code
    // is expected to always set an error handler before calling Bind().
    ZX_DEBUG_ASSERT(error_handler_);
    // Clean up the channel before calling the error handler, in case the
    // error handling triggers any calls to _reply().
    ErrorHandler error_handler = Close();
    ZX_DEBUG_ASSERT(!is_bound());
    ZX_DEBUG_ASSERT(!error_handler_);
    error_handler(status);
    // in general, |this| is gone now
  }

  // async_wait_handler_t
  static void AsyncWaitHandlerRaw(async_dispatcher_t* dispatcher, async_wait_t* wait,
                                  zx_status_t status, const zx_packet_signal_t* signal) {
    auto binding =
        reinterpret_cast<Binding*>(reinterpret_cast<uint8_t*>(wait) - offsetof(Binding, wait_));
    ZX_DEBUG_ASSERT(&binding->wait_ == wait);
    binding->AsyncWaitHandlerCooked(dispatcher, status, signal);
  }

  void AsyncWaitHandlerCooked(async_dispatcher_t* dispatcher, zx_status_t status,
                              const zx_packet_signal_t* signal) {
    if (status != ZX_OK) {
      goto error;
    }

    // We want to do all the reading before any closing due to peer closed.
    if (signal->observed & ZX_CHANNEL_READABLE) {
      for (uint64_t i = 0; i < signal->count; i++) {
        fidl_incoming_msg_t msg = {
            .bytes = bytes_,
            .handles = handles_,
            .num_bytes = 0u,
            .num_handles = 0u,
        };
        status = zx_channel_read(wait_.object, 0, bytes_, handles_, ZX_CHANNEL_MAX_MSG_BYTES,
                                 ZX_CHANNEL_MAX_MSG_HANDLES, &msg.num_bytes, &msg.num_handles);
        if (status != ZX_OK) {
          goto error;
        }
        if (msg.num_bytes < sizeof(fidl_message_header_t)) {
          status = ZX_ERR_BUFFER_TOO_SMALL;
          goto error;
        }
        fidl_message_header_t* hdr = (fidl_message_header_t*)msg.bytes;

        // The request's txid flows into the future response's txid.
        Txn stack_txn(this, hdr->txid);
        ZX_DEBUG_ASSERT(concurrency_ <= concurrency_cap_);
        ++concurrency_;
        if (concurrency_ > concurrency_cap_) {
          status = ZX_ERR_NO_RESOURCES;
          goto error;
        }

        // If ~Binding is run during Dispatch(), we find out via
        // binding_is_gone_canary.
        bool binding_is_gone_canary = false;
        binding_is_gone_canary_ = &binding_is_gone_canary;
#if ZX_DEBUG_ASSERT_IMPLEMENTED
        stack_txn_during_dispatch_ = &stack_txn;
#endif
        auto cleanup_after_dispatch = fit::defer([this, &binding_is_gone_canary] {
          // Set binding_is_gone_canary_ back to nullptr before
          // binding_is_gone_canary leaves the stack, but only if
          // |this| binding still exists.
          //
          // Same for stack_txn_during_dispatch_.
          if (!binding_is_gone_canary) {
            this->binding_is_gone_canary_ = nullptr;
#if ZX_DEBUG_ASSERT_IMPLEMENTED
            this->stack_txn_during_dispatch_ = nullptr;
#endif
          }
        });

        // The callee methods in Stub are each required to copy out
        // anything needed from msg during this call if the
        // txn.raw_txn().reply() will be called later after this
        // Dispatch() has returned.
        //
        // A Dispatch() that deletes |this| can return ZX_OK or a
        // failure status, so we can't use that to determine whether
        // |this| still exists after Dispatch().
        status = Dispatch(ops_ctx_, &stack_txn.raw_txn(), &msg, ops_);

        // The Dispatch() call is permitted to run ~Binding, so we need
        // to check if |this| is still valid as part of determining
        // whether to wait again.
        if (binding_is_gone_canary) {
          // |this| binding is gone, so just return.
          return;
        }

        // We permit ZX_ERR_ASYNC, but given this dispatching code it's
        // equivalent to ZX_OK.  So we just convert to ZX_OK here.
        //
        // We still permit _dispatch() to return ZX_ERR_ASYNC for
        // convenience and (inbound) compatibility for porting (in)
        // handlers that used to use be called by fidl-async dispatching
        // code, but ... we don't require ZX_ERR_ASYNC.
        if (status == ZX_ERR_ASYNC) {
          status = ZX_OK;
        }
        if (status != ZX_OK) {
          goto error;
        }

        // If the binding still exists but the channel_ was closed
        // using Close(), the client code is responsible for running
        // ~Binding and ~Txn for all associated txns (in any order), so
        // just return without starting another wait.
        if (!is_bound()) {
          return;
        }

        // keep going with the next message, until done reading messages
      }

      // |this| binding still exists, so wait again.  If ~Binding runs
      // later outside of wait completion, ~Binding will cancel the wait.
      status = async_begin_wait(dispatcher, &wait_);
      if (status != ZX_OK) {
        goto error;
      }

      // Now that new wait is started, return.  We intentionally choose to
      // not handle PEER_CLOSED until we're done reading messages.  This
      // choice essentially preserves ordering of send and close (in that
      // order).
      return;
    }

    // We don't notify an error until we've drained all the messages out of
    // the channel.  We run the error handler with ZX_ERR_PEER_CLOSED for
    // consistency with message_reader.cc.
    ZX_DEBUG_ASSERT(signal->observed & ZX_CHANNEL_PEER_CLOSED);
    status = ZX_ERR_PEER_CLOSED;

  error:;
    RunErrorHandler(status);
    return;
  }

  async_dispatcher_t* dispatcher_ = nullptr;
  Stub* ops_ctx_ = nullptr;
  const Ops* ops_ = nullptr;
  const uint32_t concurrency_cap_ = 0;
  uint32_t concurrency_ = 0;
  ErrorHandler error_handler_;
  zx::channel channel_;
  async_wait_t wait_{};

  struct TxnListTraits {
    inline static fbl::DoublyLinkedListNodeState<Txn*>& node_state(Txn& obj) {
      return obj.txn_list_node_state_;
    }
  };
  using TxnList = fbl::DoublyLinkedListCustomTraits<Txn*, TxnListTraits>;
  TxnList txn_list_;

  // Only non-nullptr during Dispatch().  When not nullptr, this points at a
  // canary bool on the stack that's calling Dispatch(), so the stack frame
  // calling Dispatch() can determine whether ~Binding ran during Dispatch().
  bool* binding_is_gone_canary_ = nullptr;

#if ZX_DEBUG_ASSERT_IMPLEMENTED
  Txn* stack_txn_during_dispatch_ = nullptr;
#endif

  // These are a bit large for the stack, so we pre-allocate them as part of
  // the connection.  Only one thread (per connection) is ever actively
  // starting the processing of a server request.  These get overwritten on
  // each zx_channel_read() for this connection.  Note that each _reply()
  // function will still put similarly-sized arrays on the stack.
  char bytes_[ZX_CHANNEL_MAX_MSG_BYTES];
  zx_handle_t handles_[ZX_CHANNEL_MAX_MSG_HANDLES];
};

#endif  // LIB_FIDL_ASYNC_2_SIMPLE_BINDING_H_
