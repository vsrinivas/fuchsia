// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int usage(void) {
    fprintf(stderr, "usage: dd [OPTIONS]\n");
    fprintf(stderr, "dd can be used to convert and copy files\n");
    fprintf(stderr, " bs=BYTES  : read and write BYTES bytes at a time\n");
    fprintf(stderr, " count=N   : copy only N input blocks\n");
    fprintf(stderr, " ibs=BYTES : read BYTES bytes at a time (default: 512)\n");
    fprintf(stderr, " if=FILE   : read from FILE instead of stdin\n");
    fprintf(stderr, " obs=BYTES : write BYTES bytes at a time (default: 512)\n");
    fprintf(stderr, " of=FILE   : write to FILE instead of stdout\n");
    fprintf(stderr, " seek=N    : skip N obs-sized blocks at start of output\n");
    fprintf(stderr, " skip=N    : skip N ibs-sized blocks at start of input\n");
    fprintf(stderr, " N and BYTES may be followed by the following multiplicitive\n"
                    " suffixes: c = 1, w = 2, b = 512, kB = 1000, K = 1024,\n"
                    "           MB = 1000 * 1000, M = 1024 * 1024, xM = M,\n"
                    "           GB = 1000 * 1000 * 1000, G = 1024 * 1024 * 1024\n");
    fprintf(stderr, " --help : Show this help message\n");
    return -1;
}

// Returns "true" if the argument matches the prefix.
// In this case, moves the argument past the prefix.
bool prefix_match(const char** arg, const char* prefix) {
    if (!strncmp(*arg, prefix, strlen(prefix))) {
        *arg += strlen(prefix);
        return true;
    }
    return false;
}

#define MAYBE_MULTIPLY_SUFFIX(str, out, suffix, value) \
        if (!strcmp((str), (suffix))) {                \
            (out) *= (value);                          \
            return 0;                                  \
        }

// Parse the formatted size string from |s|, and place
// the result in |out|.
//
// Returns 0 on success.
int parse_size(const char* s, size_t* out) {
    char* endptr;
    if (!(*s >= '0' && *s <= '9')) {
        goto done;
    }
    *out = strtol(s, &endptr, 10);
    if (*endptr == '\0') {
        return 0;
    }

    MAYBE_MULTIPLY_SUFFIX(endptr, *out, "G", 1UL << 30);
    MAYBE_MULTIPLY_SUFFIX(endptr, *out, "GB", 1000 * 1000 * 1000UL);
    MAYBE_MULTIPLY_SUFFIX(endptr, *out, "xM", 1UL << 20);
    MAYBE_MULTIPLY_SUFFIX(endptr, *out, "M", 1UL << 20);
    MAYBE_MULTIPLY_SUFFIX(endptr, *out, "MB", 1000 * 1000UL);
    MAYBE_MULTIPLY_SUFFIX(endptr, *out, "K", 1UL << 10);
    MAYBE_MULTIPLY_SUFFIX(endptr, *out, "kB", 1000UL);
    MAYBE_MULTIPLY_SUFFIX(endptr, *out, "b", 512UL);
    MAYBE_MULTIPLY_SUFFIX(endptr, *out, "w", 2UL);
    MAYBE_MULTIPLY_SUFFIX(endptr, *out, "c", 1UL);

done:
    fprintf(stderr, "Couldn't parse size string: %s\n", s);
    return -1;
}

typedef struct {
    bool use_count;
    size_t count;
    size_t input_bs;
    size_t input_skip;
    size_t output_bs;
    size_t output_seek;
    char input[PATH_MAX];
    char output[PATH_MAX];
} dd_options_t;

int parse_args(int argc, const char** argv, dd_options_t* options) {
    while (argc > 1) {
        const char* arg = argv[1];
        if (prefix_match(&arg, "bs=")) {
            size_t size;
            if (parse_size(arg, &size)) {
                return usage();
            }
            options->input_bs = size;
            options->output_bs = size;
        } else if (prefix_match(&arg, "count=")) {
            if (parse_size(arg, &options->count)) {
                return usage();
            }
            options->use_count = true;
        } else if (prefix_match(&arg, "ibs=")) {
            if (parse_size(arg, &options->input_bs)) {
                return usage();
            }
        } else if (prefix_match(&arg, "obs=")) {
            if (parse_size(arg, &options->output_bs)) {
                return usage();
            }
        } else if (prefix_match(&arg, "seek=")) {
            if (parse_size(arg, &options->output_seek)) {
                return usage();
            }
        } else if (prefix_match(&arg, "skip=")) {
            if (parse_size(arg, &options->input_skip)) {
                return usage();
            }
        } else if (prefix_match(&arg, "if=")) {
            strncpy(options->input, arg, PATH_MAX);
            options->input[PATH_MAX - 1] = '\0';
        } else if (prefix_match(&arg, "of=")) {
            strncpy(options->output, arg, PATH_MAX);
            options->output[PATH_MAX - 1] = '\0';
        } else {
            return usage();
        }
        argc--;
        argv++;
    }
    return 0;
}

#define MIN(x,y) ((x) < (y) ? (x) : (y))
#define MAX(x,y) ((x) < (y) ? (y) : (x))

int main(int argc, const char** argv) {
    dd_options_t options;
    memset(&options, 0, sizeof(dd_options_t));
    options.input_bs = 512;
    options.output_bs = 512;
    int r;
    if ((r = parse_args(argc, argv, &options))) {
        return r;
    }

    if (options.input_bs == 0 || options.output_bs == 0) {
        fprintf(stderr, "block sizes must be greater than zero\n");
        return -1;
    }

    options.input_skip *= options.input_bs;
    options.output_seek *= options.output_bs;

    // Input and output fds
    int in = -1;
    int out = -1;
    // Buffer to contain partially read data
    char* buf = NULL;
    // Return code
    r = -1;
    // Number of full records copied to/from target
    size_t records_in = 0;
    size_t records_out = 0;
    // Size of remaining "partial" transfer from input / to output.
    size_t record_in_partial = 0;
    size_t record_out_partial = 0;

    if (*options.input == '\0') {
        in = STDIN_FILENO;
    } else {
        in = open(options.input, O_RDONLY);
        if (in < 0) {
            fprintf(stderr, "Couldn't open input file %s : %d\n", options.input, errno);
            goto done;
        }
    }

    if (*options.output == '\0') {
        out = STDOUT_FILENO;
    } else {
        out = open(options.output, O_WRONLY | O_CREAT);
        if (out < 0) {
            fprintf(stderr, "Couldn't open output file %s : %d\n", options.output, errno);
            goto done;
        }
    }

    buf = malloc(MAX(options.output_bs, options.input_bs));
    if (buf == NULL) {
        fprintf(stderr, "No memory\n");
        goto done;
    }

    if (options.input_skip != 0) {
        // Try seeking first; if that doesn't work, try reading to an input buffer.
        if (lseek(in, options.input_skip, SEEK_SET) != (off_t) options.input_skip) {
            while (options.input_skip) {
                if (read(in, buf, options.input_bs) != (ssize_t) options.input_bs) {
                    fprintf(stderr, "Couldn't read from input\n");
                    goto done;
                }
                options.input_skip -= options.input_bs;
            }
        }
    }

    if (options.output_seek != 0) {
        if (lseek(out, options.output_seek, SEEK_SET) != (off_t) options.output_seek) {
            fprintf(stderr, "Failed to seek on output\n");
            goto done;
        }
    }

    if (MAX(options.input_bs, options.output_bs) %
        MIN(options.input_bs, options.output_bs) != 0) {
        // TODO(smklein): Implement this case, rather than returning an error
        fprintf(stderr, "Input and output block sizes must be multiples\n");
        goto done;
    }

    bool terminating = false;
    size_t rlen = 0;
    while (true) {
        if (options.use_count && !options.count) {
            r = 0;
            goto done;
        }

        // Read as much as we can (up to input_bs) into our target buffer
        ssize_t rout;
        if ((rout = read(in, buf, options.input_bs)) != (ssize_t) options.input_bs) {
            terminating = true;
        }
        if (rout == (ssize_t) options.input_bs) {
            records_in++;
        } else if (rout > 0) {
            record_in_partial = rout;
        }
        if (rout > 0) {
            rlen += rout;
        }
        if (options.use_count) {
            --options.count;
            if (options.count == 0) {
                terminating = true;
            }
        }

        // If we can (or should, due to impending termination), dump the read
        // buffer into the output file.
        if (rlen >= options.output_bs || terminating) {
            size_t off = 0;
            while (off != rlen) {
                size_t wlen = MIN(options.output_bs, rlen - off);
                if (write(out, buf + off, wlen) != (ssize_t) wlen) {
                    fprintf(stderr, "Couldn't write %zu bytes to output\n", wlen);
                    goto done;
                }
                if (wlen == options.output_bs) {
                    records_out++;
                } else {
                    record_out_partial = wlen;
                }
                off += wlen;
            }
            rlen = 0;
        }

        if (terminating) {
            r = 0;
            goto done;
        }
    }

done:
    printf("%zu+%u records in\n", records_in, record_in_partial ? 1 : 0);
    printf("%zu+%u records out\n", records_out, record_out_partial ? 1 : 0);
    printf("%zu bytes copied\n", records_out * options.output_bs + record_out_partial);

    if (in != -1) {
        close(in);
    }
    if (out != -1) {
        close(out);
    }
    free(buf);
    return r;
}
