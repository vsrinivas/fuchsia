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

#include <unittest/unittest.h>
#include <stdbool.h>
#include <stdint.h>

static bool global_ctor_ran;

static struct Global {
    Global() { global_ctor_ran = true; }
} global;

bool check_ctor() {
    BEGIN_TEST;
    EXPECT_TRUE(global_ctor_ran, "global constuctor didn't run!");
    END_TEST;
}

static int my_static = 23;

bool check_initializer() {
    BEGIN_TEST;
    EXPECT_EQ(my_static, 23, "static initializer didn't run!");
    END_TEST;
}

BEGIN_TEST_CASE(ctors)
RUN_TEST(check_ctor)
RUN_TEST(check_initializer)
END_TEST_CASE(ctors)

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
