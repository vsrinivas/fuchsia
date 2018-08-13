// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mediaplayer/util/incident.h"

#include "gtest/gtest.h"

namespace media_player {
namespace {

// Tests whether Incident::Occur and Incident::Reset have the right effect on
// Incident::occurred.
TEST(IncidentTest, Basics) {
  Incident under_test;

  EXPECT_FALSE(under_test.occurred());

  under_test.Occur();
  EXPECT_TRUE(under_test.occurred());

  under_test.Reset();
  EXPECT_FALSE(under_test.occurred());
}

// Tests whether a consequence registered with Incident::When runs only after
// Incident::Occur is called.
TEST(IncidentTest, When_Delayed) {
  Incident under_test;

  // These two together should be a no-op.
  under_test.Occur();
  under_test.Reset();

  bool consequence_ran = false;
  under_test.When([&consequence_ran]() { consequence_ran = true; });
  EXPECT_FALSE(consequence_ran);

  under_test.Occur();
  EXPECT_TRUE(consequence_ran);
}

// Tests whether a consequence registered with Incident::When runs immediately
// when Incident::Occur was called first.
TEST(IncidentTest, When_Immediate) {
  Incident under_test;

  under_test.Occur();

  bool consequence_ran = false;
  under_test.When([&consequence_ran]() { consequence_ran = true; });
  EXPECT_TRUE(consequence_ran);
}

// Tests whether a consequence registered with Incident::When runs
// Incident::Reset is called before Incident::Occur (it shouldn't).
TEST(IncidentTest, When_Reset) {
  Incident under_test;

  bool consequence_ran = false;
  under_test.When([&consequence_ran]() { consequence_ran = true; });
  EXPECT_FALSE(consequence_ran);

  under_test.Reset();
  EXPECT_FALSE(consequence_ran);

  under_test.Occur();
  EXPECT_FALSE(consequence_ran);
}

// Tests whether a consequences registered with Incident::When run in the
// correct order.
TEST(IncidentTest, When_Order) {
  Incident under_test;
  int sequence_counter = 0;

  under_test.When([&sequence_counter]() { EXPECT_EQ(0, sequence_counter++); });
  under_test.When([&sequence_counter]() { EXPECT_EQ(1, sequence_counter++); });
  under_test.When([&sequence_counter]() { EXPECT_EQ(2, sequence_counter++); });
  under_test.When([&sequence_counter]() { EXPECT_EQ(3, sequence_counter++); });

  under_test.Occur();

  under_test.When([&sequence_counter]() { EXPECT_EQ(4, sequence_counter++); });
  under_test.When([&sequence_counter]() { EXPECT_EQ(5, sequence_counter++); });
  under_test.When([&sequence_counter]() { EXPECT_EQ(6, sequence_counter++); });
  under_test.When([&sequence_counter]() { EXPECT_EQ(7, sequence_counter++); });

  EXPECT_EQ(8, sequence_counter);
}

// Tests whether a consequence registered with Incident::When runs when
// Incident::Occur is never called and the Incident is deleted (it shouldn't).
TEST(IncidentTest, When_After_Delete) {
  bool consequence_ran = false;

  {
    Incident under_test;
    under_test.When([&consequence_ran]() { consequence_ran = true; });
  }

  EXPECT_FALSE(consequence_ran);
}

// Tests whether ThreadsafeIncident::Occur and ThreadsafeIncident::Reset have
// the right effect on ThreadsafeIncident::occurred.
TEST(ThreadsafeIncidentTest, Basics) {
  ThreadsafeIncident under_test;

  EXPECT_FALSE(under_test.occurred());

  under_test.Occur();
  EXPECT_TRUE(under_test.occurred());

  under_test.Reset();
  EXPECT_FALSE(under_test.occurred());
}

// Tests whether a consequence registered with ThreadsafeIncident::When runs
// only after ThreadsafeIncident::Occur is called.
TEST(ThreadsafeIncidentTest, When_Delayed) {
  ThreadsafeIncident under_test;

  // These two together should be a no-op.
  under_test.Occur();
  under_test.Reset();

  bool consequence_ran = false;
  under_test.When([&consequence_ran]() { consequence_ran = true; });
  EXPECT_FALSE(consequence_ran);

  under_test.Occur();
  EXPECT_TRUE(consequence_ran);
}

// Tests whether a consequence registered with ThreadsafeIncident::When runs
// immediately when ThreadsafeIncident::Occur was called first.
TEST(ThreadsafeIncidentTest, When_Immediate) {
  ThreadsafeIncident under_test;

  under_test.Occur();

  bool consequence_ran = false;
  under_test.When([&consequence_ran]() { consequence_ran = true; });
  EXPECT_TRUE(consequence_ran);
}

// Tests whether a consequence registered with ThreadsafeIncident::When runs
// ThreadsafeIncident::Reset is called before ThreadsafeIncident::Occur (it
// shouldn't).
TEST(ThreadsafeIncidentTest, When_Reset) {
  ThreadsafeIncident under_test;

  bool consequence_ran = false;
  under_test.When([&consequence_ran]() { consequence_ran = true; });
  EXPECT_FALSE(consequence_ran);

  under_test.Reset();
  EXPECT_FALSE(consequence_ran);

  under_test.Occur();
  EXPECT_FALSE(consequence_ran);
}

// Tests whether a consequences registered with ThreadsafeIncident::When run in
// the correct order.
TEST(ThreadsafeIncidentTest, When_Order) {
  ThreadsafeIncident under_test;
  int sequence_counter = 0;

  under_test.When([&sequence_counter]() { EXPECT_EQ(0, sequence_counter++); });
  under_test.When([&sequence_counter]() { EXPECT_EQ(1, sequence_counter++); });
  under_test.When([&sequence_counter]() { EXPECT_EQ(2, sequence_counter++); });
  under_test.When([&sequence_counter]() { EXPECT_EQ(3, sequence_counter++); });

  under_test.Occur();

  under_test.When([&sequence_counter]() { EXPECT_EQ(4, sequence_counter++); });
  under_test.When([&sequence_counter]() { EXPECT_EQ(5, sequence_counter++); });
  under_test.When([&sequence_counter]() { EXPECT_EQ(6, sequence_counter++); });
  under_test.When([&sequence_counter]() { EXPECT_EQ(7, sequence_counter++); });

  EXPECT_EQ(8, sequence_counter);
}

// Tests whether a consequence registered with ThreadsafeIncident::When runs
// when ThreadsafeIncident::Occur is never called and the ThreadsafeIncident is
// deleted (it shouldn't).
TEST(ThreadsafeIncidentTest, When_After_Delete) {
  bool consequence_ran = false;

  {
    ThreadsafeIncident under_test;
    under_test.When([&consequence_ran]() { consequence_ran = true; });
  }

  EXPECT_FALSE(consequence_ran);
}

}  // namespace
}  // namespace media_player
