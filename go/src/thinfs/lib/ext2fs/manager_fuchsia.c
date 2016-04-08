// Copyright 2016 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// +build fuchsia

#include "manager_fuchsia.h"

extern errcode_t fuchsiaOpen(const char *name, int flags, io_channel *channel);
extern errcode_t fuchsiaClose(io_channel channel);
extern errcode_t fuchsiaSetBlockSize(io_channel channel, int blksize);
extern errcode_t fuchsiaReadBlock(io_channel channel, unsigned long block,
                                int count, void *data);
extern errcode_t fuchsiaWriteBlock(io_channel channel, unsigned long block,
                                 int count, const void *data);
extern errcode_t fuchsiaFlush(io_channel channel);
extern errcode_t fuchsiaWriteByte(io_channel channel, unsigned long offset,
                                  int count, const void *data);
extern errcode_t fuchsiaSetOption(io_channel channel, const char *option,
                                  const char *arg);
extern errcode_t fuchsiaGetStats(io_channel channel, io_stats *io_stats);
extern errcode_t fuchsiaReadBlock64(io_channel channel, unsigned long long block,
                                  int count, void *data);
extern errcode_t fuchsiaWriteBlock64(io_channel channel, unsigned long long block,
                                   int count, const void *data);
extern errcode_t fuchsiaDiscard(io_channel channel, unsigned long long block,
                                unsigned long long count);

static struct struct_io_manager struct_fuchsia_manager = {
    .magic = EXT2_ET_MAGIC_IO_MANAGER,
    .name = "Fuchsia I/O Manager",
    .open = fuchsiaOpen,
    .close = fuchsiaClose,
    .set_blksize = fuchsiaSetBlockSize,
    .read_blk = fuchsiaReadBlock,
    .write_blk = fuchsiaWriteBlock,
    .flush = fuchsiaFlush,
    .write_byte = fuchsiaWriteByte,
    .set_option = fuchsiaSetOption,
    .get_stats = fuchsiaGetStats,
    .read_blk64 = fuchsiaReadBlock64,
    .write_blk64 = fuchsiaWriteBlock64,
    .discard = fuchsiaDiscard,
};

io_manager fuchsia_io_manager = &struct_fuchsia_manager;
