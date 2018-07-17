// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <getopt.h>
#include <inttypes.h>
#include <libgen.h>
#include <limits.h>
#include <sys/stat.h>

#include <fbl/algorithm.h>
#include <fbl/auto_call.h>

#include "fs-host/common.h"

#define MIN_ARGS 3

// Options struct.
struct {
    const char* name;
    Option option;
    const char* argument;
    const char* default_value;
    const char* help;
} OPTS[] = {
    {"depfile", Option::kDepfile, "",          nullptr,
        "Produce a depfile"},
    {"readonly", Option::kReadonly, "",        nullptr,
        "Mount filesystem read-only"},
    {"offset",   Option::kOffset,   "[bytes]", "0",
        "Byte offset at which minfs partition starts"},
    {"length",   Option::kLength,   "[bytes]", "Remaining Length",
        "Length in bytes of minfs partition"},
    {"help",     Option::kHelp,     "",        nullptr,
        "Display this message"},
};

// Commands struct.
struct {
    const char* name;
    Command command;
    uint32_t flags;
    ArgType arg_type;
    const char* help;
} CMDS[] = {
    {"create",   Command::kMkfs,     O_RDWR | O_CREAT, ArgType::kOptional,
        "Initialize filesystem."},
    {"mkfs",     Command::kMkfs,     O_RDWR | O_CREAT, ArgType::kOptional,
        "Initialize filesystem."},
    {"check",    Command::kFsck,     O_RDONLY,         ArgType::kNone,
        "Check filesystem integrity."},
    {"fsck",     Command::kFsck,     O_RDONLY,         ArgType::kNone,
        "Check filesystem integrity."},
    {"add",      Command::kAdd,      O_RDWR,           ArgType::kMany,
        "Add files to an fs image (additional arguments required)."},
    {"cp",       Command::kCp,       O_RDWR,           ArgType::kTwo,
        "Copy to/from fs."},
    {"mkdir",    Command::kMkdir,    O_RDWR,           ArgType::kOne,
        "Create directory."},
    {"ls",       Command::kLs,       O_RDONLY,         ArgType::kOne,
        "List contents of directory."},
    {"manifest", Command::kManifest, O_RDWR,           ArgType::kOne,
        "Add files to fs as specified in manifest (deprecated)."},
};

// Arguments struct.
struct {
    const char* name;
    Argument argument;
} ARGS[] = {
    {"--manifest", Argument::kManifest},
    {"--blob",     Argument::kBlob},
};

zx_status_t FsCreator::ProcessAndRun(int argc, char** argv) {
    zx_status_t status;
    if ((status = ProcessArgs(argc, argv)) != ZX_OK) {
        return status;
    }

    return RunCommand();
}

zx_status_t FsCreator::Usage() {
    fprintf(stderr,
            "usage: %s [ <option>* ] <file-or-device>[@<size>] <command> [ <arg>* ]\n\n",
            GetToolName());

    // Display all valid pre-command options.
    bool first = true;
    for (unsigned n = 0; n < fbl::count_of(OPTS); n++) {
        if (IsOptionValid(OPTS[n].option)) {
            fprintf(stderr, "%-8s -%c|--%-8s ", first ? "options:" : "",
                    OPTS[n].name[0], OPTS[n].name);

            fprintf(stderr, "%-8s", OPTS[n].argument);

            fprintf(stderr, "\t%s\n", OPTS[n].help);
            if (OPTS[n].default_value != nullptr) {
                fprintf(stderr, "%33s(Default = %s)\n", "", OPTS[n].default_value);
            }
            first = false;
        }
    }
    fprintf(stderr, "\n");

    // Display all valid commands.
    first = true;
    for (unsigned n = 0; n < fbl::count_of(CMDS); n++) {
        if (IsCommandValid(CMDS[n].command)) {
            fprintf(stderr, "%9s %-10s %s\n", first ? "commands:" : "",
                    CMDS[n].name, CMDS[n].help);
            first = false;
        }
    }
    fprintf(stderr, "\n");

    // Display all valid '--' arguments.
    fprintf(stderr, "arguments (valid for create, one or more required for add):\n");
    for (unsigned n = 0; n < fbl::count_of(ARGS); n++) {
        if (IsArgumentValid(ARGS[n].argument)) {
            fprintf(stderr, "\t%-10s <path>\n", ARGS[n].name);
        }
    }

    return ZX_ERR_INVALID_ARGS;
}

zx_status_t FsCreator::ProcessManifest(char* manifest_path) {
    fbl::unique_fd manifest_fd(open(manifest_path, O_RDONLY, 0644));
    if (!manifest_fd) {
        fprintf(stderr, "error: cannot open '%s'\n", manifest_path);
        return ZX_ERR_IO;
    }

    char dir_path[PATH_MAX];
    strncpy(dir_path, dirname(manifest_path), PATH_MAX);
    FILE* manifest = fdopen(manifest_fd.release(), "r");
    while (true) {
        // Keep processing lines in the manifest until we have reached EOF.
        zx_status_t status = ProcessManifestLine(manifest, dir_path);
        if (status == ZX_ERR_OUT_OF_RANGE) {
            fclose(manifest);
            return ZX_OK;
        } else if (status != ZX_OK) {
            fclose(manifest);
            return status;
        }
    }
}

zx_status_t FsCreator::ParseManifestLine(FILE* manifest, const char* dir_path, char* src, char* dst) {
    size_t size = 0;
    char* line = nullptr;

    // Always free the line on exiting this method.
    auto cleanup = fbl::MakeAutoCall([&line]() { if (line) free(line); });

    // Retrieve the next line from the manifest.
    ssize_t r = getline(&line, &size, manifest);
    if (r < 0) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    // Exit early if line is commented out
    if (line[0] == '#') {
        return ZX_OK;
    }

    char* equals = strchr(line, '=');
    char* src_start = line;

    if (equals != nullptr) {
        // If we found an '=', there is a destination in this line.
        // (Note that destinations are allowed but not required for blobfs.)
        if (strchr(equals + 1, '=') != nullptr) {
            fprintf(stderr, "Too many '=' in input\n");
            return ZX_ERR_INVALID_ARGS;
        }

        src_start = equals + 1;
        equals[0] = '\0';

        strncat(dst, line, PATH_MAX - strlen(dst));
    }

    // If the source is not an absolute path, append the manifest's local directory.
    if (src_start[0] != '/') {
        strncpy(src, dir_path, PATH_MAX);
        strncat(src, "/", PATH_MAX - strlen(src));
    }

    strncat(src, src_start, PATH_MAX - strlen(src));

    // Set the source path to terminate if it currently ends in a new line.
    char* new_line = strchr(src, '\n');
    if (new_line != nullptr) {
        *new_line = '\0';
    }

    return ZX_OK;
}

zx_status_t FsCreator::ProcessArgs(int argc, char** argv) {
    if (argc < MIN_ARGS) {
        fprintf(stderr, "Not enough args\n");
        return Usage();
    }

    bool depfile_needed = false;

    // Read options.
    while (true) {
        // Set up options struct for pre-device option processing.
        unsigned index = 0;
        struct option opts[fbl::count_of(OPTS) + 1];
        for (unsigned n = 0; n < fbl::count_of(OPTS); n++) {
            if (IsOptionValid(OPTS[n].option)) {
                opts[index].name = OPTS[n].name;
                opts[index].has_arg = strlen(OPTS[n].argument) ? required_argument : no_argument;
                opts[index].flag = nullptr;
                opts[index].val = OPTS[n].name[0];
                index++;
            }
        }

        opts[index] = {nullptr, 0, nullptr, 0};

        int opt_index;

        int c = getopt_long(argc, argv, "+dro:l:h", opts, &opt_index);
        if (c < 0) {
            break;
        }

        switch (c) {
        case 'd':
            depfile_needed = true;
            break;
        case 'r':
            read_only_ = true;
            break;
        case 'o':
            offset_ = atoll(optarg);
            break;
        case 'l':
            length_ = atoll(optarg);
            break;
        case 'h':
        default:
            return Usage();
        }

    }

    argc -= optind;
    argv += optind;

    if (argc < 2) {
        fprintf(stderr, "Not enough arguments\n");
        return Usage();
    }

    // Read device name.
    char* device = argv[0];
    argc--;
    argv++;

    // Read command name.
    char* command = argv[0];
    argc--;
    argv++;

    uint32_t open_flags = 0;
    ArgType arg_type = ArgType::kNone;

    // Validate command.
    for (unsigned i = 0; i < sizeof(CMDS) / sizeof(CMDS[0]); i++) {
        if (!strcmp(command, CMDS[i].name)) {
            if (!IsCommandValid(CMDS[i].command)) {
                fprintf(stderr, "Invalid command %s\n", command);
                return Usage();
            }

            command_ = CMDS[i].command;
            open_flags = read_only_ ? O_RDONLY : CMDS[i].flags;
            arg_type = CMDS[i].arg_type;
        }
    }

    if (command_ == Command::kNone) {
        fprintf(stderr, "Unknown command: %s\n", argv[0]);
        return Usage();
    }

    // Parse the size argument (if any) from the device string.
    size_t requested_size = 0;
    if (ParseSize(device, &requested_size) != ZX_OK) {
        return Usage();
    }

    // Open the target device. Do this before we continue processing arguments, in case we are
    // copying directories from a minfs image and need to pre-process them.
    fd_.reset(open(device, open_flags, 0644));
    if (!fd_) {
        fprintf(stderr, "error: cannot open '%s'\n", device);
        return ZX_ERR_IO;
    }

    struct stat stats;
    if (fstat(fd_.get(), &stats) < 0) {
        fprintf(stderr, "Failed to stat device %s\n", device);
        return ZX_ERR_IO;
    }

    // Unless we are creating an image, the length_ has already been decided.
    if (command_ != Command::kMkfs) {
        if (length_) {
            if (offset_ + length_ > stats.st_size) {
                fprintf(stderr, "Must specify offset + length <= file size\n");
                return ZX_ERR_INVALID_ARGS;
            }
        } else {
            length_ = stats.st_size - offset_;
        }
    }

    // Verify that we've received a valid number of arguments for the given command.
    bool valid = true;
    switch (arg_type) {
    case ArgType::kNone:
        valid = argc == 0;
        break;
    case ArgType::kOne:
        valid = argc == 1;
        break;
    case ArgType::kTwo:
        valid = argc == 2;
        break;
    case ArgType::kMany:
        valid = argc;
        break;
    case ArgType::kOptional:
        break;
    default:
        return ZX_ERR_INTERNAL;
    }

    if (!valid) {
        fprintf(stderr, "Invalid arguments\n");
        return Usage();
    }

    // Process remaining arguments.
    while (argc > 0) {
        // Default to 2 arguments processed for manifest. If ProcessCustom is called, processed
        // will be populated with the actual number of arguments used.
        uint8_t processed = 2;
        if (!strcmp(argv[0], "--manifest")) {
            if (argc < 2) {
                return ZX_ERR_INVALID_ARGS;
            }

            zx_status_t status;
            if ((status = ProcessManifest(argv[1])) != ZX_OK) {
                return status;
            }
        } else if (ProcessCustom(argc, argv, &processed) != ZX_OK) {
            return Usage();
        }

        argc -= processed;
        argv += processed;
    }

    // Resize the file if we need to.
    zx_status_t status;
    if ((status = ResizeFile(requested_size, stats)) != ZX_OK) {
        return status;
    }

    if (depfile_needed) {
        size_t len = strlen(device);
        assert(len+2 < PATH_MAX);
        char buf[PATH_MAX] = {0};
        memcpy(&buf[0], device, strlen(device));
        buf[len++] = '.';
        buf[len++] = 'd';

        depfile_.reset(open(buf, O_CREAT|O_TRUNC|O_WRONLY, 0644));
        if (!depfile_) {
            fprintf(stderr, "error: cannot open '%s'\n", buf);
            return ZX_ERR_IO;
        }

        // update the buf to be suitable to pass to AppendDepfile.
        buf[len-2] = ':';
        buf[len-1] = 0;

        status = AppendDepfile(&buf[0]);
    }

    return status;
}

zx_status_t FsCreator::AppendDepfile(const char* str) {
    if (!depfile_) {
        return ZX_OK;
    }

    size_t len = strlen(str);
    assert(len < PATH_MAX);
    char buf[PATH_MAX] = {0};
    memcpy(&buf[0], str, len);
    buf[len++] = ' ';

    std::lock_guard<std::mutex> lock(depfile_lock_);

    // this code makes assumptions about the size of atomic writes on target
    // platforms which currently hold true, but are not part of e.g. POSIX.
    if (write(depfile_.get(), buf, len) != len) {
        fprintf(stderr, "error: depfile append error\n");
        return ZX_ERR_IO;
    }
    return ZX_OK;
}

zx_status_t FsCreator::RunCommand() {
    if (!fd_) {
        fprintf(stderr, "Failed to open fd before running command\n");
        return ZX_ERR_INTERNAL;
    }

    switch (command_) {
    case Command::kMkfs:
        return Mkfs();
    case Command::kFsck:
        return Fsck();
    case Command::kAdd:
    case Command::kCp:
    case Command::kManifest:
    case Command::kMkdir:
        return Add();
    case Command::kLs:
        return Ls();
    default:
        fprintf(stderr, "Error: Command not defined\n");
        return ZX_ERR_INTERNAL;
    }
}

zx_status_t FsCreator::ParseSize(char* device, size_t* out) {
    char* sizestr = nullptr;
    if ((sizestr = strchr(device, '@')) != nullptr) {
        if (command_ != Command::kMkfs) {
            fprintf(stderr, "Cannot specify size for this command\n");
            return ZX_ERR_INVALID_ARGS;
        }
        // Create a file with an explicitly requested size
        *sizestr++ = 0;
        char* end;
        size_t size = strtoull(sizestr, &end, 10);
        if (end == sizestr) {
            fprintf(stderr, "%s: bad size: %s\n", GetToolName(), sizestr);
            return ZX_ERR_INVALID_ARGS;
        }
        switch (end[0]) {
        case 'M':
        case 'm':
            size *= (1024 * 1024);
            end++;
            break;
        case 'G':
        case 'g':
            size *= (1024 * 1024 * 1024);
            end++;
            break;
        }
        if (end[0] || size == 0) {
            fprintf(stderr, "%s: bad size: %s\n", GetToolName(), sizestr);
            return ZX_ERR_INVALID_ARGS;
        }

        if (length_ && offset_ + length_ > size) {
            fprintf(stderr, "Must specify size > offset + length\n");
            return ZX_ERR_INVALID_ARGS;
        }
        *out = size;
    }

    return ZX_OK;
}

zx_status_t FsCreator::ResizeFile(off_t requested_size, struct stat stats) {
    if (command_ != Command::kMkfs) {
        // This method is only valid on creation of the fs image.
        return ZX_OK;
    }

    // Calculate the total required size for the fs image, given all files that have been processed
    // up to this point. Note that for blobfs there is currently no de-duplification of files, so
    // the estimate might be slightly higher than the minimum required.
    off_t required_size = CalculateRequiredSize();

    bool is_block = S_ISBLK(stats.st_mode);

    if (requested_size) {
        if (requested_size < required_size) {
            // If the size requested by @ is smaller than the size required, return an error.
            fprintf(stderr, "Must specify size larger than required size %" PRIu64 "\n",
                    required_size);
            return ZX_ERR_INVALID_ARGS;
        } else if (is_block) {
            // Do not allow re-sizing for block devices.
            fprintf(stderr, "%s: @size argument is not supported for block device targets\n",
                    GetToolName());
            return ZX_ERR_INVALID_ARGS;
        }
    }

    if (command_ == Command::kMkfs && !is_block &&
        (stats.st_size != required_size || requested_size)) {
        // Only truncate the file size under the following conditions:
        // 1.  We are creating the fs store for the first time.
        // 2.  We are not operating on a block device.
        // 3a. The current file size is different than the size required for the specified files, OR
        // 3b. The user has requested a particular size using the @ argument.
        off_t truncate_size = requested_size ? requested_size : required_size;

        if (length_ && (offset_ + length_) > truncate_size) {
            // If an offset+length were specified and they are smaller than the minimum required,
            // return an error.
            fprintf(stderr, "Length %" PRIu64 " too small for required size %" PRIu64 "\n", length_,
                    truncate_size);
            return ZX_ERR_INVALID_ARGS;
        }

        if (ftruncate(fd_.get(), truncate_size)) {
            fprintf(stderr, "error: cannot truncate device\n");
            return ZX_ERR_IO;
        }

        if (!length_) {
            length_ = truncate_size - offset_;
        }
    } else if (!length_) {
        // If not otherwise specified, update length to be equal to the size of the image.
        length_ = stats.st_size - offset_;
    }

    return ZX_OK;
}
