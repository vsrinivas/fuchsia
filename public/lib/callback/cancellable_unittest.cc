// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/callback/cancellable.h"

#include <lib/fit/function.h>

#include "gtest/gtest.h"

namespace callback {
namespace {

class FakeCancellable : public Cancellable {
 public:
  static fxl::RefPtr<FakeCancellable> Create(bool* destructed = nullptr) {
    return fxl::AdoptRef(new FakeCancellable(destructed));
  }

  explicit FakeCancellable(bool* destructed) : destructed_(destructed) {}

  void Cancel() override {
    if (is_done_)
      return;
    ++nb_cancel;
  }

  bool IsDone() override {
    ++nb_is_done;
    return is_done_;
  }

  void SetOnDone(fit::closure callback) override {
    ++nb_set_on_done;
    this->callback = std::move(callback);
  }

  void Do() {
    is_done_ = true;
    if (callback)
      callback();
  }
  int nb_cancel = 0;
  int nb_is_done = 0;
  int nb_set_on_done = 0;

  fit::closure callback;

 private:
  ~FakeCancellable() override {
    if (destructed_)
      *destructed_ = true;
  }

  bool is_done_ = false;
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
  EXPECT_EQ(0, cancellable1->nb_set_on_done);
  EXPECT_EQ(0, cancellable2->nb_cancel);
  EXPECT_EQ(0, cancellable2->nb_set_on_done);
  {
    CancellableContainer container;
    container.emplace(cancellable1);
    container.emplace(cancellable2);

    EXPECT_EQ(0, cancellable1->nb_cancel);
    EXPECT_EQ(1, cancellable1->nb_set_on_done);
    EXPECT_EQ(0, cancellable2->nb_cancel);
    EXPECT_EQ(1, cancellable2->nb_set_on_done);
  }

  EXPECT_EQ(1, cancellable1->nb_cancel);
  EXPECT_EQ(1, cancellable2->nb_cancel);
}

TEST(CancellableContainer, CancelOnReset) {
  auto cancellable1 = FakeCancellable::Create();
  auto cancellable2 = FakeCancellable::Create();

  EXPECT_EQ(0, cancellable1->nb_cancel);
  EXPECT_EQ(0, cancellable1->nb_set_on_done);
  EXPECT_EQ(0, cancellable2->nb_cancel);
  EXPECT_EQ(0, cancellable2->nb_set_on_done);

  CancellableContainer container;
  container.emplace(cancellable1);
  container.emplace(cancellable2);

  EXPECT_EQ(0, cancellable1->nb_cancel);
  EXPECT_EQ(1, cancellable1->nb_set_on_done);
  EXPECT_EQ(0, cancellable2->nb_cancel);
  EXPECT_EQ(1, cancellable2->nb_set_on_done);
}

TEST(CancellableContainer, ClearOnDone) {
  bool destructed = false;
  auto cancellable1 = FakeCancellable::Create(&destructed);
  auto cancellable2 = FakeCancellable::Create();

  EXPECT_EQ(0, cancellable1->nb_cancel);
  EXPECT_EQ(0, cancellable1->nb_set_on_done);
  EXPECT_EQ(0, cancellable2->nb_cancel);
  EXPECT_EQ(0, cancellable2->nb_set_on_done);
  {
    CancellableContainer container;
    container.emplace(cancellable1);
    container.emplace(cancellable2);

    EXPECT_EQ(0, cancellable1->nb_cancel);
    EXPECT_EQ(1, cancellable1->nb_set_on_done);
    EXPECT_EQ(0, cancellable2->nb_cancel);
    EXPECT_EQ(1, cancellable2->nb_set_on_done);

    cancellable1->Do();
    EXPECT_EQ(0, cancellable1->nb_cancel);
    cancellable1 = nullptr;
    // Check that container doesn't keep a reference to cancellable1 once it is
    // done.
    EXPECT_TRUE(destructed);
  }

  EXPECT_EQ(1, cancellable2->nb_cancel);
}

TEST(CancellableContainer, Move) {
  auto cancellable1 = FakeCancellable::Create();
  auto cancellable2 = FakeCancellable::Create();

  EXPECT_EQ(0, cancellable1->nb_cancel);
  EXPECT_EQ(0, cancellable1->nb_set_on_done);
  EXPECT_EQ(0, cancellable2->nb_cancel);
  EXPECT_EQ(0, cancellable2->nb_set_on_done);

  CancellableContainer container;
  container.emplace(cancellable1);
  container.emplace(cancellable2);
  EXPECT_EQ(0, cancellable1->nb_cancel);
  EXPECT_EQ(1, cancellable1->nb_set_on_done);
  EXPECT_EQ(0, cancellable2->nb_cancel);
  EXPECT_EQ(1, cancellable2->nb_set_on_done);

  {
    CancellableContainer moved = std::move(container);
    EXPECT_EQ(0, cancellable1->nb_cancel);
    EXPECT_EQ(1, cancellable1->nb_set_on_done);
    EXPECT_EQ(0, cancellable2->nb_cancel);
    EXPECT_EQ(1, cancellable2->nb_set_on_done);
  }

  EXPECT_EQ(1, cancellable1->nb_cancel);
  EXPECT_EQ(1, cancellable2->nb_cancel);
}

}  //  namespace
}  //  namespace callback
