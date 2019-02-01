
#include "garnet/public/lib/cobalt/cpp/project_profile.h"

#include "garnet/public/lib/cobalt/cpp/test_metrics_registry.cb.h"
#include "gtest/gtest.h"

namespace cobalt {

TEST(ProjectProfile, FromString) { ProjectProfileFromString(""); }

TEST(ProjectProfile, FromBase64String) {
  ProjectProfileFromBase64String(cobalt_test_metrics::kConfig);
}

TEST(ProjectProfile, FromFile) {
  ProjectProfileFromFile(
      "/pkgfs/packages/garnet_lib_tests/0/data/test_metrics_registry.pb");
}

}  // namespace cobalt
