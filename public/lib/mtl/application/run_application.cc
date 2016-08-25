// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/public/cpp/application/run_application.h"

#include <pthread.h>

#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"
#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/system/macros.h"
#include "mojo/public/cpp/system/message_pipe.h"
#include "mojo/public/interfaces/application/application.mojom.h"

namespace mojo {
namespace {

// We store a pointer to a |ResultHolder|, which just stores a |MojoResult|, in
// TLS so that |TerminateApplication()| can provide the result that
// |RunApplication()| will return. (The |ResultHolder| is just on
// |RunApplication()|'s stack.)
struct ResultHolder {
#ifndef NDEBUG
  bool is_set = false;
#endif
  // TODO(vtl): The default result should probably be
  // |MOJO_SYSTEM_RESULT_UNKNOWN|, but |ApplicationRunner| always returned
  // |MOJO_RESULT_OK|.
  MojoResult result = MOJO_RESULT_OK;
};

pthread_key_t g_current_result_holder_key;

// Ensures that we have a TLS slot to store the current result in.
void InitializeCurrentResultHolderIfNecessary() {
  static pthread_once_t current_result_holder_key_once = PTHREAD_ONCE_INIT;
  int error = pthread_once(&current_result_holder_key_once, []() {
    int error = pthread_key_create(&g_current_result_holder_key, nullptr);
    MOJO_ALLOW_UNUSED_LOCAL(error);
    FTL_DCHECK(!error);
  });
  MOJO_ALLOW_UNUSED_LOCAL(error);
  FTL_DCHECK(!error);
}

ResultHolder* GetCurrentResultHolder() {
  InitializeCurrentResultHolderIfNecessary();
  return static_cast<ResultHolder*>(
      pthread_getspecific(g_current_result_holder_key));
}

void SetCurrentResultHolder(ResultHolder* result_holder) {
  InitializeCurrentResultHolderIfNecessary();

  int error = pthread_setspecific(g_current_result_holder_key, result_holder);
  MOJO_ALLOW_UNUSED_LOCAL(error);
  FTL_DCHECK(!error);
}

}  // namespace

MojoResult RunApplication(MojoHandle application_request_handle,
                          ApplicationImplBase* application_impl,
                          const RunApplicationOptions* options) {
  FTL_DCHECK(!options);  // No options supported!
  FTL_DCHECK(!GetCurrentResultHolder());

  ResultHolder result_holder;
  SetCurrentResultHolder(&result_holder);

  mtl::MessageLoop message_loop;
  application_impl->Bind(InterfaceRequest<Application>(
      MakeScopedHandle(MessagePipeHandle(application_request_handle))));
  message_loop.Run();

  // TODO(vtl): Should we unbind stuff here? (Should there be "will start"/"did
  // stop" notifications to the |ApplicationImplBase|?)

  SetCurrentResultHolder(nullptr);

  // TODO(vtl): We'd like to enable the following assertion, but we do things
  // like |RunLoop::current()->Quit()| in various places.
  // assert(result_holder.is_set);

  return result_holder.result;
}

void TerminateApplication(MojoResult result) {
  mtl::MessageLoop::GetCurrent()->QuitNow();

  ResultHolder* result_holder = GetCurrentResultHolder();
  // TODO(vtl): We may execute code during |MessageLoop|'s destruction (in
  // particular, "handlers" are notified of destruction, and those handlers may
  // call this). There's no |is_running()| method (unlike |base::MessageLoop|),
  // so we detect this case by checking if there's a current result holder. Ugh.
  if (!result_holder)
    return;
  result_holder->result = result;
#ifndef NDEBUG
  FTL_DCHECK(!result_holder->is_set);
  result_holder->is_set = true;
#endif
}

}  // namespace mojo
