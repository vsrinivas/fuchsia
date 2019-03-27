// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include <fbl/auto_call.h>
#include <fbl/macros.h>
#include <fbl/string.h>
#include <fbl/string_buffer.h>
#include <fbl/string_piece.h>
#include <fbl/string_printf.h>
#include <fuzz-utils/fuzzer.h>
#include <fuzz-utils/path.h>
#include <fuzz-utils/string-list.h>
#include <lib/fdio/spawn.h>
#include <lib/zx/process.h>
#include <lib/zx/time.h>
#include <task-utils/walker.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/syscalls/object.h>
#include <zircon/types.h>

namespace fuzzing {
namespace {

// List of supported subcommands
enum Command : uint32_t {
    kNone,
    kHelp,
    kList,
    kSeeds,
    kStart,
    kCheck,
    kStop,
    kRepro,
    kMerge,
};

// Usage information for specific tool subcommands.  Keep in sync with //scripts/devshell/fuzz!
const struct {
    Command cmd;
    const char* name;
    const char* args;
    const char* desc;
} kCommands[] = {
    {kHelp, "help", "", "Prints this message and exits."},
    {kList, "list", "[name]", "Lists fuzzers matching 'name' if provided, or all\nfuzzers."},
    {kSeeds, "seeds", "<name>", "Lists the seed corpus location(s) for the fuzzer."},
    {kStart, "start", "<name> [...]",
     "Starts the named fuzzer.  Additional arguments are\npassed to the fuzzer."},
    {kCheck, "check", "<name>",
     "Reports information about the named fuzzer, such as\nexecution status, corpus size, and "
     "number of\nartifacts."},
    {kStop, "stop", "<name>", "Stops all instances of the named fuzzer."},
    {kRepro, "repro", "<name> [...]",
     "Runs the named fuzzer on specific inputs. If no\nadditional inputs are provided, uses "
     "previously\nfound artifacts."},
    {kMerge, "merge", "<name> [...]",
     "Merges the corpus for the named fuzzer.  If no\nadditional inputs are provided, minimizes "
     "the\ncurrent corpus."},
};

// |kArtifactPrefixes| should matches the prefixes in libFuzzer passed to |Fuzzer::DumpCurrentUnit|
// or |Fuzzer::WriteUnitToFileWithPrefix|.
constexpr const char* kArtifactPrefixes[] = {
    "crash",
    "leak",
    "mismatch",
    "oom",
    "slow-unit",
    "timeout",
};
constexpr size_t kArtifactPrefixesLen = sizeof(kArtifactPrefixes) / sizeof(kArtifactPrefixes[0]);

} // namespace

// Public methods

Fuzzer::~Fuzzer() {}

zx_status_t Fuzzer::Main(int argc, char** argv) {
    Fuzzer fuzzer;
    StringList args(argv + 1, argc - 1);
    return fuzzer.Run(&args);
}

// Protected methods

Fuzzer::Fuzzer()
    : cmd_(kNone), out_(stdout), err_(stderr) {}

void Fuzzer::Reset() {
    cmd_ = kNone;
    name_.clear();
    target_.clear();
    root_.clear();
    resource_path_.Reset();
    data_path_.Reset();
    inputs_.clear();
    options_.clear();
    process_.reset();
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
    case kList:
        return List();
    case kSeeds:
        return Seeds();
    case kStart:
        return Start();
    case kCheck:
        return Check();
    case kStop:
        return Stop();
    case kRepro:
        return Repro();
    case kMerge:
        return Merge();
    default:
        // Shouldn't get here.
        ZX_DEBUG_ASSERT(false);
        return ZX_ERR_INTERNAL;
    }
}

zx_status_t Fuzzer::SetOption(const fbl::String& option) {
    const char* ptr = option.c_str();
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

    return SetOption(key, val);
}

zx_status_t Fuzzer::SetOption(const fbl::String& key, const fbl::String& value) {
    // Ignore blank options
    if (key.empty() && value.empty()) {
        return ZX_OK;
    }

    // Must have both key and value
    if (key.empty() || value.empty()) {
        fprintf(err_, "Empty key or value: '%s'='%s'\n", key.c_str(), value.c_str());
        return ZX_ERR_INVALID_ARGS;
    }

    // Save the option
    options_.set(key, value);

    return ZX_OK;
}

zx_status_t Fuzzer::RebasePath(const fbl::String& path, Path* out) {
    zx_status_t rc;

    out->Reset();
    if (!root_.empty() && (rc = out->Push(root_)) != ZX_OK) {
        fprintf(err_, "failed to move to '%s': %s\n", root_.c_str(), zx_status_get_string(rc));
        return rc;
    }
    if ((rc = out->Push(path)) != ZX_OK) {
        return rc;
    }

    return ZX_OK;
}

zx_status_t Fuzzer::GetPackagePath(const fbl::String& package, Path* out) {
    zx_status_t rc;

    if ((rc = RebasePath("pkgfs/packages", out)) != ZX_OK) {
        return rc;
    }
    auto pop_prefix = fbl::MakeAutoCall([&out]() { out->Pop(); });
    if ((rc = out->Push(package)) != ZX_OK) {
        fprintf(err_, "failed to move to '%s': %s\n", package.c_str(), zx_status_get_string(rc));
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
        fprintf(err_, "No versions available for package: %s\n", package.c_str());
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

void Fuzzer::FindFuzzers(const fbl::String& package, const fbl::String& target, StringMap* out) {
    Path path;
    if (RebasePath("pkgfs/packages", &path) != ZX_OK) {
        return;
    }

    auto packages = path.List();
    packages->keep_if(package);

    for (const char* p = packages->first(); p; p = packages->next()) {
        if (GetPackagePath(p, &path) != ZX_OK || path.Push("data") != ZX_OK) {
            continue;
        }
        auto targets = path.List();
        targets->keep_if(target);
        path.Pop();
        for (const char* t = targets->first(); t; t = targets->next()) {
            if (path.IsFile(fbl::StringPrintf("data/%s/corpora", t)) &&
                path.IsFile(fbl::StringPrintf("data/%s/dictionary", t)) &&
                path.IsFile(fbl::StringPrintf("data/%s/options", t)) &&
                path.IsFile(fbl::StringPrintf("meta/%s.cmx", t))) {
                out->set(fbl::StringPrintf("%s/%s", p, t),
                         fbl::StringPrintf("fuchsia-pkg://fuchsia.com/%s#meta/%s.cmx", p, t));
            }
        }
    }
}

static zx_status_t ParseName(const fbl::String& name, fbl::String* out_package,
                             fbl::String* out_target) {
    const char* ptr = name.c_str();
    const char* sep = strchr(ptr, '/');
    if (!sep) {
        return ZX_ERR_NOT_FOUND;
    }
    out_package->Set(ptr, sep - ptr);
    out_target->Set(sep + 1);
    return ZX_OK;
}

void Fuzzer::FindFuzzers(const fbl::String& name, StringMap* out) {
    ZX_DEBUG_ASSERT(out);

    // Scan the system for available fuzzers
    out->clear();
    fbl::String package, target;
    if (ParseName(name, &package, &target) == ZX_OK) {
        FindFuzzers(package, target, out);
    } else if (name.length() != 0) {
        FindFuzzers(name, "", out);
        FindFuzzers("", name, out);
    } else {
        FindFuzzers("", "", out);
    }
}

void Fuzzer::GetArgs(StringList* out) {
    out->clear();
    if (strstr(target_.c_str(), "fuchsia-pkg://fuchsia.com/") == target_.c_str()) {
        out->push_back("/bin/run");
    }
    out->push_back(target_);
    const char* key;
    const char* val;
    options_.begin();
    while (options_.next(&key, &val)) {
        out->push_back(fbl::StringPrintf("-%s=%s", key, val));
    }
    for (const char* input = inputs_.first(); input; input = inputs_.next()) {
        out->push_back(input);
    }
}

zx_status_t Fuzzer::Execute() {
    zx_status_t rc;

    // If the "-jobs=N" option is set, output will be sent to fuzz-<job>.log and can run to
    // completion in the background.
    const char* jobs = options_.get("jobs");
    bool background = jobs && atoi(jobs) != 0;

    // Copy all command line arguments.
    StringList args;
    GetArgs(&args);

    size_t argc = args.length();
    const char* argv[argc + 1];

    argv[0] = args.first();
    fprintf(out_, "+ %s", argv[0]);
    for (size_t i = 1; i < argc; ++i) {
        argv[i] = args.next();
        fprintf(out_, " %s", argv[i]);
    }
    argv[argc] = nullptr;
    fprintf(out_, "\n");

    // This works even in a component, since FDIO_SPAWN_CLONE_ALL clones the namespace and argv[0]
    // in the correct namespace name, /pkg/bin/<binary>.
    if ((rc = fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, argv[0], argv,
                         process_.reset_and_get_address())) != ZX_OK) {
        Path path;
        if (strcmp(argv[0], "/bin/run") != 0) {
            fprintf(err_, "Failed to spawn '%s': %s\n", argv[0], zx_status_get_string(rc));
        } else if (GetPackagePath("run", &path) != ZX_OK) {
            fprintf(err_, "Required package 'run' is missing.\n");
        } else if (argc > 1) {
            fprintf(err_, "Failed to spawn '%s': %s\n", argv[1], zx_status_get_string(rc));
        } else {
            fprintf(err_, "Malformed command line: '%s'\n", argv[0]);
        }
        return rc;
    }

    if (background) {
        return ZX_OK;
    }

    if ((rc = process_.wait_one(ZX_TASK_TERMINATED, zx::time::infinite(), nullptr)) != ZX_OK) {
        fprintf(err_, "Failed while waiting for process to end: %s\n", zx_status_get_string(rc));
        return rc;
    }

    zx_info_process_t proc_info;
    if ((rc = process_.get_info(ZX_INFO_PROCESS, &proc_info, sizeof(proc_info), nullptr,
                                nullptr)) != ZX_OK) {
        fprintf(err_, "Failed to get exit code for process: %s\n", zx_status_get_string(rc));
        return rc;
    }

    if (proc_info.return_code != ZX_OK) {
        fprintf(out_, "Fuzzer returned non-zero exit code: %" PRId64 "\n", proc_info.return_code);
    }

    return ZX_OK;
}

// |fuzzing::Walker| is a |TaskEnumerator| used to find a given fuzzer |executable| and print status
// or end it.
class Walker final : public TaskEnumerator {
public:
    explicit Walker(const Fuzzer* fuzzer, bool kill)
        : fuzzer_(fuzzer), kill_(kill), killed_(0) {}
    ~Walker() {}

    size_t killed() const { return killed_; }

    zx_status_t OnProcess(int depth, zx_handle_t task, zx_koid_t koid, zx_koid_t pkoid) override {
        if (!fuzzer_->CheckProcess(task, kill_)) {
            return ZX_OK;
        }
        if (kill_) {
            ++killed_;
            return ZX_OK;
        }
        return ZX_ERR_STOP;
    }

protected:
    bool has_on_process() const override { return true; }

private:
    const Fuzzer* fuzzer_;
    bool kill_;
    size_t killed_;
};

bool Fuzzer::CheckProcess(zx_handle_t task, bool kill) const {
    char name[ZX_MAX_NAME_LEN];

    if (zx_object_get_property(task, ZX_PROP_NAME, name, sizeof(name)) != ZX_OK) {
        return false;
    }

    const char* target = target_.c_str();
    const char* meta = strstr(target, "#meta/");
    if (meta) {
        target = meta + strlen("#meta/");
    }
    if (strncmp(name, target, sizeof(name) - 1) != 0) {
        return false;
    }
    if (kill) {
        zx_task_kill(task);
        return true;
    }

    zx_info_process_t info;

    if (zx_object_get_info(task, ZX_INFO_PROCESS, &info, sizeof(info), nullptr, nullptr) != ZX_OK) {
        return false;
    }
    if (!info.started) {
        fprintf(out_, "%s: NOT STARTED\n", name_.c_str());
    } else if (!info.exited) {
        fprintf(out_, "%s: RUNNING\n", name_.c_str());
    } else {
        fprintf(out_, "%s: EXITED (return code = %" PRId64 ")\n", name_.c_str(), info.return_code);
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
    zx_status_t rc;

    // Early exit for commands that don't need a single, selected fuzzer
    switch (cmd_) {
    case kHelp:
    case kList:
        if (name) {
            name_.Set(name);
        }
        return ZX_OK;

    default:
        break;
    }

    if (!name) {
        fprintf(err_, "Missing fuzzer name.\n");
        return ZX_ERR_INVALID_ARGS;
    }
    name_.Set(name);

    // Determine the fuzzer
    StringMap fuzzers;
    FindFuzzers(name_, &fuzzers);
    switch (fuzzers.size()) {
    case 0:
        fprintf(err_, "No matching fuzzers for '%s'.\n", name);
        return ZX_ERR_NOT_FOUND;
    case 1:
        break;
    default:
        fprintf(err_, "Multiple matching fuzzers for '%s':\n", name);
        List();
        return ZX_ERR_INVALID_ARGS;
    }

    fuzzers.begin();
    fuzzers.next(&name_, &target_);

    fbl::String package, target;
    if ((rc = ParseName(name_, &package, &target)) != ZX_OK) {
        return rc;
    }

    // Determine the directory that holds the fuzzing resources. It may not be present if fuzzing
    // Zircon standalone.
    if ((rc = GetPackagePath(package, &resource_path_)) != ZX_OK ||
        (rc = resource_path_.Push("data")) != ZX_OK ||
        (rc = resource_path_.Push(target)) != ZX_OK) {
        // No-op: The directory may not be present when fuzzing standalone Zircon.
        resource_path_.Reset();
    }

    // Ensure the directories that will hold the fuzzing outputs are present.
    if ((rc = RebasePath("data", &data_path_)) != ZX_OK ||
        (rc = data_path_.Ensure("fuzzing")) != ZX_OK ||
        (rc = data_path_.Push("fuzzing")) != ZX_OK || (rc = data_path_.Ensure(package)) != ZX_OK ||
        (rc = data_path_.Push(package)) != ZX_OK || (rc = data_path_.Ensure(target)) != ZX_OK ||
        (rc = data_path_.Push(target)) != ZX_OK || (rc = data_path_.Ensure("corpus")) != ZX_OK) {
        fprintf(err_, "Failed to establish data path for '%s/%s': %s\n", package.c_str(),
                target.c_str(), zx_status_get_string(rc));
        return ZX_ERR_IO;
    }

    return ZX_OK;
}

zx_status_t Fuzzer::LoadOptions() {
    zx_status_t rc;
    switch (cmd_) {
    case kHelp:
    case kList:
    case kSeeds:
        // No options needed
        return ZX_OK;

    case kStart:
        if ((rc = SetOption("jobs", "1")) != ZX_OK) {
            return rc;
        }
        break;

    case kMerge:
        if ((rc = SetOption("merge", "1")) != ZX_OK ||
            (rc = SetOption("merge_control_file", data_path_.Join(".mergefile"))) != ZX_OK) {
            return rc;
        }
        break;

    default:
        break;
    }

    // Artifacts go in the data directory
    if ((rc = SetOption("artifact_prefix", data_path_.c_str())) != ZX_OK) {
        return rc;
    }

    // Early exit if no resources
    if (resource_path_.length() <= 1) {
        return ZX_OK;
    }

    // Record the (optional) dictionary
    size_t dict_size;
    if ((rc = resource_path_.GetSize("dictionary", &dict_size)) == ZX_OK && dict_size != 0 &&
        (rc = SetOption("dict", resource_path_.Join("dictionary"))) != ZX_OK) {
        fprintf(err_, "failed to set dictionary option: %s\n", zx_status_get_string(rc));
        return rc;
    }

    // Read the (optional) options file
    fbl::String options = resource_path_.Join("options");
    FILE* f = fopen(options.c_str(), "r");
    if (f) {
        auto close_f = fbl::MakeAutoCall([&f]() { fclose(f); });
        char buffer[PATH_MAX];
        while (fgets(buffer, sizeof(buffer), f)) {
            if ((rc = SetOption(buffer)) != ZX_OK) {
                fprintf(err_, "Failed to set option: %s", zx_status_get_string(rc));
                return rc;
            }
        }
    }

    return ZX_OK;
}

// Specific subcommands

zx_status_t Fuzzer::Help() {
    fprintf(out_, "Run a fuzz test\n");
    fprintf(out_, "\n");
    fprintf(out_, "Usage: fuzz <command> [command-arguments]\n");
    fprintf(out_, "\n");
    fprintf(out_, "Commands:\n");
    fprintf(out_, "\n");
    for (size_t i = 0; i < sizeof(kCommands) / sizeof(kCommands[0]); ++i) {
        fprintf(out_, "  %-7s %-15s ", kCommands[i].name, kCommands[i].args);
        const char* desc = kCommands[i].desc;
        const char* fmt = "%s\n";
        for (const char* nl = strchr(desc, '\n'); nl; nl = strchr(desc, '\n')) {
            fbl::String line(desc, nl - desc);
            fprintf(out_, fmt, line.c_str());
            desc = nl + 1;
            fmt = "                          %s\n";
        }
        fprintf(out_, fmt, desc);
    }
    return ZX_OK;
}

zx_status_t Fuzzer::List() {
    StringMap fuzzers;
    FindFuzzers(name_, &fuzzers);
    if (fuzzers.is_empty()) {
        fprintf(out_, "No matching fuzzers.\n");
        return ZX_OK;
    }
    fprintf(out_, "Found %zu matching fuzzers:\n", fuzzers.size());
    const char* name;
    fuzzers.begin();
    while (fuzzers.next(&name, nullptr)) {
        fprintf(out_, "  %s\n", name);
    }
    return ZX_OK;
}

zx_status_t Fuzzer::Seeds() {
    if (resource_path_.length() <= 1) {
        fprintf(out_, "No seed corpora found for %s.\n", name_.c_str());
        return ZX_OK;
    }
    fbl::String corpora = resource_path_.Join("corpora");
    FILE* f = fopen(corpora.c_str(), "r");
    if (!f) {
        fprintf(out_, "No seed corpora found for %s.\n", name_.c_str());
        return ZX_OK;
    }
    auto close_f = fbl::MakeAutoCall([&f]() { fclose(f); });

    char buffer[PATH_MAX];
    while (fgets(buffer, sizeof(buffer), f)) {
        fprintf(out_, "%s\n", buffer);
    }
    return ZX_OK;
}

zx_status_t Fuzzer::Start() {
    zx_status_t rc;

    // If no inputs, use the default corpus
    if (inputs_.is_empty()) {
        if ((rc = data_path_.Ensure("corpus")) != ZX_OK) {
            fprintf(err_, "Failed to make empty corpus: %s\n", zx_status_get_string(rc));
            return rc;
        }
        inputs_.push_front(data_path_.Join("corpus"));
    }

    return Execute();
}

zx_status_t Fuzzer::Check() {
    // Report fuzzer execution status
    Walker walker(this, false /* !kill */);
    if (walker.WalkRootJobTree() != ZX_ERR_STOP) {
        fprintf(out_, "%s: STOPPED\n", name_.c_str());
    }

    // Fuzzer details
    fprintf(out_, "    Target info:  %s\n", target_.c_str());
    fprintf(out_, "    Output path:  %s\n", data_path_.c_str());

    // Report corpus details, if present
    if (data_path_.Push("corpus") != ZX_OK) {
        fprintf(out_, "    Corpus size:  0 inputs / 0 bytes\n");
    } else {
        auto corpus = data_path_.List();
        size_t corpus_len = 0;
        size_t corpus_size = 0;
        for (const char* input = corpus->first(); input; input = corpus->next()) {
            size_t input_size;
            if (data_path_.GetSize(input, &input_size) == ZX_OK) {
                ++corpus_len;
                corpus_size += input_size;
            }
        }
        fprintf(out_, "    Corpus size:  %zu inputs / %zu bytes\n", corpus_len, corpus_size);
        data_path_.Pop();
    }

    // Report number of artifacts.
    auto artifacts = data_path_.List();
    StringList prefixes(kArtifactPrefixes,
                        sizeof(kArtifactPrefixes) / sizeof(kArtifactPrefixes[0]));
    artifacts->keep_if_any(&prefixes);
    size_t num_artifacts = artifacts->length();
    if (num_artifacts == 0) {
        fprintf(out_, "    Artifacts:    None\n");
    } else {
        const char* artifact = artifacts->first();
        fprintf(out_, "    Artifacts:    %s\n", artifact);
        while ((artifact = artifacts->next())) {
            fprintf(out_, "                  %s\n", artifact);
        }
    }

    return ZX_OK;
}

zx_status_t Fuzzer::Stop() {
    Walker walker(this, true /* kill */);
    walker.WalkRootJobTree();
    fprintf(out_, "Stopped %zu tasks.\n", walker.killed());
    return ZX_OK;
}

zx_status_t Fuzzer::Repro() {
    zx_status_t rc;

    // If no patterns, match all artifacts
    if (inputs_.is_empty()) {
        inputs_.push_back("");
    }

    // Filter data for just artifacts that match one or more supplied patterns
    auto artifacts = data_path_.List();
    StringList prefixes(kArtifactPrefixes, kArtifactPrefixesLen);
    artifacts->keep_if_any(&prefixes);
    artifacts->keep_if_any(&inputs_);

    // Get full paths of artifacts
    inputs_.clear();
    for (const char* artifact = artifacts->first(); artifact; artifact = artifacts->next()) {
        inputs_.push_back(data_path_.Join(artifact));
    }

    // Nothing to repro
    if (inputs_.is_empty()) {
        fprintf(err_, "No matching artifacts found.\n");
        return ZX_ERR_NOT_FOUND;
    }

    if ((rc = Execute()) != ZX_OK) {
        fprintf(err_, "Failed to execute: %s\n", zx_status_get_string(rc));
        return rc;
    }

    return ZX_OK;
}

zx_status_t Fuzzer::Merge() {
    zx_status_t rc;

    // If no inputs and no merge in process, minimize the previous corpus (and there must be an
    // existing corpus!)
    size_t mergefile_len = 0;
    data_path_.GetSize(".mergefile", &mergefile_len);

    if (inputs_.is_empty() && mergefile_len == 0) {
        if ((rc = data_path_.Rename("corpus", "corpus.prev")) != ZX_OK) {
            fprintf(err_, "Failed to move 'corpus' for minimization: %s\n",
                    zx_status_get_string(rc));
            return rc;
        }
    }
    if (inputs_.is_empty()) {
        inputs_.push_back(data_path_.Join("corpus.prev"));
    }

    // Make sure the corpus directory exists, and make sure the output corpus is the first argument
    if ((rc = data_path_.Ensure("corpus")) != ZX_OK) {
        fprintf(err_, "Failed to ensure 'corpus': %s\n", zx_status_get_string(rc));
        return rc;
    }
    inputs_.erase_if(data_path_.Join("corpus"));
    inputs_.push_front(data_path_.Join("corpus"));

    if ((rc = Execute()) != ZX_OK) {
        fprintf(err_, "Failed to execute: %s\n", zx_status_get_string(rc));
        return rc;
    }

    // Merge complete; cleanup temporary files.
    if ((rc = data_path_.Remove("corpus.prev")) != ZX_OK ||
        (rc = data_path_.Remove(".mergefile")) != ZX_OK) {
        fprintf(err_, "Failed to remove merge control files: %s\n", zx_status_get_string(rc));
        return rc;
    }

    return ZX_OK;
}

} // namespace fuzzing
