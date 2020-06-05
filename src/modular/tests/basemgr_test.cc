#include "src/modular/lib/modular_test_harness/cpp/fake_session_shell.h"
#include "src/modular/lib/modular_test_harness/cpp/test_harness_fixture.h"

namespace {
class BasemgrTest : public modular_testing::TestHarnessFixture {};

// Tests that when multiple session shell are provided the first is picked
TEST_F(BasemgrTest, StartFirstShellWhenMultiple) {
  fuchsia::modular::testing::TestHarnessSpec spec;
  modular_testing::TestHarnessBuilder builder(std::move(spec));

  // Session shells used in list
  auto session_shell = modular_testing::FakeSessionShell::CreateWithDefaultOptions();
  auto session_shell2 = modular_testing::FakeSessionShell::CreateWithDefaultOptions();

  // Create session shell list (appended in order)
  builder.InterceptSessionShell(session_shell->BuildInterceptOptions());
  builder.InterceptSessionShell(session_shell2->BuildInterceptOptions());
  builder.BuildAndRun(test_harness());

  // Run until one is started
  RunLoopUntil([&] { return session_shell->is_running() || session_shell2->is_running(); });

  // Assert only first one is started
  EXPECT_TRUE(session_shell->is_running());
  EXPECT_FALSE(session_shell2->is_running());
}
}  // namespace
