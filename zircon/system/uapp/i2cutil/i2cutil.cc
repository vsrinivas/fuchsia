// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <fbl/span.h>
#include <fbl/unique_fd.h>
#include <fuchsia/hardware/i2c/llcpp/fidl.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/unsafe.h>
#include <stdio.h>
#include <zircon/status.h>

#include <fbl/auto_call.h>
#include <pretty/hexdump.h>

static void usage(char* prog) {
  printf("Usage:\n");
  printf(" %s w[rite]    DEVICE DATA...                                          Write bytes\n", prog);
  printf(" %s r[ead]     DEVICE ADDRESS                                          Reads one byte\n", prog);
  printf(" %s t[ransact] DEVICE [w|r] [DATA...|LENGTH] [w|r] [DATA...|LENGTH]... Transaction\n", prog);
}

static void convert_args(char** argv, size_t length, uint8_t* buffer) {
  for (size_t i = 0; i < length; i++) {
    buffer[i] = static_cast<uint8_t>(strtoul(argv[i], nullptr, 0));
  }
}

static zx_status_t write_bytes(llcpp::fuchsia::hardware::i2c::Device2::SyncClient client,
                               fbl::Span<uint8_t> write_buffer) {
  bool is_write[] = {true};
  fidl::VectorView<bool> segments_is_write(is_write, countof(is_write));

  fidl::VectorView<uint8_t> write_segment(write_buffer.data(), write_buffer.size());

  auto read = client.Transfer(
    segments_is_write,
    fidl::VectorView<fidl::VectorView<uint8_t>>(&write_segment, 1),  // One write.
    fidl::VectorView<uint8_t>(nullptr, 0));                          // No reads.
  auto status = read.status();
  if (status == ZX_OK && read->result.is_err()) {
    status = ZX_ERR_INTERNAL;
  }
  return status;
}

static zx_status_t read_byte(llcpp::fuchsia::hardware::i2c::Device2::SyncClient client,
                             fbl::Span<uint8_t> address, uint8_t* out_byte) {
  bool is_write[] = {true, false};
  fidl::VectorView<bool> segments_is_write(is_write, countof(is_write));
  fidl::VectorView<uint8_t> write_segment(address.data(), address.size());
  uint8_t read_length = 1;

  auto read = client.Transfer(
    segments_is_write,
    fidl::VectorView<fidl::VectorView<uint8_t>>(&write_segment, 1),  // One write.
    fidl::VectorView<uint8_t>(&read_length, 1));                     // One read.
  auto status = read.status();
  if (status == ZX_OK) {
    if (read->result.is_err()) {
      status = ZX_ERR_INTERNAL;
    } else {
      *out_byte = read->result.response().read_segments_data[0].data()[0];
    }
  }
  return status;
}

static zx_status_t transact(llcpp::fuchsia::hardware::i2c::Device2::SyncClient client,
                            int argc, char** argv) {
  size_t n_elements = argc - 3;
  size_t n_segments = 0;
  size_t n_writes = 0;
  // We know n_segments and total data will be smaller than n_elements, so we use it as max.
  auto segment_start = std::make_unique<size_t[]>(n_elements);
  auto writes_start = std::make_unique<size_t[]>(n_elements);
  auto write_buffer = std::make_unique<uint8_t[]>(n_elements);

  // Find n_segments, segment starts and writes starts.
  for (size_t i = 0; i < n_elements; ++i) {
    if (argv[3 + i][0] == 'r') {
      segment_start[n_segments++] = i + 1;
    } else if (argv[3 + i][0] == 'w') {
      segment_start[n_segments++] = i + 1;
      writes_start[n_writes++] = i + 1;
    }
  }

  // Must have at least one segment and start with a w or r.
  if (n_segments == 0 || (argv[3][0] != 'r' && argv[3][0] != 'w')) {
    usage(argv[0]);
    return -1;
  }
  if (n_segments > llcpp::fuchsia::hardware::i2c::MAX_COUNT_SEGMENTS) {
    printf("No more than %u segments allowed\n",
           llcpp::fuchsia::hardware::i2c::MAX_COUNT_SEGMENTS);
    return -1;
  }

  // For the last segment we pretend that data starts after a pretend w/r, this makes
  // calculations below consistent for the last actual segment without a segment to follow.
  segment_start[n_segments] = n_elements + 1;

  auto write_data = std::make_unique<fidl::VectorView<uint8_t>[]>(n_writes);
  auto read_lengths = std::make_unique<uint8_t[]>(n_segments - n_writes);
  auto is_write = std::make_unique<bool[]>(n_segments);
  fidl::VectorView<bool> segments_is_write(is_write.get(), n_segments);

  size_t element_cnt = 0;
  size_t segment_cnt = 0;
  size_t read_cnt = 0;
  size_t write_cnt = 0;
  uint8_t* write_buffer_pos = write_buffer.get();
  while (element_cnt < n_elements) {
    if (argv[3 + element_cnt][0] == 'w') {
      is_write[segment_cnt] = true;
      element_cnt++;
    } else if (argv[3 + element_cnt][0] == 'r') {
      is_write[segment_cnt] = false;
      element_cnt++;
    } else {
      if (is_write[segment_cnt]) {
        auto write_len = segment_start[segment_cnt + 1] - segment_start[segment_cnt] - 1;
        convert_args(&argv[3 + element_cnt], write_len, write_buffer_pos);
        write_data[write_cnt].set_data(write_buffer_pos);
        write_data[write_cnt].set_count(write_len);
        write_buffer_pos += write_len;
        write_cnt++;
        element_cnt += write_len;
      } else {
        convert_args(&argv[3 + element_cnt], 1, &read_lengths[read_cnt]);
        read_cnt++;
        element_cnt++;
      }
      segment_cnt++;
    }
  }
  ZX_ASSERT(write_cnt == n_writes);
  ZX_ASSERT(read_cnt + write_cnt == segment_cnt);

  auto read =
    client.Transfer(segments_is_write,
                    fidl::VectorView<fidl::VectorView<uint8_t>>(write_data.get(), n_writes),
                    fidl::VectorView<uint8_t>(read_lengths.get(), n_segments - n_writes));
  auto status = read.status();
  if (status == ZX_OK) {
    if (read->result.is_err()) {
      status = ZX_ERR_INTERNAL;
    } else {
      auto read_data = read->result.response().read_segments_data;
      for (auto& i : read_data) {
        hexdump8_ex(i.data(), i.count(), 0);
      }
    }
  }
  return status;
}

static int device_cmd(int argc, char** argv) {
  if (argc < 3) {
    usage(argv[0]);
    return -1;
  }

  fbl::unique_fd fd(open(argv[2], O_RDWR));
  if (!fd) {
    printf("%s: %s\n", argv[2], strerror(errno));
    usage(argv[0]);
    return -1;
  }

  zx_handle_t svc;
  if ((fdio_get_service_handle(fd.release(), &svc) != ZX_OK)) {
    printf("%s: get service handle failed\n", argv[2]);
    usage(argv[0]);
    return -1;
  }

  zx::channel channel(svc);
  llcpp::fuchsia::hardware::i2c::Device2::SyncClient client(std::move(channel));

  zx_status_t status = ZX_OK;

  switch (argv[1][0]) {
    case 'w': {
      if (argc < 4) {
        usage(argv[0]);
        return -1;
      }

      size_t n_write_bytes = argc - 3;
      auto write_buffer = std::make_unique<uint8_t[]>(n_write_bytes);
      convert_args(&argv[3], n_write_bytes, write_buffer.get());

      status = write_bytes(std::move(client),
                           fbl::Span<uint8_t>(write_buffer.get(), n_write_bytes));
      break;
    }

    case 'r': {
      if (argc < 4) {
        usage(argv[0]);
        return -1;
      }

      size_t n_write_bytes = argc - 3;
      auto write_buffer = std::make_unique<uint8_t[]>(n_write_bytes);
      convert_args(&argv[3], n_write_bytes, write_buffer.get());

      uint8_t out_byte = 0;
      status = read_byte(std::move(client),
                         fbl::Span<uint8_t>(write_buffer.get(), n_write_bytes),
                         &out_byte);
      if (status == ZX_OK) {
        hexdump8_ex(&out_byte, 1, 0);
      }
      break;
    }

    case 't': {
      if (argc < 5) {
        usage(argv[0]);
        return -1;
      }

      status = transact(std::move(client), argc, argv);
      break;
    }

    default:
      printf("%c: unrecognized command\n", argv[2][0]);
      usage(argv[0]);
      return -1;
  }
  if (status == ZX_OK) {
    printf("Success\n");
  } else {
    printf("Error %d\n", status);
  }
  return status;
}

int main(int argc, char** argv) {
  if (argc < 2) {
    usage(argv[0]);
    return -1;
  }
  switch (argv[1][0]) {
    case 'w':
    case 'r':
    case 't':
      return device_cmd(argc, argv);
      break;

  default:
      usage(argv[0]);
      return -1;
  }

  return 0;
}
