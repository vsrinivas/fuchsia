// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>
#include <lib/zx/channel.h>
#include <zircon/assert.h>

#include <memory>
#include <set>
#include <stdarg.h>

template <typename Stub, typename Binding, auto vLogger>
class FidlServer {
public:
    using BindingType = Binding;
    using ErrorHandler = fit::function<void(zx_status_t)>;

    // Instances are effectively channel-owned via binding_ and
    // channel_owned_server_.  Any channel error or server-detected protocol
    // error results in deletion of the Stub instance.
    template <typename... Args>
    static void CreateChannelOwned(zx::channel server_request, Args&&... args) {
        auto local_owner = Create(std::forward<Args>(args)...);
        // Make channel-owned / self-owned:
        Stub* stub = local_owner.get();
        stub->channel_owned_server_ = std::move(local_owner);
        stub->SetErrorHandler([stub](zx_status_t status) {
            // A clean close is ZX_ERR_PEER_CLOSED.  The status passed to an
            // error handler is never ZX_OK.
            ZX_DEBUG_ASSERT(status != ZX_OK);

            if (status != ZX_ERR_PEER_CLOSED) {
                // We call FailAsync() just for its logging output (including the
                // "fail" text).  At this point !error_handler, so nothing actually
                // happens async due to this call.
                stub->FailAsync(status, "FidlServer::error_handler_() - status: %d", status);
            }

            // Now delete stub.
            std::unique_ptr<Stub> local_owner =
                std::move(stub->channel_owned_server_);
            // ~local_owner
        });
        stub->Bind(std::move(server_request));
    }

    template <typename... Args>
    static std::unique_ptr<Stub> Create(Args&&... args) {
        return std::unique_ptr<Stub>(new Stub(std::forward<Args>(args)...));
    }

    void SetErrorHandler(ErrorHandler error_handler) {
        binding_.SetErrorHandler(std::move(error_handler));
    }

    // SetErrorHandler() required before Bind().
    void Bind(zx::channel server_request) {
        binding_.Bind(std::move(server_request));
    }

protected:
    FidlServer(async_dispatcher_t* dispatcher, const char* logging_prefix, uint32_t concurrency_cap)
        : dispatcher_(dispatcher),
          binding_(dispatcher_, static_cast<Stub*>(this), &Stub::kOps, concurrency_cap),
          logging_prefix_(logging_prefix) {}

    // This picks up async_get_default_dispatcher(), which seems fine to share
    // with the devhost code, at least for now.
    FidlServer(const char* logging_prefix, uint32_t concurrency_cap)
        : FidlServer(async_get_default_dispatcher(), logging_prefix, concurrency_cap) {}

    ~FidlServer() {
        for (bool* canary : canaries_) {
            *canary = false;
        }
    }

    // Wrapper of async::PostTask() that uses dispatcher_ and abort()s the
    // current process if async::PostTask() fails.  This method does not protect
    // against ~FidlServer running first (see Post() for that).
    void PostUnsafe(fit::closure to_run) {
        zx_status_t post_status = async::PostTask(dispatcher_, std::move(to_run));
        // We don't expect this post to fail.
        ZX_ASSERT(post_status == ZX_OK);
    }

    // In addition to what PostUnsafe() does, Post() avoids running to_run if
    // ~FidlServer has already run.  This does not ensure that any other capture
    // is still allocated at the time to_run runs (that's still the caller's
    // responsibility).
    void Post(fit::closure to_run) {
        // For now we don't optimize away use of the heap here, but we easily
        // could if it became an actual problem.
        auto canary = std::make_unique<bool>(true);
        canaries_.insert(canary.get());
        PostUnsafe([this, canary = std::move(canary), to_run = std::move(to_run)] {
            if (!*canary) {
                // We haven't touched |this|, which is already gone.  Get out.
                return;
            }
            // We now know that |this| is still allocated.  Typically to_run
            // will have also captured this, but not necessarily always.
            canaries_.erase(canary.get());
            to_run();
            // ~to_run
        });
    }

    // Forces the FidlServer to binding_.Close() and run the
    // binding_.error_handler_ async if it hasn't already started running.
    //
    // If |this| is deleted before the error handler runs async, the error
    // handler will cleanly not run and instead will be deleted async without
    // ever being run.
    //
    // FailAsync() is legal to use during the error_handler_, in which case
    // FailAsync() won't cause the error_handler_ to run again.
    //
    // A sub-class is also free to just delete |this| instead of forcing the
    // error handler to run.
    //
    // A sub-class can make FailAsync() public instead of protected, as
    // appropriate.
    void FailAsync(zx_status_t status, const char* format, ...) {
        if (is_failing_) {
            // Fail() is intentionally idempotent.  We only really care about
            // the first failure.
            return;
        }
        is_failing_ = true;

        va_list args;
        va_start(args, format);
        vLogger(true, logging_prefix_, "fail", format, args);
        va_end(args);

        // TODO(dustingreen): Send string in buffer via epitaph, when possible.
        ErrorHandler error_handler = binding_.Close();

        if (error_handler) {
            // The canary in Post() allows us to simulate a channel-triggered
            // async failure while still allowing the owner to delete |this| at
            // any time.  The canary essentially serves the same purpose as the
            // async_cancel_wait() in ~Binding, but we can't cancel a Post() so
            // we use canary instead.
            Post([error_handler = std::move(error_handler), status] {
                // error_handler() will typically ~this
                error_handler(status);
                // |this| is likely gone now.
            });
        }
    }

    // Logging that prefixes logging_prefix + " " + "info"/"error", and doesn't
    // need a trailing '\n' passed in.
    void LogInfo(const char* format, ...) {
        va_list args;
        va_start(args, format);
        vLogger(false, logging_prefix_, "info", format, args);
        va_end(args);
    }
    void LogError(const char* format, ...) {
        va_list args;
        va_start(args, format);
        vLogger(true, logging_prefix_, "error", format, args);
        va_end(args);
    }

    // This is nullptr unless this instance is owned by the channel.
    std::unique_ptr<Stub> channel_owned_server_;

    async_dispatcher_t* dispatcher_ = nullptr;

    bool is_failing_ = false;

    // The binding_.error_handler_ will typically delete |this|.
    Binding binding_;

    const char* logging_prefix_ = nullptr;

private:
    // Any async arc can put a bool* in canaries_. If ~FidlServer runs, the
    // pointed-at canary will be set to false.  The async arc can notice the
    // false value and avoid touching FidlServer (can instead just clean up
    // anything associated with the async arc, such as the canary bool among
    // other things).
    //
    // TODO(dustingreen): Switch to in-band doubly-linked list for canaries.
    std::set<bool*> canaries_;
};
