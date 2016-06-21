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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <mxu/unittest.h>

// Having a section named ".interp" causes the linker to emit a PT_INTERP
// phdr even for a static link.  Note that we must do something to ensure
// that --gc-sections doesn't remove the section!  Below we just make sure
// that a function refers to this variable.
static const char interp[] __attribute__((section(".interp"))) =
    "/boot/bin/elf-interp-helper.so";

// The elf-interp-helper is a dummy "dynamic linker" that does almost
// nothing.  It increments test_word from its initial value, and then
// jumps to the entry point of the main program (this one).
#define INITIAL_TEST_WORD UINT32_C(0xfeedface)
uint32_t test_word = INITIAL_TEST_WORD;

static bool test_interp_loaded(void) {
    BEGIN_TEST;

    // This useless message serves to keep live a reference to interp, so
    // that the linker will not remove the .interp section as unused.
    unittest_printf("...Loaded via \"%s\"...", interp);

    EXPECT_EQ(test_word, INITIAL_TEST_WORD + 1,
               "interpreter did not increment test word");

    END_TEST;
}

BEGIN_TEST_CASE(elf_interp_tests)
RUN_TEST(test_interp_loaded)
END_TEST_CASE(elf_interp_tests)

int main(void) {
    bool success = unittest_run_all_tests();
    return success ? 0 : -1;
}
