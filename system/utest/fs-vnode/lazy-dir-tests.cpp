// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/vector.h>
#include <fs/lazy-dir.h>
#include <fs/pseudo-file.h>
#include <unittest/unittest.h>

namespace {

class DirentChecker {
public:
    DirentChecker(const void* buffer, size_t length)
        : current_(reinterpret_cast<const uint8_t*>(buffer)), remaining_(length) {}

    bool ExpectEnd() {
        BEGIN_HELPER;

        EXPECT_EQ(0u, remaining_);

        END_HELPER;
    }

    bool ExpectEntry(const char* name, uint32_t vtype) {
        BEGIN_HELPER;

        ASSERT_NE(0u, remaining_);
        auto entry = reinterpret_cast<const vdirent_t*>(current_);
        ASSERT_GE(remaining_, entry->size);
        current_ += entry->size;
        remaining_ -= entry->size;
        EXPECT_STR_EQ(name, entry->name, "name");
        EXPECT_EQ(VTYPE_TO_DTYPE(vtype), entry->type);

        END_HELPER;
    }

private:
    const uint8_t* current_;
    size_t remaining_;
};

class TestLazyDir : public fs::LazyDir {
public:
    struct TestContent {
        uint64_t id;
        fbl::String name;
    };

    void GetContents(LazyEntryVector* out_vector) override {
        out_vector->reserve(contents_.size());
        for (const auto& content : contents_) {
            out_vector->push_back({content.id, content.name, V_TYPE_FILE});
        }
    }

    zx_status_t GetFile(fbl::RefPtr<fs::Vnode>* out, uint64_t id, fbl::String name) override {
        last_output_file = fbl::AdoptRef(new fs::UnbufferedPseudoFile());
        *out = last_output_file;
        last_id = id;
        last_name = name;
        return ZX_OK;
    }

    void AddContent(TestContent content) {
        contents_.push_back(fbl::move(content));
    }

    fbl::RefPtr<fs::Vnode> last_output_file;
    uint64_t last_id;
    fbl::String last_name;

private:
    fbl::Vector<TestContent> contents_;
};

bool test_lazy_dir() {
    BEGIN_TEST;

    TestLazyDir test;

    {
        fs::vdircookie_t cookie = {};
        uint8_t buffer[4096];
        size_t len;

        EXPECT_EQ(test.Readdir(&cookie, buffer, sizeof(buffer), &len), ZX_OK);
        DirentChecker dc(buffer, len);
        EXPECT_TRUE(dc.ExpectEntry(".", V_TYPE_DIR));
        EXPECT_TRUE(dc.ExpectEnd());
    }

    test.AddContent({1, "test"});
    {
        fs::vdircookie_t cookie = {};
        uint8_t buffer[4096];
        size_t len;

        EXPECT_EQ(test.Readdir(&cookie, buffer, sizeof(buffer), &len), ZX_OK);
        DirentChecker dc(buffer, len);
        EXPECT_TRUE(dc.ExpectEntry(".", V_TYPE_DIR));
        EXPECT_TRUE(dc.ExpectEntry("test", V_TYPE_FILE));
        EXPECT_TRUE(dc.ExpectEnd());

        fbl::RefPtr<fs::Vnode> out;
        EXPECT_EQ(test.Lookup(&out, "test"), ZX_OK);
        EXPECT_EQ(1, test.last_id);
        EXPECT_TRUE(strcmp("test", test.last_name.c_str()) == 0);
        EXPECT_EQ(out.get(), test.last_output_file.get());

        EXPECT_EQ(test.Lookup(&out, "test2"), ZX_ERR_NOT_FOUND);
    }
    test.AddContent({33, "aaaa"});
    {
        fs::vdircookie_t cookie = {};
        uint8_t buffer[4096];
        size_t len;

        EXPECT_EQ(test.Readdir(&cookie, buffer, sizeof(buffer), &len), ZX_OK);
        DirentChecker dc(buffer, len);
        EXPECT_TRUE(dc.ExpectEntry(".", V_TYPE_DIR));
        EXPECT_TRUE(dc.ExpectEntry("test", V_TYPE_FILE));
        EXPECT_TRUE(dc.ExpectEntry("aaaa", V_TYPE_FILE));
        EXPECT_TRUE(dc.ExpectEnd());

        fbl::RefPtr<fs::Vnode> out;
        EXPECT_EQ(test.Lookup(&out, "aaaa"), ZX_OK);
        EXPECT_EQ(33, test.last_id);
        EXPECT_TRUE(strcmp("aaaa", test.last_name.c_str()) == 0);
        EXPECT_EQ(out.get(), test.last_output_file.get());
    }
    {
        // Ensure manually setting cookie past entries excludes them, but leaves "."
        fs::vdircookie_t cookie = {};
        cookie.n = 30;
        uint8_t buffer[4096];
        size_t len;

        EXPECT_EQ(test.Readdir(&cookie, buffer, sizeof(buffer), &len), ZX_OK);
        DirentChecker dc(buffer, len);
        EXPECT_TRUE(dc.ExpectEntry(".", V_TYPE_DIR));
        EXPECT_TRUE(dc.ExpectEntry("aaaa", V_TYPE_FILE));
        EXPECT_TRUE(dc.ExpectEnd());

        // Expect that "." is missing when reusing the cookie.
        EXPECT_EQ(test.Readdir(&cookie, buffer, sizeof(buffer), &len), ZX_OK);
        dc = DirentChecker(buffer, len);
        EXPECT_TRUE(dc.ExpectEnd());
    }

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(lazy_dir_tests)
RUN_TEST(test_lazy_dir)
END_TEST_CASE(lazy_dir_tests)
