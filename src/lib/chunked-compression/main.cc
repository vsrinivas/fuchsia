// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <fbl/unique_fd.h>
#include <src/lib/chunked-compression/chunked-compressor.h>
#include <src/lib/chunked-compression/chunked-decompressor.h>
#include <src/lib/chunked-compression/status.h>
#include <src/lib/chunked-compression/streaming-chunked-compressor.h>

namespace {

using chunked_compression::ChunkedCompressor;
using chunked_compression::ChunkedDecompressor;
using chunked_compression::CompressionParams;
using chunked_compression::HeaderReader;
using chunked_compression::SeekTable;
using chunked_compression::StreamingChunkedCompressor;

constexpr const char kAnsiUpLine[] = "\33[A";
constexpr const char kAnsiClearLine[] = "\33[2K\r";

constexpr size_t kTargetFrameSize = 32 * 1024;

// ProgressWriter writes live a progress indicator to stdout. Updates are written in-place
// (using ANSI control codes to rewrite the current line).
class ProgressWriter {
 public:
  explicit ProgressWriter(int refresh_hz = 60) : refresh_hz_(refresh_hz) {
    last_report_ = std::chrono::steady_clock::time_point::min();
    printf("\n");
  }

  void Update(const char* fmt, ...) {
    auto now = std::chrono::steady_clock::now();
    if (now < last_report_ + refresh_duration()) {
      return;
    }
    last_report_ = now;
    printf("%s%s", kAnsiUpLine, kAnsiClearLine);
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    fflush(stdout);
  }

  void Final(const char* fmt, ...) {
    printf("%s%s", kAnsiUpLine, kAnsiClearLine);
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    fflush(stdout);
  }

  std::chrono::steady_clock::duration refresh_duration() const {
    return std::chrono::seconds(1) / refresh_hz_;
  }

 private:
  std::chrono::steady_clock::time_point last_report_;
  int refresh_hz_;
};

void usage(const char* fname) {
  fprintf(stderr, "Usage: %s [--level #] [--stream] [--checksum] (d | c) source dest\n", fname);
  fprintf(stderr,
          "\
  c: Compress source, writing to dest.\n\
  d: Decompress source, writing to dest.\n\
  --stream: (compression only) Use stream compression\n\
  --checksum: (compression only) Include a per-frame checksum\n\
  --level #: Compression level\n");
}

// Opens |file|, truncates to |write_size|, and mmaps the file for writing.
// Returns the mapped buffer in |out_write_buf|, and the managed FD in |out_fd|.
int OpenAndMapForWriting(const char* file, size_t write_size, uint8_t** out_write_buf,
                         fbl::unique_fd* out_fd) {
  fbl::unique_fd fd(open(file, O_RDWR | O_CREAT | O_TRUNC, 0644));
  if (!fd.is_valid()) {
    fprintf(stderr, "Failed to open '%s': %s\n", file, strerror(errno));
    return 1;
  }
  if (ftruncate(fd.get(), write_size)) {
    fprintf(stderr, "Failed to truncate '%s': %s\n", file, strerror(errno));
    return 1;
  }

  void* data = nullptr;
  if (write_size > 0) {
    data = mmap(NULL, write_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd.get(), 0);
    if (data == MAP_FAILED) {
      fprintf(stderr, "mmap failed: %s\n", strerror(errno));
      return 1;
    }
  }

  *out_write_buf = static_cast<uint8_t*>(data);
  *out_fd = std::move(fd);

  return 0;
}

size_t GetFileSize(const char* file) {
  struct stat info;
  if (stat(file, &info) < 0) {
    fprintf(stderr, "stat(%s) failed: %s\n", file, strerror(errno));
    return 0;
  }
  if (!S_ISREG(info.st_mode)) {
    fprintf(stderr, "%s is not a regular file\n", file);
    return 0;
  }
  return info.st_size;
}

// Opens |file| and mmaps the file for reading.
// Returns the mapped buffer in |out_buf|, the size of the file in |out_size|, and the managed FD in
// |out_fd|.
int OpenAndMapForReading(const char* file, fbl::unique_fd* out_fd, const uint8_t** out_buf,
                         size_t* out_size) {
  fbl::unique_fd fd(open(file, O_RDONLY));
  if (!fd.is_valid()) {
    fprintf(stderr, "Failed to open '%s': %s\n", file, strerror(errno));
    return 1;
  }
  size_t size;
  struct stat info;
  if (fstat(fd.get(), &info) < 0) {
    fprintf(stderr, "stat(%s) failed: %s\n", file, strerror(errno));
    return 1;
  }
  if (!S_ISREG(info.st_mode)) {
    fprintf(stderr, "%s is not a regular file\n", file);
    return 1;
  }
  size = info.st_size;

  void* data = nullptr;
  if (size > 0) {
    data = mmap(NULL, size, PROT_READ, MAP_SHARED, fd.get(), 0);
    if (data == MAP_FAILED) {
      fprintf(stderr, "mmap failed: %s\n", strerror(errno));
      return 1;
    }
  }

  *out_fd = std::move(fd);
  *out_buf = static_cast<uint8_t*>(data);
  *out_size = size;

  return 0;
}

// Reads |sz| bytes from |src| and compresses it, writing the output to |dst_file|.
int Compress(const uint8_t* src, size_t sz, const char* dst_file, int level, bool checksum) {
  CompressionParams params;
  params.frame_checksum = checksum;
  params.compression_level = level;
  params.chunk_size = CompressionParams::ChunkSizeForInputSize(sz, kTargetFrameSize);
  size_t output_limit = params.ComputeOutputSizeLimit(sz);
  ChunkedCompressor compressor(params);

  fbl::unique_fd dst_fd;
  uint8_t* write_buf;
  if (OpenAndMapForWriting(dst_file, output_limit, &write_buf, &dst_fd)) {
    return 1;
  }

  ProgressWriter progress;
  compressor.SetProgressCallback([&](size_t bytes_read, size_t bytes_total, size_t bytes_written) {
    progress.Update("%2.0f%% (%lu bytes written)\n",
                    static_cast<double>(bytes_read) / static_cast<double>(bytes_total) * 100,
                    bytes_written);
  });

  size_t compressed_size;
  if (compressor.Compress(src, sz, write_buf, output_limit, &compressed_size) !=
      chunked_compression::kStatusOk) {
    return 1;
  }

  progress.Final("Wrote %lu bytes (%2.0f%% compression)\n", compressed_size,
                 static_cast<double>(compressed_size) / static_cast<double>(sz) * 100);

  ftruncate(dst_fd.get(), compressed_size);
  return 0;
}

// Reads |sz| bytes from |src_fd| and compresses it using a streaming compressor, writing the output
// to |dst_file|.
int CompressStream(fbl::unique_fd src_fd, size_t sz, const char* dst_file, int level,
                   bool checksum) {
  CompressionParams params;
  params.frame_checksum = checksum;
  params.compression_level = level;
  params.chunk_size = CompressionParams::ChunkSizeForInputSize(sz, kTargetFrameSize);
  size_t output_limit = params.ComputeOutputSizeLimit(sz);
  StreamingChunkedCompressor compressor(params);

  fbl::unique_fd dst_fd;
  uint8_t* write_buf;
  if (OpenAndMapForWriting(dst_file, output_limit, &write_buf, &dst_fd)) {
    return 1;
  }

  if (compressor.Init(sz, write_buf, output_limit) != chunked_compression::kStatusOk) {
    fprintf(stderr, "Init failed\n");
    return 1;
  }

  ProgressWriter progress;
  compressor.SetProgressCallback([&](size_t bytes_read, size_t bytes_total, size_t bytes_written) {
    progress.Update("%2.0f%% (%lu bytes written)\n",
                    static_cast<double>(bytes_read) / static_cast<double>(bytes_total) * 100,
                    bytes_written);
  });

  FILE* in = fdopen(src_fd.get(), "r");
  ZX_ASSERT(in != nullptr);
  const size_t chunk_size = 8192;
  uint8_t buf[chunk_size];
  size_t bytes_read = 0;
  for (size_t off = 0; off < sz; off += chunk_size) {
    size_t r = fread(buf, sizeof(uint8_t), chunk_size, in);
    if (r == 0) {
      int err = ferror(in);
      if (err) {
        fprintf(stderr, "fread failed: %s\n", strerror(err));
        return err;
      }
      break;
    }
    if (compressor.Update(buf, r) != chunked_compression::kStatusOk) {
      fprintf(stderr, "Update failed\n");
      return 1;
    }
    bytes_read += r;
  }
  if (bytes_read < sz) {
    fprintf(stderr, "Only read %lu bytes (expected %lu)\n", bytes_read, sz);
  }

  size_t compressed_size;
  if (compressor.Final(&compressed_size) != chunked_compression::kStatusOk) {
    fprintf(stderr, "Final failed\n");
    return 1;
  }

  progress.Final("Wrote %lu bytes (%2.0f%% compression)\n", compressed_size,
                 static_cast<double>(compressed_size) / static_cast<double>(sz) * 100);

  ftruncate(dst_fd.get(), compressed_size);
  return 0;
}

// Reads |sz| bytes from |src| and decompresses them, writing the results to |dst_file|.
int Decompress(const uint8_t* src, size_t sz, const char* dst_file) {
  SeekTable table;
  HeaderReader reader;
  if ((reader.Parse(src, sz, sz, &table)) != chunked_compression::kStatusOk) {
    fprintf(stderr, "Failed to parse input file; not a chunked archive?\n");
    return 1;
  }
  size_t output_size = ChunkedDecompressor::ComputeOutputSize(table);

  fbl::unique_fd dst_fd;
  uint8_t* write_buf;
  if (OpenAndMapForWriting(dst_file, output_size, &write_buf, &dst_fd)) {
    return 1;
  }

  ChunkedDecompressor decompressor;
  size_t bytes_written;
  if (decompressor.Decompress(table, src, sz, write_buf, output_size, &bytes_written) !=
      chunked_compression::kStatusOk) {
    return 1;
  }

  printf("Wrote %lu bytes (%2.0f%% compression)\n", bytes_written,
         static_cast<double>(sz) / static_cast<double>(bytes_written) * 100);
  return 0;
}

}  // namespace

int main(int argc, char* const* argv) {
  bool checksum = false;
  bool stream = false;
  int level = CompressionParams::DefaultCompressionLevel();
  while (1) {
    static struct option opts[] = {
        {"stream", no_argument, nullptr, 's'},
        {"level", required_argument, nullptr, 'l'},
        {"checksum", no_argument, nullptr, 'c'},
        {nullptr, 0, nullptr, 0},
    };
    int c = getopt_long(argc, argv, "sl:c", opts, nullptr);

    if (c < 0) {
      break;
    }
    switch (c) {
      case 'l': {
        char* endp;
        long val = strtol(optarg, &endp, 10);
        if (endp != optarg + strlen(optarg)) {
          usage(argv[0]);
          return 1;
        } else if (level < CompressionParams::MinCompressionLevel() ||
                   level > CompressionParams::MaxCompressionLevel()) {
          fprintf(stderr, "Invalid level %d, should be in range %d <= level <= %d\n", level,
                  CompressionParams::MinCompressionLevel(),
                  CompressionParams::MaxCompressionLevel());
          return 1;
        }
        level = static_cast<int>(val);
        break;
      }
      case 's': {
        stream = true;
        break;
      }
      case 'c': {
        checksum = true;
        break;
      }
      default:
        usage(argv[0]);
        return 1;
    }
  }

  const char* bin_name = argv[0];
  argc -= optind;
  argv += optind;

  if (argc < 3) {
    usage(bin_name);
    return 1;
  }
  const char* mode_str = argv[0];
  const char* input_file = argv[1];
  const char* output_file = argv[2];

  enum Mode {
    COMPRESS,
    DECOMPRESS,
    UNKNOWN,
  };
  Mode mode = Mode::UNKNOWN;
  if (!strcmp(mode_str, "d")) {
    mode = Mode::DECOMPRESS;
  } else if (!strcmp(mode_str, "c")) {
    mode = Mode::COMPRESS;
  } else {
    fprintf(stderr, "Invalid mode (should be 'd' or 'c').\n");
    usage(bin_name);
    return 1;
  }

  if (stream) {
    if (mode == Mode::DECOMPRESS) {
      printf("Ignoring --stream flag for decompression\n");
    } else {
      fbl::unique_fd fd(open(input_file, O_RDONLY));
      if (!fd.is_valid()) {
        fprintf(stderr, "Failed to open '%s': %s\n", input_file, strerror(errno));
        return 1;
      }
      return CompressStream(std::move(fd), GetFileSize(input_file), output_file, level, checksum);
    }
  }
  if (checksum && mode == Mode::DECOMPRESS) {
    printf("Ignoring --checksum flag for decompression\n");
  }

  fbl::unique_fd src_fd;
  const uint8_t* src_data;
  size_t src_size;
  if (OpenAndMapForReading(input_file, &src_fd, &src_data, &src_size)) {
    return 1;
  }

  return mode == Mode::COMPRESS ? Compress(src_data, src_size, output_file, level, checksum)
                                : Decompress(src_data, src_size, output_file);
}
