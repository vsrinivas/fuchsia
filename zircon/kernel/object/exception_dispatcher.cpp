// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/exception_dispatcher.h>
#include <object/process_dispatcher.h>

#include <fbl/alloc_checker.h>
#include <ktl/move.h>
#include <lib/counters.h>

KCOUNTER(dispatcher_exception_create_count, "dispatcher.exception.create");
KCOUNTER(dispatcher_exception_destroy_count, "dispatcher.exception.destroy");

fbl::RefPtr<ExceptionDispatcher> ExceptionDispatcher::Create(
    fbl::RefPtr<ThreadDispatcher> thread, zx_excp_type_t exception_type,
    const zx_exception_report_t* report, const arch_exception_context_t* arch_context) {
    fbl::AllocChecker ac;
    fbl::RefPtr<ExceptionDispatcher> exception = fbl::AdoptRef(new (&ac) ExceptionDispatcher(
        ktl::move(thread), exception_type, report, arch_context));
    if (!ac.check()) {
        // ExceptionDispatchers are small so if we get to this point a lot of
        // other things will be failing too, but we could potentially pre-
        // allocate space for an ExceptionDispatcher in each thread if we want
        // to eliminate this case.
        return nullptr;
    }

    return exception;
}

ExceptionDispatcher::ExceptionDispatcher(fbl::RefPtr<ThreadDispatcher> thread,
                                         zx_excp_type_t exception_type,
                                         const zx_exception_report_t* report,
                                         const arch_exception_context_t* arch_context)
    : thread_(ktl::move(thread)), exception_type_(exception_type), report_(report),
      arch_context_(arch_context) {
    kcounter_add(dispatcher_exception_create_count, 1);
}

ExceptionDispatcher::~ExceptionDispatcher() {
    kcounter_add(dispatcher_exception_destroy_count, 1);
}

void ExceptionDispatcher::on_zero_handles() {
    // TODO(ZX-3072): trigger |thread_| to continue exception handling.
}
