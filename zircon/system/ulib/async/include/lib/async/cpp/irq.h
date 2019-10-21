// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ASYNC_CPP_irq_H_
#define LIB_ASYNC_CPP_irq_H_

#include <lib/async/irq.h>
#include <lib/fit/function.h>

#include <utility>

namespace async {

// Holds context for an irq and its handler, with RAII semantics.
// Automatically unbinds the irq when it goes out of scope.
//
// This class must only be used with single-threaded asynchronous dispatchers
// and must only be accessed on the dispatch thread since it lacks internal
// synchronization of its state.
//
// Concrete implementations: |async::Irq|, |async::IrqMethod|.
// Please do not create subclasses of IrqBase outside of this library.
class IrqBase {
 protected:
  explicit IrqBase(zx_handle_t object, zx_signals_t trigger, uint32_t options,
                   async_irq_handler_t* handler);
  ~IrqBase();

  IrqBase(const IrqBase&) = delete;
  IrqBase(IrqBase&&) = delete;
  IrqBase& operator=(const IrqBase&) = delete;
  IrqBase& operator=(IrqBase&&) = delete;

 public:
  // Gets or sets the interrupt object.
  zx_handle_t object() const { return irq_.object; }
  void set_object(zx_handle_t object) { irq_.object = object; }

  // Begins asynchronously waiting for the object to receive one or more of
  // the trigger signals.  Invokes the handler when the irq is triggered.
  //
  // Returns |ZX_OK| if the irq was successfully begun.
  // Returns |ZX_ERR_BAD_STATE| if the dispatcher is shutting down.
  // Returns |ZX_ERR_NOT_SUPPORTED| if not supported by the dispatcher.
  zx_status_t Begin(async_dispatcher_t* dispatcher);

  // Cancels the irq.
  //
  // If successful, the irq's handler will not run.
  //
  // Returns |ZX_OK| if the irq was pending and it has been successfully
  // canceled; its handler will not run again and can be released immediately.
  // Returns |ZX_ERR_NOT_FOUND| if there was no pending irq either because it
  // already completed, or had not been started.
  // Returns |ZX_ERR_NOT_SUPPORTED| if not supported by the dispatcher.
  zx_status_t Cancel();

 protected:
  template <typename T>
  static T* Dispatch(async_irq* irq) {
    static_assert(offsetof(IrqBase, irq_) == 0, "");
    auto self = reinterpret_cast<IrqBase*>(irq);
    return static_cast<T*>(self);
  }

 private:
  async_irq_t irq_;
  async_dispatcher_t* dispatcher_ = nullptr;
};

// An asynchronous IRQ whose handler is bound to a |async::irq::Handler| function.
//
// Prefer using |async::IrqMethod| instead for binding to a fixed class member
// function since it is more efficient to dispatch.
class Irq final : public IrqBase {
 public:
  // Handles completion of asynchronous irq operations.
  //
  // The |status| is |ZX_OK| if the irq was satisfied and |signal| is non-null.
  // The |status| is |ZX_ERR_CANCELED| if the dispatcher was shut down before
  // the task's handler ran or the task was canceled.
  using Handler = fit::function<void(async_dispatcher_t* dispatcher, async::Irq* irq,
                                     zx_status_t status, const zx_packet_interrupt_t* interrupt)>;

  // Creates a irq with options == 0.
  explicit Irq(zx_handle_t object = ZX_HANDLE_INVALID, zx_signals_t trigger = ZX_SIGNAL_NONE,
               Handler handler = nullptr)
      : Irq(object, trigger, 0, std::move(handler)) {}

  // Creates a irq with the provided |options|.
  explicit Irq(zx_handle_t object, zx_signals_t trigger, uint32_t options,
               Handler handler = nullptr);

  ~Irq();

  void set_handler(Handler handler) { handler_ = std::move(handler); }
  bool has_handler() const { return !!handler_; }

 private:
  static void CallHandler(async_dispatcher_t* dispatcher, async_irq_t* irq, zx_status_t status,
                          const zx_packet_interrupt_t* signal);

  Handler handler_;
};

// An asynchronous irq whose handler is bound to a fixed class member function.
//
// Usage:
//
// class Foo {
//     void Handle(async_dispatcher_t* dispatcher, async::IrqBase* irq, zx_status_t status,
//                 const zx_packet_interrupt_t* interrupt) { ... }
//     async::IrqMethod<Foo, &Foo::Handle> irq_{this};
// };
template <class Class,
          void (Class::*method)(async_dispatcher_t* dispatcher, async::IrqBase* irq,
                                zx_status_t status, const zx_packet_interrupt_t* interrupt)>
class IrqMethod final : public IrqBase {
 public:
  // Creates a irqMethod with options == 0.
  explicit IrqMethod(Class* instance, zx_handle_t object = ZX_HANDLE_INVALID,
                     zx_signals_t trigger = ZX_SIGNAL_NONE)
      : IrqMethod(instance, object, trigger, 0) {}

  // Creates a IrqMethod with the provided |options|.
  explicit IrqMethod(Class* instance, zx_handle_t object, zx_signals_t trigger, uint32_t options)
      : IrqBase(object, trigger, options, &IrqMethod::CallHandler), instance_(instance) {}

  ~IrqMethod() = default;

 private:
  static void CallHandler(async_dispatcher_t* dispatcher, async_irq_t* irq, zx_status_t status,
                          const zx_packet_interrupt_t* interrupt) {
    auto self = Dispatch<IrqMethod>(irq);
    (self->instance_->*method)(dispatcher, self, status, interrupt);
  }

  Class* const instance_;
};

}  // namespace async

#endif  // LIB_ASYNC_CPP_irq_H_
