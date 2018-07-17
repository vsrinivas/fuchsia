#include <lib/async-loop/cpp/loop.h>
#include "display_manager_impl.h"

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  display::DisplayManagerImpl manager;
  loop.Run();
  return 0;
}