#include "expectations.h"
#include "netstack2_expectations.h"

namespace netstack_syscall_test {

void AddNonPassingTests(TestMap& tests) {
  AddNonPassingTestsCommonNetstack2(tests);

  // Fast UDP doesn't enforce recieve buffer limits due to the use of a zircon
  // socket.
  SkipTest(tests, "AllInetTests/UdpSocketTest.RecvBufLimits/*");
}

}  // namespace netstack_syscall_test
