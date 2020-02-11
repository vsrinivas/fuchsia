#include <stdlib.h>
#include <string.h>
#include <cstdio>

#include <zbi-bootfs/zbi-bootfs.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  const char* filename = "/data/fuzz";
  FILE* file = fopen(filename, "w");
  if (file == NULL) {
    perror("creating file");
    return 0;
  }

  fwrite(reinterpret_cast<const char*>(data), 1, size, file);
  fclose(file);

  zbi_bootfs::ZbiBootfsParser parser;
  parser.Init(filename, 0);
  return 0;
}
