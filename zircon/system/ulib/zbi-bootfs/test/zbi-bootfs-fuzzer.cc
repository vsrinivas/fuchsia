#include <stdlib.h>
#include <string.h>
#include <zircon/errors.h>
#include <zircon/status.h>

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
  zx_status_t status = parser.Init(filename);

  printf("ZbiBootfsParser::Init completed with status: %s\n", zx_status_get_string(status));

  if (status == ZX_OK) {
    zbi_bootfs::Entry entry;
    status = parser.ProcessZbi("file", &entry);
    printf("ZbiBootfsParser::Process completed with status: %s\n", zx_status_get_string(status));
  }

  return 0;
}
