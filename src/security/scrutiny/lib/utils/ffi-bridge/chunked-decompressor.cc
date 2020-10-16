#include "chunked-decompressor.h"

#include <fbl/array.h>
#include <src/lib/chunked-compression/chunked-decompressor.h>
#include <src/lib/chunked-compression/status.h>

size_t zstd_chunked_decompress(const void* src, size_t src_len, void* dst, size_t dst_capacity) {
  using namespace chunked_compression;
  SeekTable seek_table;
  HeaderReader reader;
  Status status = reader.Parse(src, src_len, dst_capacity, &seek_table);
  if (status != kStatusOk) {
    return 0;
  }
  ChunkedDecompressor decompressor;
  size_t uncompressed_size = 0;
  status = decompressor.Decompress(seek_table, src, src_len, dst, dst_capacity, &uncompressed_size);
  if (status != kStatusOk) {
    return 0;
  }
  return uncompressed_size;
}
