// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fasync/future.h>

#include <map>

#include <zxtest/zxtest.h>

namespace {

enum class disposition { pending, resumed, released };

class fake_resolver : public fasync::suspended_task::resolver {
 public:
  uint64_t num_tickets_issued() const { return next_ticket_ - 1; }

  fasync::suspended_task::ticket obtain_ticket() {
    fasync::suspended_task::ticket ticket = next_ticket_++;
    tickets_.emplace(ticket, disposition::pending);
    return ticket;
  }

  disposition get_disposition(fasync::suspended_task::ticket ticket) {
    auto it = tickets_.find(ticket);
    EXPECT_NE(it, tickets_.end());
    return it->second;
  }

  fasync::suspended_task::ticket duplicate_ticket(fasync::suspended_task::ticket ticket) override {
    auto it = tickets_.find(ticket);
    EXPECT_NE(it, tickets_.end());
    EXPECT_EQ(it->second, disposition::pending);
    return obtain_ticket();
  }

  void resolve_ticket(fasync::suspended_task::ticket ticket, bool resume_task) override {
    auto it = tickets_.find(ticket);
    ASSERT_NE(it, tickets_.end());
    ASSERT_EQ(it->second, disposition::pending);
    it->second = resume_task ? disposition::resumed : disposition::released;
  }

 private:
  fasync::suspended_task::ticket next_ticket_ = 1;
  std::map<fasync::suspended_task::ticket, disposition> tickets_;
};

TEST(SuspendedTaskTests, test) {
  fake_resolver resolver;
  {
    fasync::suspended_task empty;
    EXPECT_FALSE(empty);

    fasync::suspended_task empty_copy(empty);
    EXPECT_FALSE(empty_copy);
    EXPECT_FALSE(empty);

    fasync::suspended_task empty_move(std::move(empty));
    EXPECT_FALSE(empty_move);
    EXPECT_FALSE(empty);

    fasync::suspended_task task(resolver, resolver.obtain_ticket());
    EXPECT_TRUE(task);
    EXPECT_EQ(1, resolver.num_tickets_issued());
    EXPECT_EQ(disposition::pending, resolver.get_disposition(1));

    fasync::suspended_task task_copy(task);
    EXPECT_TRUE(task_copy);
    EXPECT_TRUE(task);
    EXPECT_EQ(2, resolver.num_tickets_issued());
    EXPECT_EQ(disposition::pending, resolver.get_disposition(1));
    EXPECT_EQ(disposition::pending, resolver.get_disposition(2));

    fasync::suspended_task task_move(std::move(task));
    EXPECT_TRUE(task_move);
    EXPECT_FALSE(task);
    EXPECT_EQ(2, resolver.num_tickets_issued());
    EXPECT_EQ(disposition::pending, resolver.get_disposition(1));
    EXPECT_EQ(disposition::pending, resolver.get_disposition(2));

    fasync::suspended_task x;
    x = empty;
    EXPECT_FALSE(x);

    x = task_copy;
    EXPECT_TRUE(x);
    EXPECT_TRUE(task_copy);
    EXPECT_EQ(3, resolver.num_tickets_issued());
    EXPECT_EQ(disposition::pending, resolver.get_disposition(1));
    EXPECT_EQ(disposition::pending, resolver.get_disposition(2));
    EXPECT_EQ(disposition::pending, resolver.get_disposition(3));

    x = std::move(empty_move);  // x's ticket is released here
    EXPECT_FALSE(x);
    EXPECT_FALSE(empty_move);

    EXPECT_EQ(3, resolver.num_tickets_issued());
    EXPECT_EQ(disposition::pending, resolver.get_disposition(1));
    EXPECT_EQ(disposition::pending, resolver.get_disposition(2));
    EXPECT_EQ(disposition::released, resolver.get_disposition(3));

    x = task_copy;             // assign x a duplicate ticket
    x = std::move(task_move);  // x's ticket is released here
    EXPECT_TRUE(x);
    EXPECT_TRUE(task_copy);
    EXPECT_FALSE(task_move);
    EXPECT_EQ(4, resolver.num_tickets_issued());
    EXPECT_EQ(disposition::pending, resolver.get_disposition(1));
    EXPECT_EQ(disposition::pending, resolver.get_disposition(2));
    EXPECT_EQ(disposition::released, resolver.get_disposition(3));
    EXPECT_EQ(disposition::released, resolver.get_disposition(4));

    x.resume();  // x's ticket is resumed here
    EXPECT_FALSE(x);
    EXPECT_EQ(4, resolver.num_tickets_issued());
    EXPECT_EQ(disposition::resumed, resolver.get_disposition(1));
    EXPECT_EQ(disposition::pending, resolver.get_disposition(2));
    EXPECT_EQ(disposition::released, resolver.get_disposition(3));
    EXPECT_EQ(disposition::released, resolver.get_disposition(4));

    x.resume();  // already resumed so has no effect
    EXPECT_FALSE(x);

    x.reset();  // already resumed so has no effect
    EXPECT_FALSE(x);

    // note: task_copy still has a ticket here which will be
    // released when the scope exits
  }
  EXPECT_EQ(4, resolver.num_tickets_issued());
  EXPECT_EQ(disposition::resumed, resolver.get_disposition(1));
  EXPECT_EQ(disposition::released, resolver.get_disposition(2));
  EXPECT_EQ(disposition::released, resolver.get_disposition(3));
  EXPECT_EQ(disposition::released, resolver.get_disposition(4));
}

TEST(SuspendedTaskTests, swapping) {
  fake_resolver resolver;
  {
    fasync::suspended_task a(resolver, resolver.obtain_ticket());
    fasync::suspended_task b(resolver, resolver.obtain_ticket());
    fasync::suspended_task c;
    EXPECT_EQ(2, resolver.num_tickets_issued());
    EXPECT_EQ(disposition::pending, resolver.get_disposition(1));
    EXPECT_EQ(disposition::pending, resolver.get_disposition(2));

    a.swap(c);
    EXPECT_FALSE(a);
    EXPECT_TRUE(c);
    EXPECT_EQ(disposition::pending, resolver.get_disposition(1));
    EXPECT_EQ(disposition::pending, resolver.get_disposition(2));

    a.swap(a);
    EXPECT_FALSE(a);
    EXPECT_EQ(disposition::pending, resolver.get_disposition(1));
    EXPECT_EQ(disposition::pending, resolver.get_disposition(2));

    swap(c, b);
    EXPECT_TRUE(c);
    EXPECT_TRUE(b);
    EXPECT_EQ(disposition::pending, resolver.get_disposition(1));
    EXPECT_EQ(disposition::pending, resolver.get_disposition(2));

    swap(c, c);
    EXPECT_TRUE(c);
    EXPECT_EQ(disposition::pending, resolver.get_disposition(1));
    EXPECT_EQ(disposition::pending, resolver.get_disposition(2));

    c.resume();
    EXPECT_FALSE(c);
    EXPECT_EQ(disposition::pending, resolver.get_disposition(1));
    EXPECT_EQ(disposition::resumed, resolver.get_disposition(2));

    b.reset();
    EXPECT_FALSE(b);
    EXPECT_EQ(disposition::released, resolver.get_disposition(1));
    EXPECT_EQ(disposition::resumed, resolver.get_disposition(2));
  }
}

}  // namespace
