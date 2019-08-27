// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/mediacodec/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/sys/cpp/component_context.h>
#include <stdio.h>

// This test is currently manual because it needs to talk to the main
// CodecFactory which in turn needs to see/open a /dev/class/media-codec/000.

void FailFatal() {
  printf("FAIL\n");
  exit(-1);
}

void PostSerial(async_dispatcher_t* dispatcher, fit::closure to_run) {
  zx_status_t result = async::PostTask(dispatcher, std::move(to_run));
  if (result != ZX_OK) {
    printf("async::PostTask() failed\n");
    FailFatal();
  }
}

void test_factory() {
  // We don't just use Sync FIDL proxies because we might need to receive events
  // before long.

  async::Loop fidl_loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  // Start a separate FIDL thread for two reasons:
  //   * It's handy for the main thread to stay separate to control the test.
  //   * By having a separate FIDL thread, this test shows how to do so without
  //     creating problems.
  fidl_loop.StartThread("FIDL_thread");

  std::unique_ptr<sys::ComponentContext> component_context;
  // Use FIDL thread to run Create(), since it uses the calling
  // thread's default async_t, and we don't want to be accidentally doing
  // FIDL requests from the main thread, so we use
  // kAsyncLoopConfigNoAttachToCurrentThread above.
  PostSerial(fidl_loop.dispatcher(),
             [&component_context] { component_context = sys::ComponentContext::Create(); });

  fuchsia::mediacodec::CodecFactoryPtr codec_factory;
  codec_factory.set_error_handler([](zx_status_t error) {
    printf("codec_factory failed with error %d\n", error);
    FailFatal();
  });

  // It appears ConnectToEnvironmentService() is probably currently safe to call
  // from the main thread, but if it moves to use FIDL libs instead, then it
  // won't be any longer, so call from FIDL thread instead.
  PostSerial(
      fidl_loop.dispatcher(),
      [&component_context, request = codec_factory.NewRequest(fidl_loop.dispatcher())]() mutable {
        component_context->svc()->Connect<fuchsia::mediacodec::CodecFactory>(std::move(request));
      });

  fuchsia::media::StreamProcessorPtr codec;
  codec.set_error_handler([](zx_status_t error) {
    printf(
        "codec failed with error %d (for now this is normal if not running "
        "this on VIM2)\n",
        error);
    FailFatal();
  });
  // Use FIDL thread to send request for Codec.
  PostSerial(
      fidl_loop.dispatcher(),
      [&codec_factory, request = codec.NewRequest(fidl_loop.dispatcher()),
       params = fuchsia::mediacodec::CreateDecoder_Params{
           .input_details.format_details_version_ordinal = 0,
           .input_details.mime_type = "video/h264",
           .promise_separate_access_units_on_input = true,
           .require_hw = true,
       }]() mutable { codec_factory->CreateDecoder(std::move(params), std::move(request)); });

  // Use FIDL thread to check that codec can communicate to the driver
  // round-trip.  The other-thread usage is a bit unnatural here, but we want to
  // keep the test sequencing on a thread of its own, and the current thread is
  // that thread.
  std::mutex is_sync_done_lock;
  bool is_sync_done = false;
  std::condition_variable is_sync_done_condition;
  PostSerial(fidl_loop.dispatcher(),
             [&codec, &is_sync_done_lock, &is_sync_done, &is_sync_done_condition] {
               codec->Sync([&is_sync_done_lock, &is_sync_done, &is_sync_done_condition] {
                 printf("codec->Sync() completing (FIDL thread)\n");
                 {  // scope lock
                   std::unique_lock<std::mutex> lock(is_sync_done_lock);
                   is_sync_done = true;
                 }  // ~lock
                 is_sync_done_condition.notify_all();
               });
             });

  // Wait for Sync() to be done, or a channel to fail (in which case the error
  // handler(s) will exit(-1) and fail the test).
  {  // scope lock
    std::unique_lock<std::mutex> lock(is_sync_done_lock);
    while (!is_sync_done) {
      is_sync_done_condition.wait_for(lock, std::chrono::seconds(10));
      if (!is_sync_done) {
        printf("still waiting for codec->Sync() to be done.\n");
      }
    }
  }  // ~lock

  printf("main thread knows codec->Sync() completed - cleaning up\n");

  // To avoid the hassle of needing to switch to the FIDL thread to un-bind
  // safely, we can use the other workable way to un-bind from a different
  // thread, which is to stop the FIDL thread first.
  fidl_loop.Quit();
  fidl_loop.JoinThreads();

  // ~codec - unbinds
  // ~codec_factory - unbinds
  // ~fidl_loop - does Shutdown()
}

void usage(const char* prog_name) { printf("usage: %s\n", prog_name); }

int main(int argc, char* argv[]) {
  if (argc != 1) {
    usage(argv[0]);
    FailFatal();
  }

  test_factory();

  // PASS
  printf("PASS\n");
  // No destructors run after printing PASS.
  return 0;
}
