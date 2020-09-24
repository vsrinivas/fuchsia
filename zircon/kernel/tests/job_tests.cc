// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/unittest/unittest.h>

#include <fbl/alloc_checker.h>
#include <fbl/string_buffer.h>
#include <ktl/unique_ptr.h>
#include <object/handle.h>
#include <object/job_dispatcher.h>

#include "tests.h"

namespace {

template <typename JobCallbackType>
class JobWalker final : public JobEnumerator {
 public:
  JobWalker(JobCallbackType cb) : cb_(cb) {}
  JobWalker(const JobWalker&) = delete;
  JobWalker(JobWalker&& other) : cb_(other.cb_) {}

 private:
  bool OnJob(JobDispatcher* job) final {
    cb_(job);
    return true;
  }

  const JobCallbackType cb_;
};

template <typename JobCallbackType>
JobWalker<JobCallbackType> MakeJobWalker(JobCallbackType cb) {
  return JobWalker<JobCallbackType>(cb);
}

using TreeString = fbl::StringBuffer<512>;

void tree_to_string(JobDispatcher* root, int indent, TreeString* buf) {
  for (int i = 0; i < indent; ++i)
    buf->Append(" ");

  char name[ZX_MAX_NAME_LEN];
  root->get_name(name);
  buf->Append(name);

  if (root->get_kill_on_oom())
    buf->Append(" KILL");

  buf->Append("\n");

  auto walker =
      MakeJobWalker([&indent, &buf](JobDispatcher* job) { tree_to_string(job, indent + 2, buf); });
  root->EnumerateChildren(&walker, false);
}

bool oom_job_kill_trivial() {
  BEGIN_TEST;

  KernelHandle<JobDispatcher> root;
  zx_rights_t rights;
  ASSERT_EQ(JobDispatcher::Create(0, GetRootJobDispatcher(), &root, &rights), ZX_OK, "root create");
  root.dispatcher()->set_name("root", 4);
  root.dispatcher()->set_kill_on_oom(true);

  TreeString buf;
  tree_to_string(root.dispatcher().get(), 0, &buf);
  EXPECT_TRUE(strcmp("root KILL\n", buf.c_str()) == 0, "incorrect start state");

  EXPECT_TRUE(root.dispatcher()->KillJobWithKillOnOOM(), "killed");

  END_TEST;
}

bool oom_job_kill_ordering() {
  BEGIN_TEST;

  // TODO(fxbug.dev/45616): Test disabled because it was broken by the change to
  // a stable sort. The test expectations need to be fixed.
  END_TEST;

  // Given this tree (KILL indicating jobs that have ZX_PROP_
  //
  //   root
  //     child1
  //       gchild1
  //       gchild2 KILL
  //       gchild3
  //     child2 KILL
  //       gchild4 KILL
  //       gchild5 KILL
  //
  KernelHandle<JobDispatcher> root;
  zx_rights_t rights;
  ASSERT_EQ(JobDispatcher::Create(0, GetRootJobDispatcher(), &root, &rights), ZX_OK, "root create");
  root.dispatcher()->set_name("root", 4);

  KernelHandle<JobDispatcher> child1, child2;
  KernelHandle<JobDispatcher> gchild1, gchild2, gchild3;
  KernelHandle<JobDispatcher> gchild4, gchild5;

  ASSERT_EQ(JobDispatcher::Create(0, root.dispatcher(), &child1, &rights), ZX_OK);
  child1.dispatcher()->set_name("child1", 6);

  ASSERT_EQ(JobDispatcher::Create(0, root.dispatcher(), &child2, &rights), ZX_OK);
  child2.dispatcher()->set_name("child2", 6);
  child2.dispatcher()->set_kill_on_oom(true);

  ASSERT_EQ(JobDispatcher::Create(0, child1.dispatcher(), &gchild1, &rights), ZX_OK);
  gchild1.dispatcher()->set_name("gchild1", 7);

  ASSERT_EQ(JobDispatcher::Create(0, child1.dispatcher(), &gchild2, &rights), ZX_OK);
  gchild2.dispatcher()->set_kill_on_oom(true);
  gchild2.dispatcher()->set_name("gchild2", 7);

  ASSERT_EQ(JobDispatcher::Create(0, child1.dispatcher(), &gchild3, &rights), ZX_OK);
  gchild3.dispatcher()->set_name("gchild3", 7);

  ASSERT_EQ(JobDispatcher::Create(0, child2.dispatcher(), &gchild4, &rights), ZX_OK);
  gchild4.dispatcher()->set_kill_on_oom(true);
  gchild4.dispatcher()->set_name("gchild4", 7);

  ASSERT_EQ(JobDispatcher::Create(0, child2.dispatcher(), &gchild5, &rights), ZX_OK);
  gchild5.dispatcher()->set_kill_on_oom(true);
  gchild5.dispatcher()->set_name("gchild5", 7);

  TreeString buf;
  buf.Clear();
  tree_to_string(root.dispatcher().get(), 0, &buf);
  EXPECT_TRUE(strcmp(R"(root
  child1
    gchild1
    gchild2 KILL
    gchild3
  child2 KILL
    gchild4 KILL
    gchild5 KILL
)",
                     buf.c_str()) == 0,
              "incorrect start state");

  EXPECT_TRUE(root.dispatcher()->KillJobWithKillOnOOM(), "killed");
  buf.Clear();
  tree_to_string(root.dispatcher().get(), 0, &buf);
  EXPECT_TRUE(strcmp(R"(root
  child1
    gchild1
    gchild2 KILL
    gchild3
  child2 KILL
    gchild4 KILL
)",
                     buf.c_str()) == 0,
              "grandchild #5 should be first");

  EXPECT_TRUE(root.dispatcher()->KillJobWithKillOnOOM(), "killed");
  buf.Clear();
  tree_to_string(root.dispatcher().get(), 0, &buf);
  EXPECT_TRUE(strcmp(R"(root
  child1
    gchild1
    gchild2 KILL
    gchild3
  child2 KILL
)",
                     buf.c_str()) == 0,
              "then grandchild #4");

  EXPECT_TRUE(root.dispatcher()->KillJobWithKillOnOOM(), "killed");

  buf.Clear();
  tree_to_string(root.dispatcher().get(), 0, &buf);
  EXPECT_TRUE(strcmp(R"(root
  child1
    gchild1
    gchild3
  child2 KILL
)",
                     buf.c_str()) == 0,
              "then grandchild #2 in the earlier child");

  EXPECT_TRUE(root.dispatcher()->KillJobWithKillOnOOM(), "killed");
  buf.Clear();
  tree_to_string(root.dispatcher().get(), 0, &buf);
  EXPECT_TRUE(strcmp(R"(root
  child1
    gchild1
    gchild3
)",
                     buf.c_str()) == 0,
              "finally higher up child2");

  EXPECT_FALSE(root.dispatcher()->KillJobWithKillOnOOM(), "no kill");
  buf.Clear();
  tree_to_string(root.dispatcher().get(), 0, &buf);
  EXPECT_TRUE(strcmp(R"(root
  child1
    gchild1
    gchild3
)",
                     buf.c_str()) == 0,
              "subsequent kills should have no effect");

  // Clean up.
  root.dispatcher()->Kill(0);

  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(job_tests)
UNITTEST("test trivial oom kill", oom_job_kill_trivial)
UNITTEST("test ordering of oom kill", oom_job_kill_ordering)
UNITTEST_END_TESTCASE(job_tests, "job", "Tests for jobs")
