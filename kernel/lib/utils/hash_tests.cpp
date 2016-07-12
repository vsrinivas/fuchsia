// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <app/tests.h>
#include <unittest.h>
#include <utils/hash_table.h>

class FooHash {
public:
    FooHash(int value) : key_(-1), value_(value), next_(nullptr) {}
    int get_value() const { return value_; }

    int get_key() const { return key_; }
    void set_key(int key) { key_ = key; }

    void list_set_next(FooHash* foo) { next_ = foo; }
    const FooHash* list_next() const { return next_; }
    FooHash* list_next() { return next_; }

private:
    int key_;
    int value_;
    FooHash* next_;
};

struct FooHashFn {
    size_t operator()(int key) const
    {
        auto s = reinterpret_cast<char*>(&key);
        auto p = s;
        size_t h = 1;
        for (int ix = 0; ix != sizeof(int); ++p, ++ix) {
            h = ((h << 4) * 17) + *p;
        }
        return h;
    }
};

static int GetHashTableKey(FooHash* fh) { return fh->get_key(); }

static void SetHashTableKey(FooHash* fh, int key) { fh->set_key(key); }

static bool hash_test(void* context)
{
    BEGIN_TEST;

    utils::HashTable<int, FooHash, FooHashFn> foo_table;

    const size_t count = 5;
    int keys[count] = {50, 500, 5000, 50000, 500000};
    FooHash foo[count] = {1, 2, 3, 4, 5};

    for (size_t ix = 0; ix != count; ++ix) {
        foo_table.add(keys[ix], &foo[ix]);
    }

    EXPECT_EQ(count, foo_table.size(), "");

    int sum = 0;
    foo_table.for_each([&sum](FooHash* f) {
        sum += f->get_value();
    });

    EXPECT_EQ(15, sum, "");

    auto item = foo_table.remove(5000);
    EXPECT_EQ(3, item->get_value(), "");
    EXPECT_EQ(count - 1, foo_table.size(), "");

    EXPECT_TRUE(nullptr == foo_table.remove(5000), "");

    foo_table.clear();
    EXPECT_EQ(0U, foo_table.size(), "");

    END_TEST;
}

UNITTEST_START_TESTCASE(hash_tests)
UNITTEST("Hash test", hash_test)
UNITTEST_END_TESTCASE(hash_tests, "hashtests", "Hash table tests", NULL, NULL);
