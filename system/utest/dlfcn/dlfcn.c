// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <launchpad/vmo.h>
#include <magenta/dlfcn.h>
#include <magenta/syscalls.h>

#include <unittest/unittest.h>

bool dlopen_vmo_test(void) {
    BEGIN_TEST;

    mx_handle_t vmo = launchpad_vmo_from_file("/boot/lib/liblaunchpad.so");
    EXPECT_GT(vmo, 0, "launchpad_vmo_from_file");

    void* obj = dlopen_vmo(vmo, RTLD_LOCAL);
    EXPECT_NEQ(obj, NULL, "dlopen_vmo");

    mx_handle_close(vmo);

    void* sym = dlsym(obj, "launchpad_create");
    EXPECT_NEQ(sym, NULL, "dlsym");

    int ok = dlclose(obj);
    EXPECT_EQ(ok, 0, "dlclose");

    END_TEST;
}

BEGIN_TEST_CASE(dlfcn_tests)
RUN_TEST(dlopen_vmo_test);
END_TEST_CASE(dlfcn_tests)

int main(int argc, char** argv) {
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}
