#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/testing/enclosing_environment.h>
#include <lib/sys/cpp/testing/test_with_environment.h>
#include <zircon/device/vfs.h>

#include <ddk/platform-defs.h>

namespace {

constexpr char kDevmgrPkgUrl[] =
    "fuchsia-pkg://fuchsia.com/mock-sensor-devmgr#meta/mock-sensor-devmgr.cmx";
constexpr char kIsolatedDevmgrServiceName[] = "fuchsia.camera.MockSensorDevmgr";

class MockSensorIntegrationTest : public sys::testing::TestWithEnvironment {
 public:
  void SetUp() override;

  fidl::InterfacePtr<fuchsia::sys::ComponentController> ctrl_;
  fidl::InterfaceHandle<fuchsia::io::Directory> devfs_dir_;
};

void MockSensorIntegrationTest::SetUp() {
  auto ctx = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  fidl::InterfacePtr<fuchsia::sys::Launcher> launcher;
  ctx->svc()->Connect(launcher.NewRequest());

  zx::channel req;
  auto services = sys::ServiceDirectory::CreateWithRequest(&req);

  fuchsia::sys::LaunchInfo info;
  info.directory_request = std::move(req);
  info.url = kDevmgrPkgUrl;

  launcher->CreateComponent(std::move(info), ctrl_.NewRequest());
  ctrl_.set_error_handler([](zx_status_t err) { ASSERT_TRUE(false); });

  fuchsia::io::DirectorySyncPtr devfs_dir;
  services->Connect(devfs_dir.NewRequest(), kIsolatedDevmgrServiceName);
  fuchsia::io::NodeInfo node_info;
  zx_status_t status = devfs_dir->Describe(&node_info);
  ASSERT_EQ(ZX_OK, status);

  devfs_dir_ = devfs_dir.Unbind();
}

TEST_F(MockSensorIntegrationTest, SimpleTest) {
  zx::channel c1, c2;
  zx::channel::create(0, &c1, &c2);
  zx_status_t status =
      fdio_service_connect_at(devfs_dir_.channel().get(), "class/camera-sensor/0", c1.release());
  ASSERT_EQ(ZX_OK, status);
}

}  // namespace
