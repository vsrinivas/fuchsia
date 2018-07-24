// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/string.h>
#include <fbl/vector.h>
#include <lib/zx/time.h>
#include <runtests-utils/fuchsia-run-test.h>
#include <runtests-utils/log-exporter.h>
#include <runtests-utils/runtests-utils.h>

namespace {

// The name of the file containing the syslog.
constexpr char kSyslogFileName[] = "syslog.txt";

const char* kDefaultTestDirs[] = {
    // zircon builds place everything in ramdisks so tests are located in /boot
    "/boot/test/core",
    "/boot/test/libc",
    "/boot/test/ddk",
    "/boot/test/sys",
    "/boot/test/fs",
    // /pkgfs is where test binaries should be found in garnet and above.
    "/pkgfs/packages/*/*/test",
    // Moreover, for the higher layers, there are still tests using the deprecated /system image.
    // Soon they will all be moved under /pkgfs.
    "/system/test",
    "/system/test/core",
    "/system/test/libc",
    "/system/test/ddk",
    "/system/test/sys",
    "/system/test/fs",
};

class FuchsiaStopwatch final : public runtests::Stopwatch {
public:
    FuchsiaStopwatch() { Start(); }
    void Start() override { start_time_ = Now(); }
    int64_t DurationInMsecs() override { return (Now() - start_time_).to_msecs(); }

private:
    zx::time Now() const { return zx::clock::get_monotonic(); }

    zx::time start_time_;
};

// Parse |argv| for an output directory argument.
const char* GetOutputDir(int argc, const char* const* argv) {
    int i = 1;
    while (i < argc - 1 && strcmp(argv[i], "-o") != 0) {
        ++i;
    }
    if (i >= argc - 1) {
        return nullptr;
    }
    return argv[i + 1];
}

} // namespace

int main(int argc, char** argv) {
    const char* output_dir = GetOutputDir(argc, argv);

    // Start Log Listener.
    fbl::unique_ptr<runtests::LogExporter> log_exporter_ptr;
    if (output_dir != nullptr) {
        int error = runtests::MkDirAll(output_dir);
        if (error) {
            printf("Error: Could not create output directory: %s, %s\n", output_dir,
                   strerror(error));
            return -1;
        }

        runtests::ExporterLaunchError exporter_error;
        log_exporter_ptr = runtests::LaunchLogExporter(
            runtests::JoinPath(output_dir, kSyslogFileName), &exporter_error);
        // Don't fail if logger service is not available because it is only
        // available in garnet layer and above.
        if (!log_exporter_ptr && exporter_error != runtests::CONNECT_TO_LOGGER_SERVICE) {
            printf("Error: Failed to launch log listener: %d", exporter_error);
            return -1;
        }
    }

    fbl::Vector<fbl::String> default_test_dirs;
    const int num_default_test_dirs = sizeof(kDefaultTestDirs) / sizeof(char*);
    for (int i = 0; i < num_default_test_dirs; ++i) {
        default_test_dirs.push_back(kDefaultTestDirs[i]);
    }

    FuchsiaStopwatch stopwatch;
    return runtests::DiscoverAndRunTests(&runtests::FuchsiaRunTest, argc, argv, default_test_dirs,
                                         &stopwatch, kSyslogFileName);
}
