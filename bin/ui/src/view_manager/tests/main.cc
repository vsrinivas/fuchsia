#include "application/lib/app/application_context.h"
#include "gtest/gtest.h"
#include "lib/mtl/tasks/message_loop.h"
#include "apps/mozart/services/views/view_manager.fidl.h"

mozart::ViewManagerPtr g_view_manager;

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  mtl::MessageLoop message_loop;

  auto application_context = app::ApplicationContext::CreateFromStartupInfo();
  auto view_manager =
      application_context->ConnectToEnvironmentService<mozart::ViewManager>();
  g_view_manager = mozart::ViewManagerPtr::Create(std::move(view_manager));

  return RUN_ALL_TESTS();
}
