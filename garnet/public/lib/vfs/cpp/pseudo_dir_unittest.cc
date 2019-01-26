// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/vfs/cpp/pseudo_dir.h"
#include "lib/gtest/real_loop_fixture.h"

namespace {

class TestNode : public vfs::Node {
 public:
  TestNode(std::function<void()> death_callback = nullptr)
      : death_callback_(death_callback) {}
  ~TestNode() override {
    if (death_callback_) {
      death_callback_();
    }
  }

 private:
  bool IsDirectory() const override { return false; }

  void Describe(fuchsia::io::NodeInfo* out_info) override {}

  zx_status_t CreateConnection(
      uint32_t flags, std::unique_ptr<vfs::Connection>* connection) override {
    return ZX_ERR_NOT_SUPPORTED;
  }

  std::function<void()> death_callback_;
};

class PseudoDirUnit : public ::testing::Test {
 protected:
  void Init(int number_of_nodes) {
    nodes_.resize(number_of_nodes);
    node_names_.resize(number_of_nodes);

    for (int i = 0; i < number_of_nodes; i++) {
      node_names_[i] = "node" + std::to_string(i);
      nodes_[i] = std::make_shared<TestNode>();
      ASSERT_EQ(ZX_OK, dir_.AddSharedEntry(node_names_[i], nodes_[i]));
    }
  }

  vfs::PseudoDir dir_;
  std::vector<std::string> node_names_;
  std::vector<std::shared_ptr<TestNode>> nodes_;
};

TEST_F(PseudoDirUnit, NotEmpty) {
  Init(1);
  ASSERT_FALSE(dir_.IsEmpty());
}

TEST_F(PseudoDirUnit, Empty) {
  Init(0);
  ASSERT_TRUE(dir_.IsEmpty());
}

TEST_F(PseudoDirUnit, Lookup) {
  Init(10);
  for (int i = 0; i < 10; i++) {
    vfs::Node* n;
    ASSERT_EQ(ZX_OK, dir_.Lookup(node_names_[i], &n))
        << "for " << node_names_[i];
    ASSERT_EQ(nodes_[i].get(), n) << "for " << node_names_[i];
  }
}

TEST_F(PseudoDirUnit, LookupUniqueNode) {
  Init(1);

  auto node = std::make_unique<TestNode>();
  vfs::Node* node_ptr = node.get();
  ASSERT_EQ(ZX_OK, dir_.AddEntry("un", std::move(node)));
  vfs::Node* n;
  ASSERT_EQ(ZX_OK, dir_.Lookup(node_names_[0], &n));
  ASSERT_EQ(nodes_[0].get(), n);

  ASSERT_EQ(ZX_OK, dir_.Lookup("un", &n));
  ASSERT_EQ(node_ptr, n);
}

TEST_F(PseudoDirUnit, InvalidLookup) {
  Init(3);
  vfs::Node* n;
  ASSERT_EQ(ZX_ERR_NOT_FOUND, dir_.Lookup("invalid", &n));
}

TEST_F(PseudoDirUnit, RemoveEntry) {
  Init(5);
  for (int i = 0; i < 5; i++) {
    ASSERT_EQ(2, nodes_[i].use_count());
    ASSERT_EQ(ZX_OK, dir_.RemoveEntry(node_names_[i]))
        << "for " << node_names_[i];

    // cannot access
    vfs::Node* n;
    ASSERT_EQ(ZX_ERR_NOT_FOUND, dir_.Lookup(node_names_[i], &n))
        << "for " << node_names_[i];
    // check that use count went doen by 1
    ASSERT_EQ(1, nodes_[i].use_count());
  }
  ASSERT_TRUE(dir_.IsEmpty());
}

TEST_F(PseudoDirUnit, RemoveUniqueNode) {
  Init(0);

  bool node_died = false;
  auto node = std::make_unique<TestNode>([&]() { node_died = true; });
  EXPECT_FALSE(node_died);
  ASSERT_EQ(ZX_OK, dir_.AddEntry("un", std::move(node)));
  ASSERT_EQ(ZX_OK, dir_.RemoveEntry("un"));
  EXPECT_TRUE(node_died);

  vfs::Node* n;
  ASSERT_EQ(ZX_ERR_NOT_FOUND, dir_.Lookup("un", &n));
}

TEST_F(PseudoDirUnit, RemoveInvalidEntry) {
  Init(5);
  ASSERT_EQ(ZX_ERR_NOT_FOUND, dir_.RemoveEntry("invalid"));

  // make sure nothing was removed
  for (int i = 0; i < 5; i++) {
    vfs::Node* n;
    ASSERT_EQ(ZX_OK, dir_.Lookup(node_names_[i], &n))
        << "for " << node_names_[i];
    ASSERT_EQ(nodes_[i].get(), n) << "for " << node_names_[i];
  }
}

TEST_F(PseudoDirUnit, AddAfterRemove) {
  Init(5);
  ASSERT_EQ(ZX_OK, dir_.RemoveEntry(node_names_[2]));

  auto new_node = std::make_shared<TestNode>();
  ASSERT_EQ(ZX_OK, dir_.AddSharedEntry("new_node", new_node));

  for (int i = 0; i < 5; i++) {
    zx_status_t expected_status = ZX_OK;
    if (i == 2) {
      expected_status = ZX_ERR_NOT_FOUND;
    }
    vfs::Node* n;
    ASSERT_EQ(expected_status, dir_.Lookup(node_names_[i], &n))
        << "for " << node_names_[i];
    if (expected_status == ZX_OK) {
      ASSERT_EQ(nodes_[i].get(), n) << "for " << node_names_[i];
    }
  }

  vfs::Node* n;
  ASSERT_EQ(ZX_OK, dir_.Lookup("new_node", &n));
  ASSERT_EQ(new_node.get(), n);
}

}  // namespace
