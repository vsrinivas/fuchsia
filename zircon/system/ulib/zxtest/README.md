# ZX Test

ZX Test is a testing library which provides a similar interface as 'Google Test' for writing tests in Zircon.
Unlike gTest, ZX Test has a limited set of dependencies used to test kernel constructs, which 'Google Test' expects to work.
Additionally, Zx Test only depends on a subset libc++, which is currently restricted to header only libraries in Zircon.

ZX Test also supports writing tests in C, a feature which is not supported in 'Google Test'.

This document may reference fatal and non-fatal failures. A failure is the result of an asserted condition being false.
A fatal failure requires the test to abort or stop execution. On the other hand a non fatal failure allows the test execution to continue uninterrupted.
A fatal failure is triggered by macros starting with ``ASSERT_*`` and non fatal failures are triggered by macros starting with ``EXPECT_*``.

## Bugs and Feature Requests
* Please report any bug under ZX Jira component, and assign it to any user in the OWNERS file.
* For feature requests, please describe the use case and the problems the missing feature is causing.


## zxtest Dependencies

zxtest requires some basic libc I/O functions. Typically, this is provided by
including an fdio dependency in the test target that uses zxtest. However, the
"core tests" in ``//zircon/system/utest/core`` provide their own libc I/O
functions since the core tests can't use fdio. (This is why the zxtest library
itself cannot depend on fdio.)

Most other tests should ensure that they separately depend on fdio. If your test
crashes and you see ``libc_io_functions_not_implemented_use_fdio_instead`` at
the top of the stack trace, this is why.

**TODO**: Consider splitting "zxtest" into "zxtest" and "zxtest-core", where the
former includes fdio but the latter doesn't. This would help avoid the
dependency weirdness where fdio has to be separately included by most users even
though it's really needed by zxtest.

## Key Differences from 'Google Test'

* Limited set of dependencies.
* All assertions on main thread.
* ``ASSERT/EXPECT_STATUS`` custom macro.
* ``ASSERT/EXPECT_NOT_STATUS`` custom macro.
* ``ASSERT/EXPECT_OK`` custom macro.
* ``ASSERT/EXPECT_NOT_OK`` custom macro.
* ``CURRENT_TEST_HAS_(FATAL_)FAILURES`` custom macro.
* Custom messages on assertion rely on a printf-like approach instead of a stream ``ASSERT_TRUE(false, "My msg  %d", i);``
* Library supports C.
* `namespace` is `::zxtest` instead of `::testing`. (``zxtest::Test, zxtest::Environment, zxtest::Runner,...``)
* ``RUN_ALL_TESTS`` takes argc and argv as arguments.

## Running Registered Tests
By default the library provides its own ``main`` symbol, which will run all registered tests. This means that users only need to write their tests.
If there is a need for customized initialization, the program must provide its own ``main``.
By doing so, the library's ``main`` will be ignored and the main program's will be picked instead.

In order to execute all registered tests, ``RUN_ALL_TESTS(int argc, char** argv)`` should be called.

```cpp
int return_val = RUN_ALL_TESTS(argc, argv);
```
In this example return_val is 0 on success and non-zero on error. ``RUN_ALL_TESTS`` may be invoked at any time.

## Writing a Test
The following example will add a test named `FooIsBar` to a test case named `FooTestCase`.
Both test case and test names are case sensitive,
meaning that `FooTestCase` and `FOoTestCase` are two different test cases.

The test case name and the test name for a tuple which represents a unique identifier for each test. This identifier must be unique within an entire binary.

```cpp
#include <zxtest/zxtest.h>

TEST(FooTestCase, FooIsBar) {
    Foo foo(/*is_bar=*/true);
    ASSERT_TRUE(foo.IsBar());
}
```

## Helper Methods
A helper method must conform to ``void(*)(Args...args)`` signature if any assertion will used within the helper function body.
The following example demonstrates how to call a helper function and abort execution if within the call stack of the helper function a fatal failure occurs.

```cpp
#include <zxtest/zxtest.h>

void ValidateFoo(const Foo& foo);

TEST(FooTestCase, FooIsBar) {
    Foo foo(true);\
    // Call helper method, and aborts if any error ocurred printing the formatted message.
    ASSERT_NO_FATAL_FAILURES(ValidateFoo(foo), "Foo is invalid %s", foo.ToString().c_str());
    ASSERT_TRUE(foo.IsBar());
}
```

Test writers should prefer helper methods over fixtures.

## Death Tests - Fuchsia Only
Deaths tests are mechanisms in which we expect that a statement may crash the executing thread.
This useful for testing API preconditions violations that would enforce a crash.
There are two types of death tests, those that we expect to crash and those that don't.

In C, |statement| must of type (void*)(void), while in Cpp |statement| must be
convertible to |fit::function<void()>| or closure.

ASSERT_DEATH(statement, message-optional, message-arguments-options);
ASSERT_NO_DEATH(statement, message-optional, message-arguments-options);

# Cpp Example
```cpp
TEST(CheckPanicTest, ZxAssertFalse) {
    ASSERT_DEATH([] {ZX_ASSERT(false);}, "Assertion Failure did not crash");
}
```

In Cpp, we might have multiple lines, or commas within |statement|, this may cause
the surprising behavior of an error on the macro expansion. To deal with this
problem wrap |statement| with parenthesis().

```cpp
TEST(CheckPanicTest, ZxAssertFalse) {
    ASSERT_DEATH(([] {
    int a,b;
    a = 1;
    b = 2;
    ZX_ASSERT(a == b);}), "Assertion Failure did not crash");
}
```

# C Example
```c
TEST(CheckPanicTest, CrashingFunction) {
    ASSERT_DEATH(&CrashingFunction, "CrashingFunction did not crash");
}
```

## Test Fixture -- Cpp Only
Tests frequently required SetUp/TearDown which is different from the test logic itself.
Common patterns include setting up some resource the system under test or component under test
needs.
The following example illustrates how to use a fixture to set up state before any test is
executed and clean afterwards.
```cpp
#include <zxtest/zxtest.h>

class RamdiskFixture : public zxtest::Test {
public:
    const fbl::String& ramdisk_path() const { return ramdisk_path_;}

    // Called before this test case's first test runs.
    // Optional: Defaults to nothing.
    static void SetUpTestCase() {}

    // Called after all tests of this test case runs.
    // Optional: Defaults to nothing.
    static void TearDownTestCase() {}

protected:
    // Called before every test of this test case.
    // Optional: Defaults to nothing.
    void SetUp() override {
        ramdisk_create(......, ramdisk_path.get());
    }

    // Called after every test test of this test case.
    // Optional: Defaults to nothing.
    void TearDown() override {
        ramdisk_destroy(ramdisk_path.get());
    }
private:
    // Test body does not have direct access to this value.
    fbl::StringBuffer<PATH_MAX> ramdisk_path_;
};

// In order to reuse a fixture across multiple test cases, aliasing the fixture or
// using class inheritance allows renaming. In this case RamdiskFixture is used for
// FooTestCase.
using FooTestCase = RamdiskFixture;

// In this case, RamdiskFixture is used for BarTestCase. These are two different test cases,
// that use the same logic.
using BarTestCase = RamdiskFixture;

TEST_F(FooTestCase, FooUsesRamdisk) {
    Foo foo(/*is_bar=*/true);
    // Maybe move this to the fixture?
    fbl::unique_fd ramdisk_fd(open(ramdisk_path().c_str(), O_RDWR));

    ASSERT_TRUE(ramdisk_fd.IsValid());
    ASSERT_TRUE(foo.IsBar());
}
```
* Assertions are permitted in SetUpTestCase/TearDownTestCase and SetUp/TearDown methods.
* Fatal errors during test case initialization (SetUpTestCase) will skip execution of all tests in that test case, but will still run TearDownTestCase.
* Fatal errors during test initialization(SetUp) will skip that test, but will still run TearDown.

## Environment - Cpp only
There are occasions when there is a global resource that needs lifecycle management. The library provides ``zxtest::Environment`` with a SetUp and TearDown method for creating
and dismantling any resource. Once created, the ``zxtest::Runner`` instance will take ownership of the environment, and call the respective methods accordingly.
When used with ``--gtest_repeat`` the environment will be set up and torn down for each iteration.

```cpp
#include <zxtest/zxtest.h>

class MyGlobalResource : public zxtest::Environment {
public:
    // Called before every test of this test case.
    // Optional: Defaults to nothing.
    void SetUp() override {
        ramdisk_create(......, ramdisk_path.get());
    }

    // Called after every test test of this test case.
    // Optional: Defaults to nothing.
    void TearDown() override {
        ramdisk_destroy(ramdisk_path.get());
    }
private:
    // Test body does not have direct access to this value.
    fbl::StringBuffer<PATH_MAX> ramdisk_path_;
};

// Use an alias to match the testcase name we want, which means that it will be treated
// as a completly different testcase.
int main(int argc, char** argv) {
    // Takes ownership.
    zxtest::Runner->GetInstance()->AddGlobalTestEnvironment(new MyGlobalResource());
    return RUN_ALL_TESTS(argc, argv);
}
```

## Flags
We preserve the gtest flag names intentionally, so binaries that interact through that interface can treat the test binary as a black box.

- ``--gtest_filter(-f) pattern`` Follows gtest syntax for defining patterns that match a given test name.
- ``--gtest_break_on_failure(-b)`` Will finish test execution upon encountering a fatal failure.
- ``--gtest_repeat(-i) iter`` Will run all matching tests |iter| times. If |iter| is -1 will run until killed.
- ``--gtest_list_tests(-l)`` List all tests that would be executed with the current options and filters.
- ``--gtest_shuffle(-s)`` Shuffle test execution order.
- ``--gtest_also_run_disabled_tests(-a)`` Will also execute and list tests prefixed with ``DISABLED``.
- ``--gtest_random_seed(-r)`` Provides a seed for random decisions, such as shuffling. The value is available to the user, through ``zxtest::Runner::GetInstance()->random_seed();``. If unset a random seed is provided.
- ``--help(-h)`` Prints help message.
