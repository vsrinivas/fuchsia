#include "mold/mold.h"
#include "spinel/spinel.h"
#include <gtest/gtest.h>
TEST(MoldTest, ContextCreationBgra8888) {
  spn_context_t context = {};
  char buffer[16 * 16] = {};
  const mold_raw_buffer raw_buffer = {
     .buffer_ptr = (void**) &buffer,
     .stride = 16,
     .format = MOLD_BGRA8888,
  };

  mold_context_create(&context, &raw_buffer);
  spn_context_release(context);
}
