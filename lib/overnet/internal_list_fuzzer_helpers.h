// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <assert.h>
#include <list>
#include <vector>
#include "internal_list.h"

namespace overnet {
namespace internal_list {

class Fuzzer {
 public:
  bool PushBack(uint8_t node, uint8_t list) {
    TestNode* n = &nodes_[node];
    switch (list) {
      default:
        return false;
      case 1:
        if (n->in1) return false;
        n->in1 = true;
        list1_.PushBack(n);
        mirror1_.push_back(n);
        break;
      case 2:
        if (n->in2) return false;
        n->in2 = true;
        list2_.PushBack(n);
        mirror2_.push_back(n);
        break;
      case 3:
        if (n->in3) return false;
        n->in3 = true;
        list3_.PushBack(n);
        mirror3_.push_back(n);
        break;
      case 4:
        if (n->in4) return false;
        n->in4 = true;
        list4_.PushBack(n);
        mirror4_.push_back(n);
        break;
      case 5:
        if (n->in5) return false;
        n->in5 = true;
        list5_.PushBack(n);
        mirror5_.push_back(n);
        break;
      case 6:
        if (n->in6) return false;
        n->in6 = true;
        list6_.PushBack(n);
        mirror6_.push_back(n);
        break;
      case 7:
        if (n->in7) return false;
        n->in7 = true;
        list7_.PushBack(n);
        mirror7_.push_back(n);
        break;
    }
    return true;
  }

  bool PushFront(uint8_t node, uint8_t list) {
    TestNode* n = &nodes_[node];
    switch (list) {
      default:
        return false;
      case 1:
        if (n->in1) return false;
        n->in1 = true;
        list1_.PushFront(n);
        mirror1_.push_front(n);
        break;
      case 2:
        if (n->in2) return false;
        n->in2 = true;
        list2_.PushFront(n);
        mirror2_.push_front(n);
        break;
      case 3:
        if (n->in3) return false;
        n->in3 = true;
        list3_.PushFront(n);
        mirror3_.push_front(n);
        break;
      case 4:
        if (n->in4) return false;
        n->in4 = true;
        list4_.PushFront(n);
        mirror4_.push_front(n);
        break;
      case 5:
        if (n->in5) return false;
        n->in5 = true;
        list5_.PushFront(n);
        mirror5_.push_front(n);
        break;
      case 6:
        if (n->in6) return false;
        n->in6 = true;
        list6_.PushFront(n);
        mirror6_.push_front(n);
        break;
      case 7:
        if (n->in7) return false;
        n->in7 = true;
        list7_.PushFront(n);
        mirror7_.push_front(n);
        break;
    }
    return true;
  }

  bool Remove(uint8_t node, uint8_t list) {
    TestNode* n = &nodes_[node];
    switch (list) {
      default:
        return false;
      case 1:
        if (!n->in1) return false;
        n->in1 = false;
        list1_.Remove(n);
        mirror1_.remove(n);
        break;
      case 2:
        if (!n->in2) return false;
        n->in2 = false;
        list2_.Remove(n);
        mirror2_.remove(n);
        break;
      case 3:
        if (!n->in3) return false;
        n->in3 = false;
        list3_.Remove(n);
        mirror3_.remove(n);
        break;
      case 4:
        if (!n->in4) return false;
        n->in4 = false;
        list4_.Remove(n);
        mirror4_.remove(n);
        break;
      case 5:
        if (!n->in5) return false;
        n->in5 = false;
        list5_.Remove(n);
        mirror5_.remove(n);
        break;
      case 6:
        if (!n->in6) return false;
        n->in6 = false;
        list6_.Remove(n);
        mirror6_.remove(n);
        break;
      case 7:
        if (!n->in7) return false;
        n->in7 = false;
        list7_.Remove(n);
        mirror7_.remove(n);
        break;
    }
    return true;
  }

  void Verify() {
    auto f1a = Flatten(list1_);
    auto f1b = Flatten(mirror1_);
    auto f2a = Flatten(list2_);
    auto f2b = Flatten(mirror2_);
    auto f3a = Flatten(list3_);
    auto f3b = Flatten(mirror3_);
    auto f4a = Flatten(list4_);
    auto f4b = Flatten(mirror4_);
    auto f5a = Flatten(list5_);
    auto f5b = Flatten(mirror5_);
    auto f6a = Flatten(list6_);
    auto f6b = Flatten(mirror6_);
    auto f7a = Flatten(list7_);
    auto f7b = Flatten(mirror7_);
    assert(f1a == f1b);
    assert(f2a == f2b);
    assert(f3a == f3b);
    assert(f4a == f4b);
    assert(f5a == f5b);
    assert(f6a == f6b);
    assert(f7a == f7b);
    assert(list1_.Front() == mirror1_.front());
    assert(list2_.Front() == mirror2_.front());
    assert(list3_.Front() == mirror3_.front());
    assert(list4_.Front() == mirror4_.front());
    assert(list5_.Front() == mirror5_.front());
    assert(list6_.Front() == mirror6_.front());
    assert(list7_.Front() == mirror7_.front());
    assert(list1_.Size() == mirror1_.size());
    assert(list2_.Size() == mirror2_.size());
    assert(list3_.Size() == mirror3_.size());
    assert(list4_.Size() == mirror4_.size());
    assert(list5_.Size() == mirror5_.size());
    assert(list6_.Size() == mirror6_.size());
    assert(list7_.Size() == mirror7_.size());
  }

 private:
  template <class T>
  std::vector<uint16_t> Flatten(T& t) {
    std::vector<uint16_t> out;
    for (auto i : t) {
      out.push_back(i - nodes_);
    }
    return out;
  }

  struct TestNode {
    bool in1 = false;
    bool in2 = false;
    bool in3 = false;
    bool in4 = false;
    bool in5 = false;
    bool in6 = false;
    bool in7 = false;
    InternalListNode<TestNode> link1;
    InternalListNode<TestNode> link2;
    InternalListNode<TestNode> link3;
    InternalListNode<TestNode> link4;
    InternalListNode<TestNode> link5;
    InternalListNode<TestNode> link6;
    InternalListNode<TestNode> link7;
  };

  TestNode nodes_[256];
  InternalList<TestNode, &TestNode::link1> list1_;
  InternalList<TestNode, &TestNode::link2> list2_;
  InternalList<TestNode, &TestNode::link3> list3_;
  InternalList<TestNode, &TestNode::link4> list4_;
  InternalList<TestNode, &TestNode::link5> list5_;
  InternalList<TestNode, &TestNode::link6> list6_;
  InternalList<TestNode, &TestNode::link7> list7_;
  std::list<TestNode*> mirror1_;
  std::list<TestNode*> mirror2_;
  std::list<TestNode*> mirror3_;
  std::list<TestNode*> mirror4_;
  std::list<TestNode*> mirror5_;
  std::list<TestNode*> mirror6_;
  std::list<TestNode*> mirror7_;
};

}  // namespace internal_list
}  // namespace overnet
