#ifndef SRC_SECURITY_SCRUTINY_UTILS_FFI_BRIDGE_CHUNKED_DECOMPRESSOR_H_
#define SRC_SECURITY_SCRUTINY_UTILS_FFI_BRIDGE_CHUNKED_DECOMPRESSOR_H_

#include <cstddef>

// FFI for ChunkedDecompressor::DecompressBytes
// Acts as a C bridge to the C++ ChunkedDecompress function to enable it to
// be used by the rust FFI.
extern "C" size_t zstd_chunked_decompress(const void* src, size_t src_len, void* dst,
                                          size_t dst_capacity);

#endif  // SRC_SECURITY_SCRUTINY_UTILS_FFI_BRIDGE_CHUNKED_DECOMPRESSOR_H_
