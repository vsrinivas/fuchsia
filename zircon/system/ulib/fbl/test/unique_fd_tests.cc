// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include <fbl/algorithm.h>
#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

namespace {
TEST(UniqueFdTest, InvalidFd) {
  fbl::unique_fd fd;

  EXPECT_EQ(fd.get(), fbl::unique_fd::InvalidValue());
  EXPECT_EQ(fbl::unique_fd::InvalidValue(), fd.get());

  EXPECT_EQ(static_cast<int>(fd), fbl::unique_fd::InvalidValue());
  EXPECT_EQ(fbl::unique_fd::InvalidValue(), static_cast<int>(fd));

  EXPECT_EQ(false, fd.is_valid());
  EXPECT_EQ(static_cast<bool>(fd), false);
  EXPECT_EQ(false, static_cast<bool>(fd));

  EXPECT_EQ(fd.reset(), -1);

  EXPECT_FALSE(fd);
}

TEST(UniqueFdTest, ValidComparison) {
  int pipes[2];
  EXPECT_EQ(pipe(pipes), 0);
  {
    fbl::unique_fd in(pipes[1]);
    fbl::unique_fd out(pipes[0]);

    EXPECT_NE(in.get(), fbl::unique_fd::InvalidValue());
    EXPECT_NE(out.get(), fbl::unique_fd::InvalidValue());
    EXPECT_NE(fbl::unique_fd::InvalidValue(), in.get());
    EXPECT_NE(fbl::unique_fd::InvalidValue(), out.get());

    EXPECT_EQ(in.get(), in.get());
    EXPECT_NE(in.get(), out.get());
    EXPECT_FALSE(in == out);
    EXPECT_TRUE(in == in);
    EXPECT_TRUE(out == out);
    EXPECT_EQ(pipes[1], in.get());

    EXPECT_TRUE(in);
    EXPECT_TRUE(out);
  }
}

void VerifyPipesOpen(int in, int out) {
  char w = 'a';
  EXPECT_EQ(write(in, &w, 1), 1);
  char r;
  EXPECT_EQ(read(out, &r, 1), 1);
  EXPECT_EQ(r, w);
}

void VerifyPipesClosed(int in, int out) {
  char c = 'a';
  EXPECT_EQ(write(in, &c, 1), -1);
  EXPECT_EQ(read(out, &c, 1), -1);
}

TEST(UniqueFdTest, Scoping) {
  int pipes[2];
  EXPECT_EQ(pipe(pipes), 0);
  ASSERT_NO_FAILURES(VerifyPipesOpen(pipes[1], pipes[0]));
  {
    fbl::unique_fd in(pipes[1]);
    fbl::unique_fd out(pipes[0]);

    EXPECT_EQ(pipes[0], out.get());
    EXPECT_EQ(pipes[1], in.get());
    ASSERT_NO_FAILURES(VerifyPipesOpen(in.get(), out.get()));
  }
  ASSERT_NO_FAILURES(VerifyPipesClosed(pipes[1], pipes[0]));
}

TEST(UniqueFdTest, Swap) {
  int pipes[2];
  EXPECT_EQ(pipe(pipes), 0);
  ASSERT_NO_FAILURES(VerifyPipesOpen(pipes[1], pipes[0]));
  {
    fbl::unique_fd in(pipes[1]);
    fbl::unique_fd out(pipes[0]);

    in.swap(out);
    EXPECT_EQ(pipes[0], in.get());
    EXPECT_EQ(pipes[1], out.get());
    ASSERT_NO_FAILURES(VerifyPipesOpen(out.get(), in.get()));
  }
  ASSERT_NO_FAILURES(VerifyPipesClosed(pipes[1], pipes[0]));
  ASSERT_NO_FAILURES(VerifyPipesClosed(pipes[0], pipes[1]));
}

TEST(UniqueFdTest, Move) {
  // Move assignment
  int pipes[2];
  EXPECT_EQ(pipe(pipes), 0);
  ASSERT_NO_FAILURES(VerifyPipesOpen(pipes[1], pipes[0]));
  {
    fbl::unique_fd in(pipes[1]);
    fbl::unique_fd out(pipes[0]);

    fbl::unique_fd in2, out2;
    ASSERT_NO_FAILURES(VerifyPipesOpen(in.get(), out.get()));
    ASSERT_NO_FAILURES(VerifyPipesClosed(in2.get(), out2.get()));

    in2 = std::move(in);
    out2 = std::move(out);

    ASSERT_NO_FAILURES(VerifyPipesClosed(in.get(), out.get()));
    ASSERT_NO_FAILURES(VerifyPipesOpen(in2.get(), out2.get()));
  }
  ASSERT_NO_FAILURES(VerifyPipesClosed(pipes[1], pipes[0]));

  // Move constructor
  EXPECT_EQ(pipe(pipes), 0);
  ASSERT_NO_FAILURES(VerifyPipesOpen(pipes[1], pipes[0]));
  {
    fbl::unique_fd in(pipes[1]);
    fbl::unique_fd out(pipes[0]);

    ASSERT_NO_FAILURES(VerifyPipesOpen(in.get(), out.get()));

    fbl::unique_fd in2 = std::move(in);
    fbl::unique_fd out2 = std::move(out);

    ASSERT_NO_FAILURES(VerifyPipesClosed(in.get(), out.get()));
    ASSERT_NO_FAILURES(VerifyPipesOpen(in2.get(), out2.get()));
  }
  ASSERT_NO_FAILURES(VerifyPipesClosed(pipes[1], pipes[0]));
}

TEST(UniqueFdTest, Reset) {
  int pipes[2];
  EXPECT_EQ(pipe(pipes), 0);
  int other_pipes[2];
  EXPECT_EQ(pipe(other_pipes), 0);
  int third_pipes[2];
  EXPECT_EQ(pipe(third_pipes), 0);
  ASSERT_NO_FAILURES(VerifyPipesOpen(pipes[1], pipes[0]));
  ASSERT_NO_FAILURES(VerifyPipesOpen(other_pipes[1], other_pipes[0]));
  ASSERT_NO_FAILURES(VerifyPipesOpen(third_pipes[1], third_pipes[0]));
  {
    fbl::unique_fd in(pipes[1]);
    fbl::unique_fd out(pipes[0]);

    ASSERT_NO_FAILURES(VerifyPipesOpen(in.get(), out.get()));
    ASSERT_NO_FAILURES(VerifyPipesOpen(pipes[1], pipes[0]));
    ASSERT_NO_FAILURES(VerifyPipesOpen(other_pipes[1], other_pipes[0]));
    ASSERT_NO_FAILURES(VerifyPipesOpen(third_pipes[1], third_pipes[0]));

    EXPECT_EQ(in.reset(other_pipes[1]), 0);
    EXPECT_EQ(out.reset(other_pipes[0]), 0);

    ASSERT_NO_FAILURES(VerifyPipesOpen(in.get(), out.get()));
    ASSERT_NO_FAILURES(VerifyPipesClosed(pipes[1], pipes[0]));
    ASSERT_NO_FAILURES(VerifyPipesOpen(other_pipes[1], other_pipes[0]));
    ASSERT_NO_FAILURES(VerifyPipesOpen(third_pipes[1], third_pipes[0]));

    *in.reset_and_get_address() = third_pipes[1];
    *out.reset_and_get_address() = third_pipes[0];

    ASSERT_NO_FAILURES(VerifyPipesOpen(in.get(), out.get()));
    ASSERT_NO_FAILURES(VerifyPipesClosed(pipes[1], pipes[0]));
    ASSERT_NO_FAILURES(VerifyPipesClosed(other_pipes[1], other_pipes[0]));
    ASSERT_NO_FAILURES(VerifyPipesOpen(third_pipes[1], third_pipes[0]));

    EXPECT_EQ(in.reset(), 0);
    EXPECT_EQ(out.reset(), 0);

    ASSERT_NO_FAILURES(VerifyPipesClosed(in.get(), out.get()));
    ASSERT_NO_FAILURES(VerifyPipesClosed(pipes[1], pipes[0]));
    ASSERT_NO_FAILURES(VerifyPipesClosed(other_pipes[1], other_pipes[0]));
    ASSERT_NO_FAILURES(VerifyPipesClosed(third_pipes[1], third_pipes[0]));
  }
}

TEST(UniqueFdTest, Duplicate) {
  int pipes[2];
  EXPECT_EQ(pipe(pipes), 0);

  fbl::unique_fd in(pipes[1]);
  fbl::unique_fd out(pipes[0]);
  ASSERT_NO_FAILURES(VerifyPipesOpen(in.get(), out.get()));
  {
    fbl::unique_fd in2 = in.duplicate();
    fbl::unique_fd out2 = out.duplicate();
    ASSERT_NO_FAILURES(VerifyPipesOpen(in2.get(), out2.get()));

    ASSERT_NO_FAILURES(VerifyPipesOpen(in2.get(), out.get()));
    ASSERT_NO_FAILURES(VerifyPipesOpen(in.get(), out2.get()));
    ASSERT_NO_FAILURES(VerifyPipesOpen(in.get(), out.get()));
  }
  ASSERT_NO_FAILURES(VerifyPipesOpen(in.get(), out.get()));
}

}  // namespace
