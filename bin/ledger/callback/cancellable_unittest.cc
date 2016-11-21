// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/callback/cancellable.h"
#include "gtest/gtest.h"

namespace callback {
namespace {

class FakeCancellable : public Cancellable {
 public:
  static ftl::RefPtr<FakeCancellable> Create(bool* destructed = nullptr) {
    return ftl::AdoptRef(new FakeCancellable(destructed));
  }

  FakeCancellable(bool* destructed) : destructed_(destructed) {}

  void Cancel() override { ++nb_cancel; }

  bool IsDone() override {
    ++nb_is_done;
    return false;
  }

  void SetOnDone(ftl::Closure callback) override {
    ++nb_on_done;
    this->callback = callback;
  }

  int nb_cancel = 0;
  int nb_is_done = 0;
  int nb_on_done = 0;
  ftl::Closure callback;

 private:
  ~FakeCancellable() override {
    if (destructed_)
      *destructed_ = true;
  }

  bool* destructed_;
};

TEST(AutoCancel, EmptyAutoCancel) {
  AutoCancel auto_cancel;
}

TEST(AutoCancel, CancelOnDestruction) {
  auto cancellable = FakeCancellable::Create();
  EXPECT_EQ(0, cancellable->nb_cancel);
  {
    AutoCancel auto_cancel(cancellable);
    EXPECT_EQ(0, cancellable->nb_cancel);
  }
  EXPECT_EQ(1, cancellable->nb_cancel);
}

TEST(AutoCancel, ResetNoArgument) {
  auto cancellable = FakeCancellable::Create();
  AutoCancel auto_cancel(cancellable);
  auto_cancel.Reset();
  EXPECT_EQ(1, cancellable->nb_cancel);
}

TEST(AutoCancel, ResetArgument) {
  auto cancellable1 = FakeCancellable::Create();
  auto cancellable2 = FakeCancellable::Create();
  AutoCancel auto_cancel(cancellable1);
  auto_cancel.Reset(cancellable2);
  EXPECT_EQ(1, cancellable1->nb_cancel);
  EXPECT_EQ(0, cancellable2->nb_cancel);
}

TEST(CancellableContainer, CancelOnDestruction) {
  auto cancellable1 = FakeCancellable::Create();
  auto cancellable2 = FakeCancellable::Create();

  EXPECT_EQ(0, cancellable1->nb_cancel);
  EXPECT_EQ(0, cancellable1->nb_on_done);
  EXPECT_EQ(0, cancellable2->nb_cancel);
  EXPECT_EQ(0, cancellable2->nb_on_done);
  {
    CancellableContainer container;
    container.AddCancellable(cancellable1);
    container.AddCancellable(cancellable2);

    EXPECT_EQ(0, cancellable1->nb_cancel);
    EXPECT_EQ(1, cancellable1->nb_on_done);
    EXPECT_EQ(0, cancellable2->nb_cancel);
    EXPECT_EQ(1, cancellable2->nb_on_done);
  }

  EXPECT_EQ(1, cancellable1->nb_cancel);
  EXPECT_EQ(1, cancellable2->nb_cancel);
}

TEST(CancellableContainer, CancelOnReset) {
  auto cancellable1 = FakeCancellable::Create();
  auto cancellable2 = FakeCancellable::Create();

  EXPECT_EQ(0, cancellable1->nb_cancel);
  EXPECT_EQ(0, cancellable1->nb_on_done);
  EXPECT_EQ(0, cancellable2->nb_cancel);
  EXPECT_EQ(0, cancellable2->nb_on_done);

  CancellableContainer container;
  container.AddCancellable(cancellable1);
  container.AddCancellable(cancellable2);

  EXPECT_EQ(0, cancellable1->nb_cancel);
  EXPECT_EQ(1, cancellable1->nb_on_done);
  EXPECT_EQ(0, cancellable2->nb_cancel);
  EXPECT_EQ(1, cancellable2->nb_on_done);

  container.Reset();

  EXPECT_EQ(1, cancellable1->nb_cancel);
  EXPECT_EQ(1, cancellable2->nb_cancel);
}

TEST(CancellableContainer, ClearOnDone) {
  bool destructed = false;
  auto cancellable1 = FakeCancellable::Create(&destructed);
  auto cancellable2 = FakeCancellable::Create();

  EXPECT_EQ(0, cancellable1->nb_cancel);
  EXPECT_EQ(0, cancellable1->nb_on_done);
  EXPECT_EQ(0, cancellable2->nb_cancel);
  EXPECT_EQ(0, cancellable2->nb_on_done);
  {
    CancellableContainer container;
    container.AddCancellable(cancellable1);
    container.AddCancellable(cancellable2);

    EXPECT_EQ(0, cancellable1->nb_cancel);
    EXPECT_EQ(1, cancellable1->nb_on_done);
    EXPECT_EQ(0, cancellable2->nb_cancel);
    EXPECT_EQ(1, cancellable2->nb_on_done);

    cancellable1->callback();
    EXPECT_EQ(0, cancellable1->nb_cancel);
    cancellable1 = nullptr;
    // Check that container doesn't keep a reference to cancellable1 once it is
    // done.
    EXPECT_TRUE(destructed);
  }

  EXPECT_EQ(1, cancellable2->nb_cancel);
}

}  //  namespace
}  //  namespace callback
