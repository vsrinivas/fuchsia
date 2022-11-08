// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/threading_model.h"

#include <lib/async/cpp/task.h>

#include <gtest/gtest.h>

#include "src/media/audio/audio_core/shared/mix_profile_config.h"

namespace media::audio {
namespace {

void ValidateThreadingModel(ThreadingModel* threading_model) {
  // Sanity test we can run task on all loops.
  bool fidl_task_run = false;
  async::PostTask(threading_model->FidlDomain().dispatcher(),
                  [&fidl_task_run] { fidl_task_run = true; });
  bool io_task_run = false;
  async::PostTask(threading_model->IoDomain().dispatcher(), [&io_task_run] { io_task_run = true; });

  // For threading models that use dynamically allocated loops, we submit a task to one loop we
  // immediately release and another to a loop we retain to validate both modes of operation work.
  bool mix1_task_run = false;
  auto mix_domain1 = threading_model->AcquireMixDomain("");
  async::PostTask(mix_domain1->dispatcher(), [&mix1_task_run] { mix1_task_run = true; });

  bool mix2_task_run = false;
  auto mix_domain2 = threading_model->AcquireMixDomain("");
  async::PostTask(mix_domain2->dispatcher(), [&mix2_task_run] { mix2_task_run = true; });

  // We quit first here to cause |RunAndJoinAllThreads| to exit after all currently queued tasks
  // have executed.
  threading_model->Quit();
  threading_model->RunAndJoinAllThreads();

  EXPECT_TRUE(fidl_task_run);
  EXPECT_TRUE(io_task_run);
  EXPECT_TRUE(mix1_task_run);
  EXPECT_TRUE(mix2_task_run);
}

TEST(ThreadingModelTest, MixOnFidlThreadModel) {
  auto threading_model =
      ThreadingModel::CreateWithMixStrategy(MixStrategy::kMixOnFidlThread, MixProfileConfig{});

  // Expect |AcquireMixDomain| to be the same dispatcher as |FidlDomain().dispatcher|.
  {
    auto mix_domain = threading_model->AcquireMixDomain("");
    EXPECT_EQ(threading_model->FidlDomain().dispatcher(), mix_domain->dispatcher());
  }
  // Expect |AcquireMixDomain| to return the same dispatcher across multiple calls.
  {
    auto mix_domain1 = threading_model->AcquireMixDomain("");
    auto mix_domain2 = threading_model->AcquireMixDomain("");
    EXPECT_EQ(mix_domain1->dispatcher(), mix_domain2->dispatcher());
  }

  // |IoDomain().dispatcher| should be different from |FidlDomain().dispatcher|.
  EXPECT_NE(threading_model->FidlDomain().dispatcher(), threading_model->IoDomain().dispatcher());

  ValidateThreadingModel(threading_model.get());
}

TEST(ThreadingModelTest, MixOnSingleThreadModel) {
  auto threading_model =
      ThreadingModel::CreateWithMixStrategy(MixStrategy::kMixOnSingleThread, MixProfileConfig{});

  // Expect all dispatchers to be unique.
  {
    auto mix_domain = threading_model->AcquireMixDomain("");
    EXPECT_NE(threading_model->FidlDomain().dispatcher(), mix_domain->dispatcher());
  }
  {
    auto mix_domain = threading_model->AcquireMixDomain("");
    EXPECT_NE(threading_model->IoDomain().dispatcher(), mix_domain->dispatcher());
  }
  EXPECT_NE(threading_model->FidlDomain().dispatcher(), threading_model->IoDomain().dispatcher());

  // But |AcquireMixDomain| always returns the same dispatcher.
  {
    auto mix_domain1 = threading_model->AcquireMixDomain("");
    auto mix_domain2 = threading_model->AcquireMixDomain("");
    EXPECT_EQ(mix_domain1->dispatcher(), mix_domain2->dispatcher());
  }

  ValidateThreadingModel(threading_model.get());
}

TEST(ThreadingModelTest, ThreadPerMixModel) {
  auto threading_model =
      ThreadingModel::CreateWithMixStrategy(MixStrategy::kThreadPerMix, MixProfileConfig{});

  // Expect all dispatchers to be unique.
  {
    auto mix_domain = threading_model->AcquireMixDomain("");
    EXPECT_NE(threading_model->FidlDomain().dispatcher(), mix_domain->dispatcher());
  }
  {
    auto mix_domain = threading_model->AcquireMixDomain("");
    EXPECT_NE(threading_model->IoDomain().dispatcher(), mix_domain->dispatcher());
  }
  EXPECT_NE(threading_model->FidlDomain().dispatcher(), threading_model->IoDomain().dispatcher());

  // And |AcquireMixDomain| returns different instances
  {
    auto mix_domain1 = threading_model->AcquireMixDomain("");
    auto mix_domain2 = threading_model->AcquireMixDomain("");
    EXPECT_NE(mix_domain1->dispatcher(), mix_domain2->dispatcher());
  }

  ValidateThreadingModel(threading_model.get());
}

}  // namespace
}  // namespace media::audio
