// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Derived from chromium/src/base/observer_list_unittest.cc

#include "src/ledger/lib/callback/observer_list.h"

#include <utility>
#include <vector>

#include "gtest/gtest.h"

namespace ledger {

namespace {

class Foo {
 public:
  virtual void Observe(int x) = 0;
  virtual ~Foo() {}
  virtual int GetValue() const { return 0; }
};

class Adder : public Foo {
 public:
  explicit Adder(int scaler) : total(0), scaler_(scaler) {}
  ~Adder() override {}

  void Observe(int x) override { total += x * scaler_; }
  int GetValue() const override { return total; }

  int total;

 private:
  int scaler_;
};

class Disrupter : public Foo {
 public:
  Disrupter(ObserverList<Foo>* list, Foo* doomed, bool remove_self)
      : list_(list), doomed_(doomed), remove_self_(remove_self) {}
  Disrupter(ObserverList<Foo>* list, Foo* doomed) : Disrupter(list, doomed, false) {}
  Disrupter(ObserverList<Foo>* list, bool remove_self) : Disrupter(list, nullptr, remove_self) {}

  ~Disrupter() override {}

  void Observe(int x) override {
    if (remove_self_)
      list_->RemoveObserver(this);
    if (doomed_)
      list_->RemoveObserver(doomed_);
  }

  void SetDoomed(Foo* doomed) { doomed_ = doomed; }

 private:
  ObserverList<Foo>* list_;
  Foo* doomed_;
  bool remove_self_;
};

template <typename ObserverListType>
class AddInObserve : public Foo {
 public:
  explicit AddInObserve(ObserverListType* observer_list)
      : observer_list(observer_list), to_add_() {}

  void SetToAdd(Foo* to_add) { to_add_ = to_add; }

  void Observe(int x) override {
    if (to_add_) {
      observer_list->AddObserver(to_add_);
      to_add_ = nullptr;
    }
  }

  ObserverListType* observer_list;
  Foo* to_add_;
};

class AddInClearObserve : public Foo {
 public:
  explicit AddInClearObserve(ObserverList<Foo>* list) : list_(list), added_(false), adder_(1) {}

  void Observe(int /* x */) override {
    list_->Clear();
    list_->AddObserver(&adder_);
    added_ = true;
  }

  bool added() const { return added_; }
  const Adder& adder() const { return adder_; }

 private:
  ObserverList<Foo>* const list_;

  bool added_;
  Adder adder_;
};

class ListDestructor : public Foo {
 public:
  explicit ListDestructor(ObserverList<Foo>* list) : list_(list) {}
  ~ListDestructor() override {}

  void Observe(int x) override { delete list_; }

 private:
  ObserverList<Foo>* list_;
};

TEST(ObserverListTest, BasicTest) {
  ObserverList<Foo> observer_list;
  Adder a(1), b(-1), c(1), d(-1), e(-1);
  Disrupter evil(&observer_list, &c);

  observer_list.AddObserver(&a);
  observer_list.AddObserver(&b);

  EXPECT_TRUE(observer_list.HasObserver(&a));
  EXPECT_FALSE(observer_list.HasObserver(&c));

  for (auto& observer : observer_list)
    observer.Observe(10);

  observer_list.AddObserver(&evil);
  observer_list.AddObserver(&c);
  observer_list.AddObserver(&d);

  // Removing an observer not in the list should do nothing.
  observer_list.RemoveObserver(&e);

  for (auto& observer : observer_list)
    observer.Observe(10);

  EXPECT_EQ(20, a.total);
  EXPECT_EQ(-20, b.total);
  EXPECT_EQ(0, c.total);
  EXPECT_EQ(-10, d.total);
  EXPECT_EQ(0, e.total);
}

TEST(ObserverListTest, DisruptSelf) {
  ObserverList<Foo> observer_list;
  Adder a(1), b(-1), c(1), d(-1);
  Disrupter evil(&observer_list, true);

  observer_list.AddObserver(&a);
  observer_list.AddObserver(&b);

  for (auto& observer : observer_list)
    observer.Observe(10);

  observer_list.AddObserver(&evil);
  observer_list.AddObserver(&c);
  observer_list.AddObserver(&d);

  for (auto& observer : observer_list)
    observer.Observe(10);

  EXPECT_EQ(20, a.total);
  EXPECT_EQ(-20, b.total);
  EXPECT_EQ(10, c.total);
  EXPECT_EQ(-10, d.total);
}

TEST(ObserverListTest, DisruptBefore) {
  ObserverList<Foo> observer_list;
  Adder a(1), b(-1), c(1), d(-1);
  Disrupter evil(&observer_list, &b);

  observer_list.AddObserver(&a);
  observer_list.AddObserver(&b);
  observer_list.AddObserver(&evil);
  observer_list.AddObserver(&c);
  observer_list.AddObserver(&d);

  for (auto& observer : observer_list)
    observer.Observe(10);
  for (auto& observer : observer_list)
    observer.Observe(10);

  EXPECT_EQ(20, a.total);
  EXPECT_EQ(-10, b.total);
  EXPECT_EQ(20, c.total);
  EXPECT_EQ(-20, d.total);
}

TEST(ObserverListTest, Existing) {
  ObserverList<Foo> observer_list(ObserverList<Foo>::NotifyWhat::kExistingOnly);
  Adder a(1);
  AddInObserve<ObserverList<Foo>> b(&observer_list);
  Adder c(1);
  b.SetToAdd(&c);

  observer_list.AddObserver(&a);
  observer_list.AddObserver(&b);

  for (auto& observer : observer_list)
    observer.Observe(1);

  EXPECT_FALSE(b.to_add_);
  // B's adder should not have been notified because it was added during
  // notification.
  EXPECT_EQ(0, c.total);

  // Notify again to make sure b's adder is notified.
  for (auto& observer : observer_list)
    observer.Observe(1);
  EXPECT_EQ(1, c.total);
}

TEST(ObserverListTest, ClearNotifyAll) {
  ObserverList<Foo> observer_list;
  AddInClearObserve a(&observer_list);

  observer_list.AddObserver(&a);

  for (auto& observer : observer_list)
    observer.Observe(1);
  EXPECT_TRUE(a.added());
  EXPECT_EQ(1, a.adder().total) << "Adder should observe once and have sum of 1.";
}

TEST(ObserverListTest, ClearNotifyExistingOnly) {
  ObserverList<Foo> observer_list(ObserverList<Foo>::NotifyWhat::kExistingOnly);
  AddInClearObserve a(&observer_list);

  observer_list.AddObserver(&a);

  for (auto& observer : observer_list)
    observer.Observe(1);
  EXPECT_TRUE(a.added());
  EXPECT_EQ(0, a.adder().total) << "Adder should not observe, so sum should still be 0.";
}

TEST(ObserverListTest, IteratorOutlivesList) {
  ObserverList<Foo>* observer_list = new ObserverList<Foo>;
  ListDestructor a(observer_list);
  observer_list->AddObserver(&a);

  for (auto& observer : *observer_list)
    observer.Observe(0);
  // If this test fails, there'll be Valgrind errors when this function goes out
  // of scope.
}

TEST(ObserverListTest, BasicStdIterator) {
  using FooList = ObserverList<Foo>;
  FooList observer_list;

  // An optimization: begin() and end() do not involve weak pointers on
  // empty list.
  EXPECT_FALSE(observer_list.begin().GetContainer());
  EXPECT_FALSE(observer_list.end().GetContainer());

  // Iterate over empty list: no effect, no crash.
  for (auto& i : observer_list)
    i.Observe(10);

  Adder a(1), b(-1), c(1), d(-1);

  observer_list.AddObserver(&a);
  observer_list.AddObserver(&b);
  observer_list.AddObserver(&c);
  observer_list.AddObserver(&d);

  for (FooList::iterator i = observer_list.begin(), e = observer_list.end(); i != e; ++i)
    i->Observe(1);

  EXPECT_EQ(1, a.total);
  EXPECT_EQ(-1, b.total);
  EXPECT_EQ(1, c.total);
  EXPECT_EQ(-1, d.total);

  // Check an iteration over a 'const view' for a given container.
  const FooList& const_list = observer_list;
  for (FooList::const_iterator i = const_list.begin(), e = const_list.end(); i != e; ++i) {
    EXPECT_EQ(1, std::abs(i->GetValue()));
  }

  for (const auto& o : const_list)
    EXPECT_EQ(1, std::abs(o.GetValue()));
}

TEST(ObserverListTest, StdIteratorRemoveItself) {
  ObserverList<Foo> observer_list;
  Adder a(1), b(-1), c(1), d(-1);
  Disrupter disrupter(&observer_list, true);

  observer_list.AddObserver(&a);
  observer_list.AddObserver(&b);
  observer_list.AddObserver(&disrupter);
  observer_list.AddObserver(&c);
  observer_list.AddObserver(&d);

  for (auto& o : observer_list)
    o.Observe(1);

  for (auto& o : observer_list)
    o.Observe(10);

  EXPECT_EQ(11, a.total);
  EXPECT_EQ(-11, b.total);
  EXPECT_EQ(11, c.total);
  EXPECT_EQ(-11, d.total);
}

TEST(ObserverListTest, StdIteratorRemoveBefore) {
  ObserverList<Foo> observer_list;
  Adder a(1), b(-1), c(1), d(-1);
  Disrupter disrupter(&observer_list, &b);

  observer_list.AddObserver(&a);
  observer_list.AddObserver(&b);
  observer_list.AddObserver(&disrupter);
  observer_list.AddObserver(&c);
  observer_list.AddObserver(&d);

  for (auto& o : observer_list)
    o.Observe(1);

  for (auto& o : observer_list)
    o.Observe(10);

  EXPECT_EQ(11, a.total);
  EXPECT_EQ(-1, b.total);
  EXPECT_EQ(11, c.total);
  EXPECT_EQ(-11, d.total);
}

TEST(ObserverListTest, StdIteratorRemoveAfter) {
  ObserverList<Foo> observer_list;
  Adder a(1), b(-1), c(1), d(-1);
  Disrupter disrupter(&observer_list, &c);

  observer_list.AddObserver(&a);
  observer_list.AddObserver(&b);
  observer_list.AddObserver(&disrupter);
  observer_list.AddObserver(&c);
  observer_list.AddObserver(&d);

  for (auto& o : observer_list)
    o.Observe(1);

  for (auto& o : observer_list)
    o.Observe(10);

  EXPECT_EQ(11, a.total);
  EXPECT_EQ(-11, b.total);
  EXPECT_EQ(0, c.total);
  EXPECT_EQ(-11, d.total);
}

TEST(ObserverListTest, StdIteratorRemoveAfterFront) {
  ObserverList<Foo> observer_list;
  Adder a(1), b(-1), c(1), d(-1);
  Disrupter disrupter(&observer_list, &a);

  observer_list.AddObserver(&a);
  observer_list.AddObserver(&disrupter);
  observer_list.AddObserver(&b);
  observer_list.AddObserver(&c);
  observer_list.AddObserver(&d);

  for (auto& o : observer_list)
    o.Observe(1);

  for (auto& o : observer_list)
    o.Observe(10);

  EXPECT_EQ(1, a.total);
  EXPECT_EQ(-11, b.total);
  EXPECT_EQ(11, c.total);
  EXPECT_EQ(-11, d.total);
}

TEST(ObserverListTest, StdIteratorRemoveBeforeBack) {
  ObserverList<Foo> observer_list;
  Adder a(1), b(-1), c(1), d(-1);
  Disrupter disrupter(&observer_list, &d);

  observer_list.AddObserver(&a);
  observer_list.AddObserver(&b);
  observer_list.AddObserver(&c);
  observer_list.AddObserver(&disrupter);
  observer_list.AddObserver(&d);

  for (auto& o : observer_list)
    o.Observe(1);

  for (auto& o : observer_list)
    o.Observe(10);

  EXPECT_EQ(11, a.total);
  EXPECT_EQ(-11, b.total);
  EXPECT_EQ(11, c.total);
  EXPECT_EQ(0, d.total);
}

TEST(ObserverListTest, StdIteratorRemoveFront) {
  using FooList = ObserverList<Foo>;
  FooList observer_list;
  Adder a(1), b(-1), c(1), d(-1);
  Disrupter disrupter(&observer_list, true);

  observer_list.AddObserver(&disrupter);
  observer_list.AddObserver(&a);
  observer_list.AddObserver(&b);
  observer_list.AddObserver(&c);
  observer_list.AddObserver(&d);

  bool test_disruptor = true;
  for (FooList::iterator i = observer_list.begin(), e = observer_list.end(); i != e; ++i) {
    i->Observe(1);
    // Check that second call to i->Observe() would crash here.
    if (test_disruptor) {
      EXPECT_FALSE(i.GetCurrent());
      test_disruptor = false;
    }
  }

  for (auto& o : observer_list)
    o.Observe(10);

  EXPECT_EQ(11, a.total);
  EXPECT_EQ(-11, b.total);
  EXPECT_EQ(11, c.total);
  EXPECT_EQ(-11, d.total);
}

TEST(ObserverListTest, StdIteratorRemoveBack) {
  ObserverList<Foo> observer_list;
  Adder a(1), b(-1), c(1), d(-1);
  Disrupter disrupter(&observer_list, true);

  observer_list.AddObserver(&a);
  observer_list.AddObserver(&b);
  observer_list.AddObserver(&c);
  observer_list.AddObserver(&d);
  observer_list.AddObserver(&disrupter);

  for (auto& o : observer_list)
    o.Observe(1);

  for (auto& o : observer_list)
    o.Observe(10);

  EXPECT_EQ(11, a.total);
  EXPECT_EQ(-11, b.total);
  EXPECT_EQ(11, c.total);
  EXPECT_EQ(-11, d.total);
}

TEST(ObserverListTest, NestedLoop) {
  ObserverList<Foo> observer_list;
  Adder a(1), b(-1), c(1), d(-1);
  Disrupter disrupter(&observer_list, true);

  observer_list.AddObserver(&disrupter);
  observer_list.AddObserver(&a);
  observer_list.AddObserver(&b);
  observer_list.AddObserver(&c);
  observer_list.AddObserver(&d);

  for (auto& o : observer_list) {
    o.Observe(10);

    for (auto& o : observer_list)
      o.Observe(1);
  }

  EXPECT_EQ(15, a.total);
  EXPECT_EQ(-15, b.total);
  EXPECT_EQ(15, c.total);
  EXPECT_EQ(-15, d.total);
}

TEST(ObserverListTest, NonCompactList) {
  ObserverList<Foo> observer_list;
  Adder a(1), b(-1);

  Disrupter disrupter1(&observer_list, true);
  Disrupter disrupter2(&observer_list, true);

  // Disrupt itself and another one.
  disrupter1.SetDoomed(&disrupter2);

  observer_list.AddObserver(&disrupter1);
  observer_list.AddObserver(&disrupter2);
  observer_list.AddObserver(&a);
  observer_list.AddObserver(&b);

  for (auto& o : observer_list) {
    // Get the { nullptr, nullptr, &a, &b } non-compact list
    // on the first inner pass.
    o.Observe(10);

    for (auto& o : observer_list)
      o.Observe(1);
  }

  EXPECT_EQ(13, a.total);
  EXPECT_EQ(-13, b.total);
}

TEST(ObserverListTest, BecomesEmptyThanNonEmpty) {
  ObserverList<Foo> observer_list;
  Adder a(1), b(-1);

  Disrupter disrupter1(&observer_list, true);
  Disrupter disrupter2(&observer_list, true);

  // Disrupt itself and another one.
  disrupter1.SetDoomed(&disrupter2);

  observer_list.AddObserver(&disrupter1);
  observer_list.AddObserver(&disrupter2);

  bool add_observers = true;
  for (auto& o : observer_list) {
    // Get the { nullptr, nullptr } empty list on the first inner pass.
    o.Observe(10);

    for (auto& o : observer_list)
      o.Observe(1);

    if (add_observers) {
      observer_list.AddObserver(&a);
      observer_list.AddObserver(&b);
      add_observers = false;
    }
  }

  EXPECT_EQ(12, a.total);
  EXPECT_EQ(-12, b.total);
}

TEST(ObserverListTest, AddObserverInTheLastObserve) {
  using FooList = ObserverList<Foo>;
  FooList observer_list;

  AddInObserve<FooList> a(&observer_list);
  Adder b(-1);

  a.SetToAdd(&b);
  observer_list.AddObserver(&a);

  auto it = observer_list.begin();
  while (it != observer_list.end()) {
    auto& observer = *it;
    // Intentionally increment the iterator before calling Observe(). The
    // ObserverList starts with only one observer, and it == observer_list.end()
    // should be true after the next line.
    ++it;
    // However, the first Observe() call will add a second observer: at this
    // point, it != observer_list.end() should be true, and Observe() should be
    // called on the newly added observer on the next iteration of the loop.
    observer.Observe(10);
  }

  EXPECT_EQ(-10, b.total);
}

}  // namespace
}  // namespace ledger
