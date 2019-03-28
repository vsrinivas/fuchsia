#include <sys/utsname.h>
#include <unistd.h>

#include "gtest/gtest.h"

namespace {
TEST(NetstackTest, IoctlNetcGetNodename) {
  // gethostname calls uname, which bottoms out in a call to
  // ioctl_netc_get_nodename that isn't otherwise exposed in the SDK.
  char hostname[65];
  EXPECT_EQ(gethostname(hostname, sizeof(hostname)), 0) << strerror(errno);
  struct utsname uts;
  EXPECT_EQ(uname(&uts), 0) << strerror(errno);
}
}  // namespace
