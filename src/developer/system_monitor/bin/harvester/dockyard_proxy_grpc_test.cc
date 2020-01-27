// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dockyard_proxy_grpc.h"

#include <utility>

#include "gtest/gtest.h"

class DockyardProxyGrpcTest : public ::testing::Test {
 public:
  void SetUp() {}
};

TEST_F(DockyardProxyGrpcTest, ExtractPathsFromSampleList) {
  harvester::SampleList in = {
    {"path1", 0UL},
    {"path2", 19UL},
    {"path1", 42UL},
  };
  std::vector<const std::string*> out(in.size());

  harvester::internal::ExtractPathsFromSampleList(&out, in);

  EXPECT_EQ(*out[0], "path1");
  EXPECT_EQ(*out[1], "path2");
  EXPECT_EQ(*out[2], "path1");
}

TEST_F(DockyardProxyGrpcTest, BuildSampleListById) {
  std::vector<dockyard::DockyardId> id_list = {13, 8, 13};
  harvester::SampleList sample_list = {
    {"path1", 0UL},
    {"path2", 19UL},
    {"path1", 42UL},
  };
  harvester::SampleListById out(id_list.size());

  harvester::internal::BuildSampleListById(&out, id_list, sample_list);

  EXPECT_EQ(out[0], std::make_pair(13UL, 0UL));
  EXPECT_EQ(out[1], std::make_pair(8UL, 19UL));
  EXPECT_EQ(out[2], std::make_pair(13UL, 42UL));
}
