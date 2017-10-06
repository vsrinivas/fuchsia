// Copyright 2017 The Fuchsia Authors. All rights reserved.
// User of this source code is governed by a BSD-style license that be be found
// in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <installer/sparse.h>
#include <unittest/unittest.h>

#define DATA_SZ (312 * 1024 + 3)
#define PATH_MAX 4096
#define FOUR_K (4 * 1024)

int make_tmp_file(char *name_buf, int sz) {
  snprintf(name_buf, sz, "/tmp/%i", rand());
  return open(name_buf, O_RDWR | O_TRUNC | O_CREAT);
}

void make_rand_data(uint8_t *buf, int sz) {
  int odds = sz % sizeof(int);
  int* p = (int*) buf;
  for (;sz > odds; sz -= sizeof(int), p++) {
    int r = rand();
    memcpy(p, &r, sizeof(r));
  }
  int r = rand();
  memcpy(buf + sz - odds, &r, odds);
}

int create_test_data_and_file(uint8_t **buf_out, int buf_sz, char* name_buf_out,
                              int name_max) {

  int f = make_tmp_file(name_buf_out, name_max);
  ASSERT_GT(f, -1, "Failed to make temp file");

  uint8_t *data = malloc(buf_sz);
  ASSERT_NE(data, NULL, "Memory allocation failed");
  *buf_out = data;
  return f;
}

bool test_readn(void) {
  BEGIN_TEST;
  char name_buf[PATH_MAX];
  uint8_t *file_data;
  int fd = create_test_data_and_file(&file_data, DATA_SZ, name_buf, PATH_MAX);

  ASSERT_EQ(write(fd, file_data, DATA_SZ), (ssize_t) DATA_SZ,
            "Error writing test file");
  ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0, "Error seeking to front of file");

  size_t first_chunk = 11 * 1024;
  ASSERT_GT(DATA_SZ, first_chunk, "First chunk should be smaller than second.");
  size_t second_chunk = DATA_SZ - first_chunk;
  uint8_t *read_data = malloc(second_chunk);

  ASSERT_EQ(readn(fd, (void*) read_data, first_chunk), (ssize_t) first_chunk,
            "Read of first chunk failed.");
  ASSERT_EQ(memcmp(file_data, read_data, first_chunk), 0,
            "Read data does not match written data");

  ASSERT_EQ(readn(fd, read_data, second_chunk), (ssize_t) second_chunk,
            "Read of second chunk failed");
  ASSERT_EQ(memcmp(file_data + first_chunk, read_data, second_chunk), 0,
            "Second batch of data doesn't match");

  free(file_data);
  free(read_data);
  close(fd);
  remove(name_buf);
  END_TEST;
}

bool test_writen(void) {
  BEGIN_TEST;
  char name_buf[PATH_MAX];
  uint8_t *file_data;
  int fd = create_test_data_and_file(&file_data, DATA_SZ, name_buf, PATH_MAX);

  uint8_t *read_data = malloc(DATA_SZ);
  ASSERT_NE(read_data, NULL, "Couldn't allocate memory for read data");

  ASSERT_EQ(writen(fd, file_data, DATA_SZ), DATA_SZ,
            "File write output len not correct.");

  struct stat f_info;
  ASSERT_EQ(fstat(fd, &f_info), 0, "Unable to stat file");
  ASSERT_EQ(f_info.st_size, DATA_SZ, "File size is incorrect.");

  ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0, "Error seeking to front of file");
  ASSERT_EQ(readn(fd, read_data, DATA_SZ), DATA_SZ,
            "File read size is not correct.");
  ASSERT_EQ(memcmp(file_data, read_data, DATA_SZ), 0,
            "Data read back from file does not match input");

  free(read_data);
  free(file_data);
  close(fd);
  remove(name_buf);
  END_TEST;
}

bool test_copyn(void) {
  BEGIN_TEST;
  char name_src[PATH_MAX];
  char name_dst[PATH_MAX];
  uint8_t *file_data;
  const int copy_buf_sz = 8 * 1024;

  int fd = create_test_data_and_file(&file_data, DATA_SZ, name_src, PATH_MAX);

  uint8_t *read_data = malloc(DATA_SZ);
  ASSERT_NE(read_data, NULL, "Couldn't allocate memory for read data");
  uint8_t *copy_buffer = malloc(copy_buf_sz);
  ASSERT_NE(copy_buffer, NULL, "Couldn't allocate memory for copy buffer.");

  int dst = make_tmp_file(name_dst, PATH_MAX);
  ASSERT_GT(dst, -1, "Couldn't create destination file.");

  ASSERT_EQ(writen(fd, file_data, DATA_SZ), DATA_SZ,
            "File output length not correct.");
  ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0, "Error seeking to front of file");

  struct stat f_info;
  ASSERT_EQ(fstat(fd, &f_info), 0, "Unable to stat file");
  ASSERT_EQ(f_info.st_size, DATA_SZ, "File size is incorrect.");

  ASSERT_EQ(copyn(fd, dst, DATA_SZ, copy_buffer, copy_buf_sz), DATA_SZ,
            "Bytes copied not expected.");
  ASSERT_EQ(fstat(dst, &f_info), 0, "Unable to stat file");
  ASSERT_EQ(f_info.st_size, DATA_SZ, "File size is incorrect.");

  ASSERT_EQ(lseek(dst, 0, SEEK_SET), 0, "Error seeking to front of file");

  ASSERT_EQ(readn(dst, read_data, DATA_SZ), DATA_SZ,
            "Incorrect number of bytes read back from destination file.");
  ASSERT_EQ(memcmp(read_data, file_data, DATA_SZ), 0,
            "Data read back from copied file does not match!");
  free(read_data);
  free(file_data);
  free(copy_buffer);
  close(fd);
  close(dst);
  remove(name_src);
  remove(name_dst);
  END_TEST;
}

int make_src_dst_and_buffers(int *src_out, char *name_src_out, int *dst_out,
                             char *name_dst_out, int name_max,
                             uint8_t **data_buf_out, int data_buf_sz,
                             uint8_t **read_buf_out, int read_buf_sz,
                             uint8_t **cpy_buf_out, int cpy_buf_sz) {
  int fd = make_tmp_file(name_src_out, name_max);
  ASSERT_GT(fd, -1, "Temporary file creation failed.");
  *src_out = fd;

  fd = make_tmp_file(name_dst_out, name_max);
  ASSERT_GT(fd, -1, "Temporary file creation failed.");
  *dst_out = fd;

  *data_buf_out = malloc(data_buf_sz);
  ASSERT_NE(data_buf_out, NULL, "Couldn't allocate for buffer");

  *read_buf_out = malloc(read_buf_sz);
  ASSERT_NE(read_buf_out, NULL, "Couldn't allocate for buffer");

  *cpy_buf_out = malloc(cpy_buf_sz);
  ASSERT_NE(cpy_buf_out, NULL, "Couldn't allocate for buffer");

  return 0;
}

int build_sample_chunk_list(chunk_t *chunks) {
    // create a sparse file layout as a series of data sizes and hole sizes
  uint32_t hole_sizes[3] = {FOUR_K, FOUR_K * 3, FOUR_K};
  uint32_t blank_space = 0;
  for (int i = -1; ++i < 3; blank_space += hole_sizes[i]);

  uint32_t lengths[4] = {DATA_SZ / 6, 0, DATA_SZ / 6 * 2, DATA_SZ /6};
  lengths[1] = DATA_SZ - lengths[0] - lengths[2] - lengths[3] - blank_space;
  uint32_t data_space = 0;
  for (int i = -1; ++i < 4; data_space += lengths[i]);

  ASSERT_EQ(DATA_SZ, data_space + blank_space, "Error creating file map.");

  // compose data and hole sizes into a list of chunk_t descriptors
  chunks[0].start = 0;
  chunks[0].len = lengths[0];

  for (int i = 0; i < 3; ++i) {
    chunks[i+1].start = chunks[i].start + chunks[i].len + hole_sizes[i];
    chunks[i+1].len = lengths[i];
  }

  chunks[4].start = 0;
  chunks[4].len = data_space + blank_space;
  return 0;
}

bool test_unsparse_no_holes(void) {
  BEGIN_TEST;
  char name_src[PATH_MAX];
  char name_dst[PATH_MAX];
  uint8_t *file_data;
  const int copy_buf_sz = 8 * 1024;
  uint8_t *read_data;
  uint8_t *copy_buffer;
  int src;
  int dst;

  ASSERT_EQ(0, make_src_dst_and_buffers(&src, name_src, &dst, name_dst,
                                        PATH_MAX, &file_data,
                                        DATA_SZ, &read_data, DATA_SZ,
                                        &copy_buffer, copy_buf_sz),
            "Failure initializing test data.");
  chunk_t header;
  header.start = 0;
  header.len = DATA_SZ;

  ASSERT_EQ(write(src, &header, sizeof(header)), sizeof(header),
            "Couldn't write header to sparsed file");
  ASSERT_EQ(writen(src, file_data, DATA_SZ), DATA_SZ,
            "File output length not correct.");
  ASSERT_EQ(write(src, &header, sizeof(header)), sizeof(header),
            "Couldn't write end header to sparsed file.");
  ASSERT_EQ(lseek(src, 0, SEEK_SET), 0, "Couldn't seek to beginning of file.");

  ASSERT_EQ(unsparse(src, dst, copy_buffer, copy_buf_sz), 0,
            "Unsparsing of file failed.");
  ASSERT_EQ(lseek(dst, 0, SEEK_SET), 0, "Rewinding destination file failed.");
  ASSERT_EQ(close(dst), 0, "Error closing destination file.");
  dst = open(name_dst, O_RDONLY);
  ASSERT_GT(dst, -1, "Error re-opening output file.");

  struct stat dst_info;
  ASSERT_EQ(fstat(dst, &dst_info), 0, "fstat of output failed");
  ASSERT_EQ(dst_info.st_size, DATA_SZ, "Size of unsparsed file doesn't match");

  ASSERT_EQ(readn(dst, read_data, DATA_SZ), DATA_SZ,
            "Size of read data is unexpected.");
  ASSERT_EQ(memcmp(read_data, file_data, DATA_SZ), 0,
            "Contents of unsparsed file did not match!");

  free(read_data);
  free(file_data);
  free(copy_buffer);
  close(src);
  close(dst);
  remove(name_src);
  remove(name_dst);

  END_TEST;
}

bool test_unsparse_holes(void) {
  BEGIN_TEST;
  char name_src[PATH_MAX];
  char name_dst[PATH_MAX];
  uint8_t *file_data;
  const int copy_buf_sz = 8 * 1024;
  uint8_t *read_data;
  uint8_t *copy_buffer;

  int src = make_tmp_file(name_src, PATH_MAX);
  ASSERT_GT(src, -1, "Creation of temporary file failed.");
  int dst = make_tmp_file(name_dst, PATH_MAX);
  ASSERT_GT(dst, -1, "Creation of temporary file failed.");

  chunk_t sects[5];
  build_sample_chunk_list(sects);

  file_data = malloc(DATA_SZ);
  ASSERT_NE(file_data, NULL, "Memory allocation failed");
  memset(file_data, 0, DATA_SZ);
  copy_buffer = malloc(copy_buf_sz);
  ASSERT_NE(copy_buffer, NULL, "Memory allocation failed");
  read_data = malloc(DATA_SZ);
  ASSERT_NE(read_data, NULL, "Memory allocation failed");

  // write chunk descriptors to file and create an in-memory copy of the
  // unsparsed data
  for (int i = 0; i < 4; ++i) {
    ASSERT_EQ(writen(src, &sects[i], sizeof(chunk_t)), sizeof(chunk_t),
              "Couldn't write chunk data to sparsed file.");
    make_rand_data(file_data + sects[i].start, sects[i].len);
    ASSERT_EQ(writen(src, file_data + sects[i].start, sects[i].len),
              (ssize_t) sects[i].len, "Write to source file failed.");
  }

  ASSERT_EQ(writen(src, &sects[4], sizeof(chunk_t)), sizeof(chunk_t),
            "Write of last chunk to source file failed.");

  ASSERT_EQ(lseek(src, 0, SEEK_SET), 0, "Source file rewind failed.");
  ASSERT_EQ(unsparse(src, dst, copy_buffer, copy_buf_sz), 0,
            "Failed when unsparsing file.");

  struct stat dst_info;
  ASSERT_EQ(fstat(dst, &dst_info), 0, "fstat of output failed");
  ASSERT_EQ(dst_info.st_size, DATA_SZ, "Size of unsparsed file doesn't match");

  ASSERT_EQ(lseek(dst, 0, SEEK_SET), 0, "Destination file rewind failed.");
  ASSERT_EQ(readn(dst, read_data, DATA_SZ), DATA_SZ,
            "Read back of file data head unexpected size");
  ASSERT_EQ(memcmp(file_data, read_data, DATA_SZ), 0,
            "Unsparsed file does not match in-memory copy.");

  close(src);
  close(dst);
  remove(name_src);
  remove(name_dst);
  free(copy_buffer);
  free(read_data);
  free(file_data);
  END_TEST;
}

bool unsparse_in_pieces(uint8_t *src_buf, uint8_t *read_buf, uint8_t *verify_buf,
                        size_t verify_sz, size_t *pieces, uint8_t piece_count,
                        int dst) {
  memset(read_buf, 0, verify_sz);
  unsparse_ctx_t context;
  init_unsparse_ctx(&context);

  size_t decomp_count = 0;

  for (int i = 0; i < piece_count; ++i) {
    ASSERT_EQ(unsparse_buf(src_buf + decomp_count, pieces[i], &context, dst),
              (ssize_t) pieces[i],
              "Unexpected amount of data drea during decompression");
    decomp_count += pieces[i];
  }
  ASSERT_EQ(lseek(dst, 0, SEEK_SET), 0, "Rewinding destination file failed.");

  struct stat f_info;
  ASSERT_EQ(fstat(dst, &f_info), 0, strerror(errno));
  ASSERT_EQ(f_info.st_size, (ssize_t) verify_sz,
            "Output file is of unexpected size");
  ASSERT_EQ(readn(dst, read_buf, verify_sz), (ssize_t) verify_sz,
            "Read unexpected amount of data from output file.");
  ASSERT_EQ(memcmp(read_buf, verify_buf, verify_sz), 0,
            "Data read back form file does not match original.");

  // reset conditions
  ASSERT_EQ(lseek(dst, 0, SEEK_SET), 0, "Rewinding destination file failed.");
  ASSERT_EQ(ftruncate(dst, 0), 0, "Couldn't truncate destination file.");
  return true;
}

bool test_unsparse_buf_no_holes(void) {
  BEGIN_TEST;
  char name_dst[PATH_MAX];
  uint8_t *file_data;
  //const int copy_buf_sz = 8 * 1024;
  uint8_t *read_data;
  //uint8_t *copy_buffer;
  int dst = make_tmp_file(name_dst, PATH_MAX);
  ASSERT_GT(dst, -1, "Failed to create temporary output file.");

  uint32_t d_sz = DATA_SZ + sizeof(chunk_t) * 2;
  file_data = malloc(d_sz);
  ASSERT_NE(file_data, NULL, "Memory allocation for source data failed.");
  read_data = malloc(DATA_SZ);
  ASSERT_NE(read_data, NULL, "Memory allocation for destination data failed.");

  chunk_t header;
  header.start = 0;
  header.len = DATA_SZ;

  // create in-memory representation of sparsed file
  memcpy(file_data, &header, sizeof(header));
  make_rand_data(file_data + sizeof(header), DATA_SZ);
  memcpy(file_data + sizeof(header) + DATA_SZ, &header, sizeof(header));

  // feed the data to the decompressor in various ways

  // give the whole file in one chunk
  size_t pieces[2];
  pieces[0] = d_sz;
  unsparse_in_pieces(file_data, read_data, file_data + sizeof(header), DATA_SZ,
                     pieces, 1, dst);

  // give only part of the header, then the rest
  pieces[0] = sizeof(header) / 2;
  pieces[1] = d_sz - pieces[0];
  unsparse_in_pieces(file_data, read_data, file_data + sizeof(header), DATA_SZ,
                     pieces, 2, dst);

  // give the header and some of the data
  pieces[0] = sizeof(header) + DATA_SZ / 2;
  pieces[1] = d_sz - pieces[0];
  unsparse_in_pieces(file_data, read_data, file_data + sizeof(header), DATA_SZ,
                     pieces, 2, dst);

  // give everything but half the final header
  pieces[0] = d_sz - sizeof(header) / 2;
  pieces[1] = d_sz - pieces[0];
  unsparse_in_pieces(file_data, read_data, file_data + sizeof(header), DATA_SZ,
                     pieces, 2, dst);

  free(read_data);
  free(file_data);
  close(dst);
  remove(name_dst);
  END_TEST;
}

bool test_unsparse_buf_holes(void) {
  BEGIN_TEST;
  chunk_t chunks[5];
  build_sample_chunk_list(chunks);

  char name_dst[PATH_MAX];
  int dst;
  uint8_t *orig_data;
  uint8_t *unsparse_data;
  uint8_t *sparse_data;
  uint8_t *read_data;

  orig_data = malloc(DATA_SZ);
  ASSERT_NE(orig_data, NULL, "Memory allocation failed.");
  read_data = malloc(DATA_SZ);
  ASSERT_NE(read_data, NULL, "Memory allocation failed.");

  unsparse_data = malloc(chunks[4].len);
  ASSERT_NE(unsparse_data, NULL, "Memory allocation failed.");
  memset(unsparse_data, 0, chunks[4].len);

  uint32_t sparsed_sz = DATA_SZ + sizeof(chunk_t) * 5;
  sparse_data = malloc(sparsed_sz);
  ASSERT_NE(sparse_data, NULL, "Memory allocation failed.");

  // generate random data and punch holes in it
  make_rand_data(orig_data, DATA_SZ);
  uint32_t bytes_written = 0;
  for (int i = 0; i < 4; ++i) {
    memcpy(unsparse_data + chunks[i].start, orig_data + bytes_written,
           chunks[i].len);
    bytes_written += chunks[i].len;
  }

  // write a sparse representation of the original data
  bytes_written = 0;
  uint32_t bytes_read = 0;
  for (int i = 0; i < 4; ++i) {
    memcpy(sparse_data + bytes_written, &chunks[i], sizeof(chunk_t));
    bytes_written += sizeof(chunk_t);
    memcpy(sparse_data + bytes_written, orig_data + bytes_read,
           chunks[i].len);
    bytes_written += chunks[i].len;
    bytes_read += chunks[i].len;
  }
  free(orig_data);

  dst = make_tmp_file(name_dst, PATH_MAX);
  //unsparse_ctx_t context;
  // feed the decompressor in various ways

  // try the whole file at once
  size_t pieces[3];
  pieces[0] = sparsed_sz;
  unsparse_in_pieces(sparse_data, read_data, unsparse_data, chunks[4].len,
                     pieces, 1, dst);

  // try first chunk header and some data, then the remainder of the file
  pieces[0] = sizeof(chunk_t) + chunks[0].len / 2;
  pieces[1] = sparsed_sz - pieces[0];
  unsparse_in_pieces(sparse_data, read_data, unsparse_data, chunks[4].len,
                     pieces, 2, dst);

  // try just the first chunk header, then the rest of the file
  pieces[0] = sizeof(chunk_t);
  pieces[1] = sparsed_sz - pieces[0];
  unsparse_in_pieces(sparse_data, read_data, unsparse_data, chunks[4].len,
                     pieces, 2, dst);

  // try just the first chunk header, the first data section plus part of the
  // next header, then the remainder of the file
  pieces[0] = sizeof(chunk_t);
  pieces[1] = chunks[0].len + sizeof(chunk_t) / 2;
  pieces[2] = sparsed_sz - pieces[0] - pieces[1];
  unsparse_in_pieces(sparse_data, read_data, unsparse_data, chunks[4].len,
                     pieces, 3, dst);

  // try everything but then the final header
  pieces[0] = sparsed_sz - sizeof(chunk_t);
  pieces[1] = sparsed_sz - pieces[0];
  unsparse_in_pieces(sparse_data, read_data, unsparse_data, chunks[4].len,
                     pieces, 2, dst);

  close(dst);
  remove(name_dst);
  free(unsparse_data);
  free(sparse_data);
  free(read_data);
  END_TEST;
}

BEGIN_TEST_CASE(sparse_tests)
RUN_TEST(test_readn)
RUN_TEST(test_writen)
RUN_TEST(test_copyn)
RUN_TEST(test_unsparse_no_holes)
RUN_TEST(test_unsparse_holes)
RUN_TEST(test_unsparse_buf_no_holes)
RUN_TEST(test_unsparse_buf_holes)
END_TEST_CASE(sparse_tests)

int main(int argc, char **argv) {
  srand(time(NULL));
  return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
