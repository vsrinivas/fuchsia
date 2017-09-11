// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <chrono>
#include <queue>
#include <vulkan/vulkan.hpp>

#include "escher/impl/vk/pipeline_cache.h"
#include "escher/impl/vk/pipeline_factory.h"
#include "gtest/gtest.h"
#include "lib/fxl/logging.h"

namespace escher {
namespace impl {
namespace {

class TestPipelineFactory : public PipelineFactory {
 public:
  TestPipelineFactory() {}

  // Enqueue a request, but don't immediately create a new Pipeline.
  std::future<PipelinePtr> NewPipeline(PipelineSpec spec) override;

  // Service one of the queued requests.
  void ServiceOneRequest();

 private:
  struct Request {
    PipelineSpec spec;
    std::promise<PipelinePtr> promise;
  };

  std::queue<Request> requests_;
  std::mutex mutex_;
};

std::future<PipelinePtr> TestPipelineFactory::NewPipeline(PipelineSpec spec) {
  std::lock_guard<std::mutex> lock(mutex_);
  Request request = {spec, std::promise<PipelinePtr>()};
  auto future = request.promise.get_future();
  requests_.push(std::move(request));
  return future;
}

void TestPipelineFactory::ServiceOneRequest() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (requests_.empty())
    return;

  Request request = std::move(requests_.front());
  requests_.pop();

  VkPipelineLayout fake_layout;
  VkPipeline fake_pipeline;

  reinterpret_cast<uint32_t*>(&fake_layout)[0] = 1U;
  reinterpret_cast<uint32_t*>(&fake_pipeline)[0] = 1U;

  auto layout = fxl::MakeRefCounted<PipelineLayout>(nullptr, fake_layout);
  auto pipeline = fxl::MakeRefCounted<Pipeline>(nullptr, fake_pipeline, layout,
                                                request.spec);
  request.promise.set_value(pipeline);
}

// This test is disabled because it hangs on arm64.
// TODO(ES-34): un-disable when possible.
TEST(PipelineCache, DISABLED_SuperComprehensive) {
  PipelineCache cache;
  auto factory = fxl::MakeRefCounted<TestPipelineFactory>();

  PipelineSpec spec1(1, {});
  PipelineSpec spec2(1, {1, 2, 3});
  PipelineSpec spec3(2, {1, 2, 3});

  auto req1_1 = cache.GetPipeline(spec1, factory);
  auto req1_2 = cache.GetPipeline(spec1, factory);

  // Servicing one factory request resolves both cache requests for spec1, but
  // none of the cache/factory requests for spec2/spec3.
  std::chrono::milliseconds msecs(10);
  EXPECT_EQ(std::future_status::timeout, req1_1.wait_for(msecs));
  EXPECT_EQ(std::future_status::timeout, req1_2.wait_for(msecs));
  factory->ServiceOneRequest();

  auto req2_1 = cache.GetPipeline(spec2, factory);
  auto req2_2 = cache.GetPipeline(spec2, factory);
  auto req3_1 = cache.GetPipeline(spec3, factory);
  auto req3_2 = cache.GetPipeline(spec3, factory);
  EXPECT_EQ(std::future_status::ready, req1_1.wait_for(msecs));
  EXPECT_EQ(std::future_status::ready, req1_2.wait_for(msecs));
  EXPECT_EQ(std::future_status::timeout, req2_1.wait_for(msecs));
  EXPECT_EQ(std::future_status::timeout, req2_2.wait_for(msecs));

  // Servicing two more factory requests resolves the outstanding cache
  // requests.
  factory->ServiceOneRequest();
  factory->ServiceOneRequest();
  EXPECT_EQ(std::future_status::ready, req2_1.wait_for(msecs));
  EXPECT_EQ(std::future_status::ready, req2_2.wait_for(msecs));
  EXPECT_EQ(std::future_status::ready, req3_1.wait_for(msecs));
  EXPECT_EQ(std::future_status::ready, req3_2.wait_for(msecs));

  // Get the PipelinePtrs, and sanity-check them.
  auto p1_1 = req1_1.get();
  auto p1_2 = req1_2.get();
  auto p2_1 = req2_1.get();
  auto p2_2 = req2_2.get();
  auto p3_2 = req3_2.get();
  auto p3_1 = req3_1.get();
  EXPECT_EQ(p1_1, p1_2);
  EXPECT_EQ(p2_1, p2_2);
  EXPECT_EQ(p3_1, p3_2);
  EXPECT_NE(p1_1, p2_1);
  EXPECT_NE(p2_1, p3_1);
  EXPECT_EQ(spec1, p1_1->spec());
  EXPECT_EQ(spec2, p2_1->spec());
  EXPECT_EQ(spec3, p3_1->spec());
  EXPECT_NE(p1_1->spec(), p2_1->spec());
  EXPECT_NE(p2_1->spec(), p3_1->spec());
}

}  // namespace
}  // namespace impl
}  // namespace escher
