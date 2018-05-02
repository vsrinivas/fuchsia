// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <fcntl.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include <block-client/client.h>
#include <zircon/device/block.h>
#include <zircon/misc/xorshiftrand.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/threads.h>

#define BLOCK_HEADER 0xdeadbeef

static uint64_t base_seed;
static size_t block_size;
static fifo_client_t* client;
static block_info_t info;
static int num_threads;

static uint32_t start_block;
static uint32_t block_count;
static int fd;
static atomic_int_fast32_t next_txid;

static mtx_t lock;
static uint32_t blocks_read;
static bool iochk_failure;

static void generate_block_data(int blk_idx, void* buffer, size_t len) {
    // Block size should be a multiple of sizeof(uint64_t), but assert just to be safe
    assert(len % sizeof(uint64_t) == 0);

    rand64_t seed_gen = RAND63SEED(base_seed + blk_idx);
    for (int i = 0; i < 10; i++) {
        rand64(&seed_gen);
    }
    rand64_t data_gen = RAND63SEED(rand64(&seed_gen));

    uint64_t* buf = buffer;
    int idx = 0;
    uint64_t data = BLOCK_HEADER | (((uint64_t) blk_idx) << 32);

    while (idx < (int) (len / sizeof(uint64_t))) {
        buf[idx] = data;
        data = rand64(&data_gen);
        idx++;
    }
}

static int fill_range(groupid_t group, zx_handle_t vmoid, uint32_t start,
                      uint32_t count, void* buf) {
    zx_status_t st;
    for (uint32_t blk_idx = start; blk_idx < count; blk_idx++) {
        uint64_t len = (info.block_size * info.block_count) - (blk_idx * block_size);
        if (len > block_size) {
            len = block_size;
        }

        generate_block_data(blk_idx, buf, block_size);
        block_fifo_request_t request = {
            .group = group,
            .vmoid = vmoid,
            .opcode = BLOCKIO_WRITE,
            .length = len / info.block_size,
            .vmo_offset = 0,
            .dev_offset = (blk_idx * block_size) / info.block_size,
        };
        if ((st = block_fifo_txn(client, &request, 1)) != ZX_OK) {
            fprintf(stderr, "error: write block_fifo_txn error %d\n", st);
            return -1;
        }
    }
    return 0;
}

static int check_block_data(int blk_idx, void* buffer, int len) {
    rand64_t seed_gen = RAND63SEED(base_seed + blk_idx);
    for (int i = 0; i < 10; i++) {
        rand64(&seed_gen);
    }
    rand64_t data_gen = RAND63SEED(rand64(&seed_gen));

    uint64_t* buf = buffer;
    uint64_t expected = BLOCK_HEADER | (((uint64_t) blk_idx) << 32);
    int idx = 0;

    while (idx < (int) (len / sizeof(uint64_t))) {
        if (buf[idx] != expected) {
            fprintf(stderr, "error: inital read verification failed: "
                   "blk_idx=%d offset=%d expected=0x%016lx val=0x%016lx\n",
                   blk_idx, idx, expected, buf[idx]);
            return -1;
        }
        idx++;
        expected = rand64(&data_gen);
    }
    return 0;
}

static zx_status_t check_range(groupid_t group, zx_handle_t vmoid, uint32_t start,
                               uint32_t count, void* buf) {
    zx_status_t st;
    for (uint32_t blk_idx = start; blk_idx < count; blk_idx++) {
        uint64_t len = (info.block_size * info.block_count) - (blk_idx * block_size);
        if (len > block_size) {
            len = block_size;
        }

        block_fifo_request_t request = {
            .group = group,
            .vmoid = vmoid,
            .opcode = BLOCKIO_READ,
            .length = len / info.block_size,
            .vmo_offset = 0,
            .dev_offset = (blk_idx * block_size) / info.block_size,
        };
        if ((st = block_fifo_txn(client, &request, 1)) != ZX_OK) {
            fprintf(stderr, "error: read block_fifo_txn error %d\n", st);
            return -1;
        }
        if (check_block_data(blk_idx, buf, len)) {
            return -1;
        }
    }
    return 0;
}

static int init_txn_resources(zx_handle_t* vmo, vmoid_t* vmoid, groupid_t* group,  void** buf) {
    if (zx_vmo_create(block_size, 0, vmo) != ZX_OK) {
        fprintf(stderr, "error: out of memory\n");
        return -1;
    }

    if (zx_vmar_map(zx_vmar_root_self(), 0, *vmo, 0, block_size,
                ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE, (uintptr_t*) buf) != ZX_OK) {
        fprintf(stderr, "error: failed to map vmo\n");
        return -1;
    }

    zx_handle_t dup;
    if (zx_handle_duplicate(*vmo, ZX_RIGHT_SAME_RIGHTS, &dup) != ZX_OK) {
        fprintf(stderr, "error: cannot duplicate handle\n");
        return -1;
    }

    size_t s;
    if ((s = ioctl_block_attach_vmo(fd, &dup, vmoid) != sizeof(vmoid_t))) {
        fprintf(stderr, "error: cannot attach vmo for init %lu\n", s);
        return -1;
    }

    *group = atomic_fetch_add(&next_txid, 1);
    assert(*group < MAX_TXN_GROUP_COUNT);

    return 0;
}

static void free_txn_resources(zx_handle_t vmo, void** buf) {
    zx_handle_close(vmo);
    zx_vmar_unmap(zx_vmar_root_self(), *(uintptr_t*) buf, block_size);
}

static int init_device(void) {
    zx_handle_t vmo;
    void* buf;
    vmoid_t vmoid;
    groupid_t group;

    if (init_txn_resources(&vmo, &vmoid, &group, &buf)) {
        fprintf(stderr, "Failed to alloc resources to init device\n");
        return -1;
    }

    zx_status_t st;
    printf("writing test data to device...");
    fflush(stdout);
    if ((st = fill_range(group, vmoid, start_block, block_count, buf)) != ZX_OK) {
        fprintf(stderr, "failed to write test data\n");
        goto fail;
    }
    printf("done\n");

    printf("verifying test data...");
    fflush(stdout);
    if (check_range(group, vmoid, start_block, block_count, buf)) {
        fprintf(stderr, "failed to verify test data\n");
        goto fail;
    }
    printf("done\n");

    free_txn_resources(vmo, &buf);
    return 0;

fail:
    free_txn_resources(vmo, &buf);
    return -1;
}

static void update_progress(uint32_t was_read) {
    uint32_t total_work = ((int) (block_count * log(block_count))) * num_threads;

    int old_progress = (int) (100 * blocks_read / total_work);
    blocks_read += was_read;
    int progress = (int) (100 * blocks_read / total_work);

    if (old_progress != progress) {
        int ticks = 40;
        char str[ticks + 1];
        memset(str, ' ', ticks);
        memset(str, '=', ticks * progress / 100);
        str[ticks] = '\0';
        printf("\r[%s] %02d%%", str, progress);
        fflush(stdout);
    }
    if (progress == 100) {
        printf("\n");
    }
}

static int do_work(void* arg) {
    zx_handle_t vmo;
    void* buf;
    vmoid_t vmoid;
    groupid_t group;
    if (init_txn_resources(&vmo, &vmoid, &group, &buf)) {
        fprintf(stderr, "failed to initialize background thread\n");
        return -1;
    }

    rand32_t seed_gen = RAND32SEED(base_seed + (int) (uintptr_t) arg);
    for (int i = 0; i < 20; i++) {
    }
    rand32_t work_gen = RAND32SEED(rand32(&seed_gen));
    // The expected number of random pages we need to hit all of them is
    // approx n*log(n) (the coupon collector problem)
    uint32_t blocks_left = block_count * log(block_count);

    while (blocks_left > 0 && !iochk_failure) {
        uint32_t to_read = (rand32(&work_gen) % blocks_left) + 1;
        uint32_t work_offset = rand32(&work_gen) % block_count;
        if (work_offset + to_read > block_count) {
            to_read = block_count - work_offset;
        }

        int res;
        if (rand32(&work_gen) % 2) {
            res = check_range(group, vmoid, start_block + work_offset, to_read, buf);
        } else {
            res = fill_range(group, vmoid, start_block + work_offset, to_read, buf);
        }

        mtx_lock(&lock);
        if (res) {
            iochk_failure = true;
        } else if (!iochk_failure) {
            update_progress(to_read);
            blocks_left -= to_read;
        }
        mtx_unlock(&lock);
    }

    free_txn_resources(vmo, &buf);
    return 0;
}

static uint64_t number(const char* str) {
    char* end;
    uint64_t n = strtoull(str, &end, 10);

    uint64_t m = 1;
    switch (*end) {
    case 'G':
    case 'g':
        m = 1024*1024*1024;
        break;
    case 'M':
    case 'm':
        m = 1024*1024;
        break;
    case 'K':
    case 'k':
        m = 1024;
        break;
    }
    return m * n;
}

static int usage(void) {
    fprintf(stderr, "usage: iochk [OPTIONS] <device>\n\n"
            "    -bs block_size - number of bytes to treat as a unit (default=device block size)\n"
            "    -t thread# - the number of threads to run (default=1)\n"
            "    -c block_count - number of blocks to read (default=the whole device)\n"
            "    -o offset - block-size offset to start reading from (default=0)\n"
            "    -s seed - the seed to use for pseudorandom testing\n"
            "    --live-dangerously - skip confirmation prompt\n");
    return -1;
}

int iochk(int argc, char** argv) {
    const char* device = argv[argc - 1];
    atomic_init(&next_txid, 0);
    fd = open(device, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "error: cannot open '%s'\n", device);
        return usage();
    }

    ioctl_block_get_info(fd, &info);
    printf("opened %s - block_size=%u, block_count=%lu\n",
            device, info.block_size, info.block_count);

    int seed_set = 0;
    num_threads = 1;
    int i = 1;
    bool confirmed = false;
    while (i < argc - 1) {
        if (strcmp(argv[i], "-t") == 0) {
            num_threads = atoi(argv[i + 1]);
            i += 2;
        } else if (strcmp(argv[i], "-c") == 0) {
            block_count = atoi(argv[i + 1]);
            i += 2;
        } else if (strcmp(argv[i], "-o") == 0) {
            start_block = atoi(argv[i + 1]);
            i += 2;
        } else if (strcmp(argv[i], "-bs") == 0) {
            block_size = number(argv[i + 1]);
            i += 2;
        } else if (strcmp(argv[i], "-s") == 0) {
            base_seed = atoll(argv[i + 1]);
            seed_set = 1;
            i += 2;
        } else if (strcmp(argv[i], "--live-dangerously") == 0) {
            confirmed = true;
            i++;
        } else if (strcmp(argv[i], "-h") == 0 ||
                   strcmp(argv[i], "--help") == 0) {
            return usage();
        } else {
            fprintf(stderr, "Invalid arg %s\n", argv[i]);
            return usage();
        }
    }

    if (!confirmed) {
        const char* warning = "\033[0;31mWARNING\033[0m";
        printf("%s: iochk is a destructive operation.\n", warning);
        printf("%s: All data on %s in the given range will be overwritten.\n", warning, device);
        printf("%s: Type 'y' to continue, 'n' or ESC to cancel:\n", warning);
        for (;;) {
            char c;
            int r = read(STDIN_FILENO, &c, 1);
            if (r < 0) {
                fprintf(stderr, "Error reading from stdin\n");
                return r;
            }
            if (c == 'y' || c == 'Y') {
                break;
            } else if (c == 'n' || c == 'N' || c == 27) {
                return 0;
            }
        }
    }

    if (!seed_set) {
        base_seed = zx_clock_get_monotonic();
    }
    printf("seed is %ld\n", base_seed);

    if (block_size == 0) {
        block_size = info.block_size;
    } else if (block_size % info.block_size != 0) {
        fprintf(stderr, "error: block-size is not a multiple of device block size\n");
        return -1;
    }
    uint32_t dev_blocks_per_block = block_size / info.block_size;

    if (dev_blocks_per_block * start_block >= info.block_count) {
        fprintf(stderr, "error: offset past end of device\n");
        return -1;
    }

    if (block_count == 0) {
        block_count = (info.block_count + dev_blocks_per_block - 1) / dev_blocks_per_block;
    } else if (dev_blocks_per_block * (block_count + start_block)
            >= dev_blocks_per_block + info.block_count) {
        // Don't allow blocks to start past the end of the device
        fprintf(stderr, "error: block_count+offset too large\n");
        return -1;
    }

    if (info.max_transfer_size < block_size) {
        fprintf(stderr, "error: block-size is larger than max transfer size (%d)\n",
                info.max_transfer_size);
        return -1;
    }

    zx_handle_t fifo;
    if (ioctl_block_get_fifos(fd, &fifo) != sizeof(fifo)) {
        fprintf(stderr, "error: cannot get fifo for device\n");
        return -1;
    }

    if (block_fifo_create_client(fifo, &client) != ZX_OK) {
        fprintf(stderr, "error: cannot create block client for device\n");
        return -1;
    }

    if (init_device()) {
        fprintf(stderr, "error: device initialization failed\n");
        return -1;
    }

    printf("starting worker threads...\n");
    mtx_init(&lock, mtx_plain);
    thrd_t thrds[num_threads];

    if (num_threads > MAX_TXN_GROUP_COUNT) {
        fprintf(stderr, "number of threads capped at %u\n", MAX_TXN_GROUP_COUNT);
        num_threads = MAX_TXN_GROUP_COUNT;
    }

    for (int i = 0; i < num_threads; i++) {
        if (thrd_create(thrds + i, do_work, (void*) (uintptr_t) i) != thrd_success) {
            fprintf(stderr, "error: thread creation failed\n");
            return -1;
        }
    }

    for (int i = 0; i < num_threads; i++) {
        thrd_join(thrds[i], NULL);
    }

    if (!iochk_failure) {
        printf("re-verifying device...");
        fflush(stdout);
        zx_handle_t vmo;
        void* buf;
        vmoid_t vmoid;
        groupid_t group;
        if (init_txn_resources(&vmo, &vmoid, &group, &buf)) {
            fprintf(stderr, "failed to initialize verification thread\n");
            return -1;
        }
        if (check_range(group, vmoid, start_block, block_count, buf)) {
            fprintf(stderr, "failed to re-verify test data\n");
            iochk_failure = true;
        } else {
            printf("done\n");
        }
        free_txn_resources(vmo, &buf);
    }

    if (!iochk_failure) {
        printf("iochk completed successfully\n");
        return 0;
    } else {
        printf("iochk failed (seed was %ld)\n", base_seed);
        return -1;
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        return usage();
    }

    fd = -1;
    client = NULL;

    int res = iochk(argc, argv);

    if (client) {
        block_fifo_release_client(client);
    }
    if (fd != -1) {
        close(fd);
    }

    return res;
}
