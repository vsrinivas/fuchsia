#include "expectations.h"
#include "netstack2_expectations.h"

namespace netstack_syscall_test {

// TODO(https://fxbug.dev/104104): Remove sync expectations after Fast UDP
// rollout.

void AddNonPassingTests(TestMap& tests) {
  AddNonPassingTestsCommonNetstack2(tests);

  // https://fxbug.dev/45245
  ExpectFailure(tests, "AllUDPSockets/NonStreamSocketPairTest.SendMsgTooLarge/*");

#undef GUNIT_NAME
#undef GUNIT_NAME_P
}

}  // namespace netstack_syscall_test
