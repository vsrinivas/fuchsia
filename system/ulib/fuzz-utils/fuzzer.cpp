// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>

#include <fbl/auto_call.h>
#include <fbl/string.h>
#include <fbl/string_printf.h>
#include <fuzz-utils/fuzzer.h>
#include <task-utils/walker.h>
#include <zircon/status.h>
#include <zircon/types.h>

namespace fuzzing {
namespace {

// List of supported subcommands
enum Command : uint32_t {
    kNone,
    kHelp,
};

// Usage information for specific tool subcommands.
const struct {
    Command cmd;
    const char* name;
    const char* args;
    const char* desc;
} kCommands[] = {
    {kHelp, "help", "", "Print this message and exit."},
};

} // namespace

// Public methods

Fuzzer::~Fuzzer() {}

// Protected methods

Fuzzer::Fuzzer() : cmd_(kNone), out_(stdout), err_(stderr) {}

void Fuzzer::Reset() {
    cmd_ = kNone;
    name_.clear();
    executable_.clear();
    root_.clear();
    inputs_.clear();
    options_.clear();
    out_ = stdout;
    err_ = stderr;
}

zx_status_t Fuzzer::Run(StringList* args) {
    ZX_DEBUG_ASSERT(args);
    zx_status_t rc;

    if ((rc = SetCommand(args->first())) != ZX_OK || (rc = SetFuzzer(args->next())) != ZX_OK ||
        (rc = LoadOptions()) != ZX_OK) {
        return rc;
    }
    const char* arg;
    while ((arg = args->next())) {
        if (*arg != '-') {
            inputs_.push_back(arg);
        } else if ((rc = SetOption(arg + 1)) != ZX_OK) {
            return rc;
        }
    }
    switch (cmd_) {
    case kHelp:
        return Help();

    default:
        // Shouldn't get here.
        ZX_DEBUG_ASSERT(false);
        return ZX_ERR_INTERNAL;
    }
}

zx_status_t Fuzzer::SetOption(const char* option) {
    ZX_DEBUG_ASSERT(option);

    const char* ptr = option;
    while (*ptr && *ptr != '#' && (*ptr == '-' || isspace(*ptr))) {
        ++ptr;
    }
    const char* mark = ptr;
    while (*ptr && *ptr != '#' && *ptr != '=' && !isspace(*ptr)) {
        ++ptr;
    }
    fbl::String key(mark, ptr - mark);
    while (*ptr && *ptr != '#' && (*ptr == '=' || isspace(*ptr))) {
        ++ptr;
    }
    mark = ptr;
    while (*ptr && *ptr != '#' && !isspace(*ptr)) {
        ++ptr;
    }
    fbl::String val(mark, ptr - mark);

    return SetOption(key.c_str(), val.c_str());
}

zx_status_t Fuzzer::SetOption(const char* key, const char* value) {
    ZX_DEBUG_ASSERT(key);
    ZX_DEBUG_ASSERT(value);

    // Ignore blank options
    if (*key == '\0' && *value == '\0') {
        return ZX_OK;
    }

    // Must have both key and value
    if (*key == '\0' || *value == '\0') {
        fprintf(err_, "Empty key or value: '%s'='%s'\n", key, value);
        return ZX_ERR_INVALID_ARGS;
    }

    // Save the option
    options_.set(key, value);

    return ZX_OK;
}

zx_status_t Fuzzer::RebasePath(const char* path, Path* out) {
    zx_status_t rc;

    out->Reset();
    if (!root_.empty() && (rc = out->Push(root_.c_str())) != ZX_OK) {
        fprintf(err_, "failed to move to '%s': %s\n", root_.c_str(), zx_status_get_string(rc));
        return rc;
    }
    if ((rc = out->Push(path)) != ZX_OK) {
        return rc;
    }

    return ZX_OK;
}

zx_status_t Fuzzer::GetPackagePath(const char* package, Path* out) {
    zx_status_t rc;

    if ((rc = RebasePath("pkgfs/packages", out)) != ZX_OK) {
        return rc;
    }
    auto pop_prefix = fbl::MakeAutoCall([&out]() { out->Pop(); });
    if ((rc = out->Push(package)) != ZX_OK) {
        fprintf(err_, "failed to move to '%s': %s\n", package, zx_status_get_string(rc));
        return rc;
    }
    auto pop_package = fbl::MakeAutoCall([&out]() { out->Pop(); });

    auto versions = out->List();
    long int max = -1;
    const char* max_version = nullptr;
    for (const char* version = versions->first(); version; version = versions->next()) {
        if (version[0] == '\0') {
            continue;
        }
        char* endptr = nullptr;
        long int val = strtol(version, &endptr, 10);
        if (endptr[0] != '\0') {
            continue;
        }
        if (val > max) {
            max = val;
            max_version = version;
        }
    }
    if (!max_version) {
        fprintf(err_, "No versions available for package: %s\n", package);
        return ZX_ERR_NOT_FOUND;
    }

    if ((rc = out->Push(max_version)) != ZX_OK) {
        fprintf(err_, "failed to move to '%s': %s\n", max_version, zx_status_get_string(rc));
        return rc;
    }

    pop_package.cancel();
    pop_prefix.cancel();
    return ZX_OK;
}

void Fuzzer::FindZirconFuzzers(const char* zircon_path, const char* target, StringMap* out) {
    Path path;
    if (RebasePath(zircon_path, &path) != ZX_OK) {
        return;
    }

    auto targets = path.List();
    for (const char* t = targets->first(); t; t = targets->next()) {
    }

    targets->keep_if(target);
    for (const char* t = targets->first(); t; t = targets->next()) {
        out->set(fbl::StringPrintf("zircon_fuzzers/%s", t).c_str(), path.Join(t).c_str());
    }
}

void Fuzzer::FindFuchsiaFuzzers(const char* package, const char* target, StringMap* out) {
    Path path;
    if (RebasePath("pkgfs/packages", &path) != ZX_OK) {
        return;
    }

    auto packages = path.List();
    packages->keep_if(package);

    for (const char* p = packages->first(); p; p = packages->next()) {
        if (GetPackagePath(p, &path) != ZX_OK || path.Push("test") != ZX_OK) {
            continue;
        }

        auto targets = path.List();
        targets->keep_if(target);

        fbl::String abspath;
        for (const char* t = targets->first(); t; t = targets->next()) {
            out->set(fbl::StringPrintf("%s/%s", p, t).c_str(), path.Join(t).c_str());
        }
    }
}

void Fuzzer::FindFuzzers(const char* package, const char* target, StringMap* out) {
    if (strstr("zircon_fuzzers", package) != nullptr) {
        FindZirconFuzzers("boot/test/fuzz", target, out);
        FindZirconFuzzers("system/test/fuzz", target, out);
    }
    FindFuchsiaFuzzers(package, target, out);
}

static zx_status_t ParseName(const char* name, fbl::String* out_package, fbl::String* out_target) {
    const char* sep = name ? strchr(name, '/') : nullptr;
    if (!sep) {
        return ZX_ERR_NOT_FOUND;
    }
    out_package->Set(name, sep - name);
    out_target->Set(sep + 1);
    return ZX_OK;
}

void Fuzzer::FindFuzzers(const char* name, StringMap* out) {
    ZX_DEBUG_ASSERT(out);

    // Scan the system for available fuzzers
    out->clear();
    fbl::String package, target;
    if (ParseName(name, &package, &target) == ZX_OK) {
        FindFuzzers(package.c_str(), target.c_str(), out);
    } else if (name) {
        FindFuzzers(name, "", out);
        FindFuzzers("", name, out);
    } else {
        FindFuzzers("", "", out);
    }
}

// |fuzzing::Walked| is a |TaskEnumerator| used to find and print status information about a given
// fuzzer |executable|.
class Walker final : public TaskEnumerator {
public:
    explicit Walker(const Fuzzer* fuzzer) : fuzzer_(fuzzer) {}
    ~Walker() {}

    zx_status_t OnProcess(int depth, zx_handle_t task, zx_koid_t koid, zx_koid_t pkoid) override {
        return fuzzer_->CheckProcess(task) ? ZX_ERR_STOP : ZX_OK;
    }

protected:
    bool has_on_process() const override { return true; }

private:
    const Fuzzer* fuzzer_;
};

bool Fuzzer::CheckProcess(zx_handle_t process) const {
    char name[ZX_MAX_NAME_LEN];
    zx_info_process_t info;
    if (zx_object_get_property(process, ZX_PROP_NAME, name, sizeof(name)) != ZX_OK ||
        strcmp(name, executable_.c_str()) != 0 ||
        zx_object_get_info(process, ZX_INFO_PROCESS, &info, sizeof(info), nullptr, nullptr) !=
            ZX_OK) {
        return false;
    }

    if (!info.started) {
        fprintf(out_, "Fuzzer '%s' has not started.\n", name_.c_str());
    } else if (!info.exited) {
        fprintf(out_, "Fuzzer '%s' is running.\n", name_.c_str());
    } else {
        fprintf(out_, "Fuzzer '%s' exited with return code %" PRId64 ".\n", name_.c_str(),
                info.return_code);
    }

    return true;
}

// Private methods

zx_status_t Fuzzer::SetCommand(const char* command) {
    cmd_ = kNone;
    options_.clear();
    inputs_.clear();

    if (!command) {
        fprintf(err_, "Missing command. Try 'help'.\n");
        return ZX_ERR_INVALID_ARGS;
    }
    for (size_t i = 0; i < sizeof(kCommands) / sizeof(kCommands[0]); ++i) {
        if (strcmp(command, kCommands[i].name) == 0) {
            cmd_ = kCommands[i].cmd;
            break;
        }
    }
    if (cmd_ == kNone) {
        fprintf(err_, "Unknown command '%s'. Try 'help'.\n", command);
        return ZX_ERR_INVALID_ARGS;
    }

    return ZX_OK;
}

zx_status_t Fuzzer::SetFuzzer(const char* name) {
    // Early exit for commands that don't need a single, selected fuzzer
    switch (cmd_) {
    case kHelp:
        if (name) {
            name_.Set(name);
        }
        return ZX_OK;

    default:
        break;
    }

    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Fuzzer::LoadOptions() {
    switch (cmd_) {
    case kHelp:
        // No options needed
        return ZX_OK;

    default:
        break;
    }

    return ZX_ERR_NOT_SUPPORTED;
}

// Specific subcommands

zx_status_t Fuzzer::Help() {
    fprintf(out_, "usage: fuzz <command> [args]\n\n");
    fprintf(out_, "Supported commands are:\n");
    for (size_t i = 0; i < sizeof(kCommands) / sizeof(kCommands[0]); ++i) {
        fprintf(out_, "  %s %s\n", kCommands[i].name, kCommands[i].args);
        fprintf(out_, "    %s\n\n", kCommands[i].desc);
    }
    return ZX_OK;
}

} // namespace fuzzing
