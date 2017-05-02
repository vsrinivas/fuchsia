#include "application/lib/app/application_context.h"
#include "gtest/gtest.h"
#include "lib/mtl/tasks/message_loop.h"

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  mtl::MessageLoop message_loop;

  auto application_context = app::ApplicationContext::CreateFromStartupInfo();

  return RUN_ALL_TESTS();
}
