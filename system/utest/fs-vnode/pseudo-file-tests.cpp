// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/pseudo-file.h>

#include <fbl/initializer_list.h>
#include <fbl/vector.h>
#include <unittest/unittest.h>

#define EXPECT_FSTR_EQ(expected, actual)                                \
    EXPECT_BYTES_EQ(reinterpret_cast<const uint8_t*>(expected.c_str()), \
                    reinterpret_cast<const uint8_t*>(actual.c_str()),   \
                    expected.size() + 1u, "unequal fbl::String")

namespace {

zx_status_t DummyReader(fbl::String* output) {
    return ZX_OK;
}

zx_status_t DummyWriter(fbl::StringPiece input) {
    return ZX_OK;
}

class VectorReader {
public:
    VectorReader(fbl::initializer_list<fbl::String> strings)
        : strings_(strings) {}

    fs::PseudoFile::ReadHandler GetHandler() {
        return [this](fbl::String* output) {
            if (index_ >= strings_.size())
                return ZX_ERR_IO;
            *output = strings_[index_++];
            return ZX_OK;
        };
    }

    const fbl::Vector<fbl::String>& strings() const { return strings_; }

private:
    fbl::Vector<fbl::String> strings_;
    size_t index_ = 0u;
};

class VectorWriter {
public:
    VectorWriter(size_t max_strings)
        : max_strings_(max_strings) {}

    fs::PseudoFile::WriteHandler GetHandler() {
        return [this](fbl::StringPiece input) {
            if (strings_.size() >= max_strings_)
                return ZX_ERR_IO;
            strings_.push_back(fbl::String(input));
            return ZX_OK;
        };
    }

    const fbl::Vector<fbl::String>& strings() const { return strings_; }

private:
    const size_t max_strings_;
    fbl::Vector<fbl::String> strings_;
};

bool CheckRead(const fbl::RefPtr<fs::Vnode>& file, zx_status_t status,
               size_t length, size_t offset, fbl::StringPiece expected) {
    BEGIN_HELPER;

    uint8_t buf[length];
    memset(buf, '!', length);
    size_t actual = 0u;
    EXPECT_EQ(status, file->Read(buf, length, offset, &actual));
    EXPECT_EQ(expected.size(), actual);
    EXPECT_BYTES_EQ(reinterpret_cast<const uint8_t*>(expected.data()), buf, expected.size(), "");

    END_HELPER;
}

bool CheckWrite(const fbl::RefPtr<fs::Vnode>& file, zx_status_t status,
                size_t offset, fbl::StringPiece content, size_t expected_actual) {
    BEGIN_HELPER;

    size_t actual = 0u;
    EXPECT_EQ(status, file->Write(content.data(), content.size(), offset, &actual));
    EXPECT_EQ(expected_actual, actual);

    END_HELPER;
}

bool CheckAppend(const fbl::RefPtr<fs::Vnode>& file, zx_status_t status,
                 fbl::StringPiece content, size_t expected_end, size_t expected_actual) {
    BEGIN_HELPER;

    size_t end = 0u;
    size_t actual = 0u;
    EXPECT_EQ(status, file->Append(content.data(), content.size(), &end, &actual));
    EXPECT_EQ(expected_end, end);
    EXPECT_EQ(expected_actual, actual);

    END_HELPER;
}

bool test_open_validation_buffered() {
    BEGIN_TEST;

    // no read handler, no write handler
    {
        auto file = fbl::AdoptRef<fs::Vnode>(new fs::BufferedPseudoFile());
        EXPECT_EQ(ZX_ERR_ACCESS_DENIED, file->ValidateFlags(ZX_FS_RIGHT_READABLE));
        EXPECT_EQ(ZX_ERR_ACCESS_DENIED, file->ValidateFlags(ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE));
        EXPECT_EQ(ZX_ERR_ACCESS_DENIED, file->ValidateFlags(ZX_FS_RIGHT_WRITABLE));
        EXPECT_EQ(ZX_ERR_NOT_DIR, file->ValidateFlags(ZX_FS_FLAG_DIRECTORY));
    }

    // read handler, no write handler
    {
        auto file = fbl::AdoptRef<fs::Vnode>(new fs::BufferedPseudoFile(&DummyReader));
        EXPECT_EQ(ZX_ERR_ACCESS_DENIED, file->ValidateFlags(ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE));
        EXPECT_EQ(ZX_ERR_ACCESS_DENIED, file->ValidateFlags(ZX_FS_RIGHT_WRITABLE));
        EXPECT_EQ(ZX_ERR_NOT_DIR, file->ValidateFlags(ZX_FS_FLAG_DIRECTORY));

        fbl::RefPtr<fs::Vnode> redirect;
        EXPECT_EQ(ZX_OK, file->ValidateFlags(ZX_FS_RIGHT_READABLE));
        EXPECT_EQ(ZX_OK, file->Open(ZX_FS_RIGHT_READABLE, &redirect));
        EXPECT_NONNULL(redirect);
    }

    // no read handler, write handler
    {
        auto file = fbl::AdoptRef<fs::Vnode>(new fs::BufferedPseudoFile(nullptr, &DummyWriter));
        EXPECT_EQ(ZX_ERR_ACCESS_DENIED, file->ValidateFlags(ZX_FS_RIGHT_READABLE));
        EXPECT_EQ(ZX_ERR_ACCESS_DENIED, file->ValidateFlags(ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE));
        EXPECT_EQ(ZX_ERR_NOT_DIR, file->ValidateFlags(ZX_FS_FLAG_DIRECTORY));

        fbl::RefPtr<fs::Vnode> redirect;
        EXPECT_EQ(ZX_OK, file->ValidateFlags(ZX_FS_RIGHT_WRITABLE));
        EXPECT_EQ(ZX_OK, file->Open(ZX_FS_RIGHT_WRITABLE, &redirect));
        EXPECT_NONNULL(redirect);
    }

    // read handler, write handler
    {
        auto file = fbl::AdoptRef<fs::Vnode>(new fs::BufferedPseudoFile(&DummyReader, &DummyWriter));
        EXPECT_EQ(ZX_ERR_NOT_DIR, file->ValidateFlags(ZX_FS_FLAG_DIRECTORY));

        fbl::RefPtr<fs::Vnode> redirect;
        EXPECT_EQ(ZX_OK, file->ValidateFlags(ZX_FS_RIGHT_READABLE));
        EXPECT_EQ(ZX_OK, file->Open(ZX_FS_RIGHT_READABLE, &redirect));
        EXPECT_NONNULL(redirect);
        redirect.reset();
        EXPECT_EQ(ZX_OK, file->ValidateFlags(ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE));
        EXPECT_EQ(ZX_OK, file->Open(ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE, &redirect));
        EXPECT_NONNULL(redirect);
        redirect.reset();
        EXPECT_EQ(ZX_OK, file->ValidateFlags(ZX_FS_RIGHT_WRITABLE));
        EXPECT_EQ(ZX_OK, file->Open(ZX_FS_RIGHT_WRITABLE, &redirect));
        EXPECT_NONNULL(redirect);
    }

    END_TEST;
}

bool test_open_validation_unbuffered() {
    BEGIN_TEST;

    // no read handler, no write handler
    {
        auto file = fbl::AdoptRef<fs::Vnode>(new fs::UnbufferedPseudoFile());
        EXPECT_EQ(ZX_ERR_ACCESS_DENIED, file->ValidateFlags(ZX_FS_RIGHT_READABLE));
        EXPECT_EQ(ZX_ERR_ACCESS_DENIED, file->ValidateFlags(ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE));
        EXPECT_EQ(ZX_ERR_ACCESS_DENIED, file->ValidateFlags(ZX_FS_RIGHT_WRITABLE));
        EXPECT_EQ(ZX_ERR_NOT_DIR, file->ValidateFlags(ZX_FS_FLAG_DIRECTORY));
    }

    // read handler, no write handler
    {
        auto file = fbl::AdoptRef<fs::Vnode>(new fs::UnbufferedPseudoFile(&DummyReader));
        EXPECT_EQ(ZX_ERR_ACCESS_DENIED, file->ValidateFlags(ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE));
        EXPECT_EQ(ZX_ERR_ACCESS_DENIED, file->ValidateFlags(ZX_FS_RIGHT_WRITABLE));
        EXPECT_EQ(ZX_ERR_NOT_DIR, file->ValidateFlags(ZX_FS_FLAG_DIRECTORY));
        fbl::RefPtr<fs::Vnode> redirect;
        EXPECT_EQ(ZX_OK, file->ValidateFlags(ZX_FS_RIGHT_READABLE));
        EXPECT_EQ(ZX_OK, file->Open(ZX_FS_RIGHT_READABLE, &redirect));
        EXPECT_NONNULL(redirect);
    }

    // no read handler, write handler
    {
        auto file = fbl::AdoptRef<fs::Vnode>(new fs::UnbufferedPseudoFile(nullptr, &DummyWriter));
        EXPECT_EQ(ZX_ERR_ACCESS_DENIED, file->ValidateFlags(ZX_FS_RIGHT_READABLE));
        EXPECT_EQ(ZX_ERR_ACCESS_DENIED, file->ValidateFlags(ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE));
        EXPECT_EQ(ZX_ERR_NOT_DIR, file->ValidateFlags(ZX_FS_FLAG_DIRECTORY));
        fbl::RefPtr<fs::Vnode> redirect;
        EXPECT_EQ(ZX_OK, file->ValidateFlags(ZX_FS_RIGHT_WRITABLE));
        EXPECT_EQ(ZX_OK, file->Open(ZX_FS_RIGHT_WRITABLE, &redirect));
        EXPECT_NONNULL(redirect);
    }

    // read handler, write handler
    {
        auto file = fbl::AdoptRef<fs::Vnode>(new fs::UnbufferedPseudoFile(&DummyReader, &DummyWriter));
        EXPECT_EQ(ZX_ERR_NOT_DIR, file->ValidateFlags(ZX_FS_FLAG_DIRECTORY));
        fbl::RefPtr<fs::Vnode> redirect;
        EXPECT_EQ(ZX_OK, file->ValidateFlags(ZX_FS_RIGHT_READABLE));
        EXPECT_EQ(ZX_OK, file->Open(ZX_FS_RIGHT_READABLE, &redirect));
        EXPECT_NONNULL(redirect);
        redirect.reset();
        EXPECT_EQ(ZX_OK, file->ValidateFlags(ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE));
        EXPECT_EQ(ZX_OK, file->Open(ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE, &redirect));
        EXPECT_NONNULL(redirect);
        redirect.reset();
        EXPECT_EQ(ZX_OK, file->ValidateFlags(ZX_FS_RIGHT_WRITABLE));
        EXPECT_EQ(ZX_OK, file->Open(ZX_FS_RIGHT_WRITABLE, &redirect));
        EXPECT_NONNULL(redirect);
    }

    END_TEST;
}

bool test_getattr_buffered() {
    BEGIN_TEST;

    // no read handler, no write handler
    {
        auto file = fbl::AdoptRef<fs::Vnode>(new fs::BufferedPseudoFile());
        vnattr_t attr;
        EXPECT_EQ(ZX_OK, file->Getattr(&attr));
        EXPECT_EQ(V_TYPE_FILE, attr.mode);
        EXPECT_EQ(1, attr.nlink);
        EXPECT_EQ(ZX_OK, file->ValidateFlags(ZX_FS_FLAG_VNODE_REF_ONLY));
        vnattr_t path_attr;
        EXPECT_EQ(ZX_OK, file->Getattr(&path_attr));
        EXPECT_BYTES_EQ((uint8_t*) &attr, (uint8_t*) &path_attr, sizeof(attr), "");
    }

    // read handler, no write handler
    {
        auto file = fbl::AdoptRef<fs::Vnode>(new fs::BufferedPseudoFile(&DummyReader));
        vnattr_t attr;
        EXPECT_EQ(ZX_OK, file->Getattr(&attr));
        EXPECT_EQ(V_TYPE_FILE | V_IRUSR, attr.mode);
        EXPECT_EQ(1, attr.nlink);

        fbl::RefPtr<fs::Vnode> redirect;
        EXPECT_EQ(ZX_OK, file->ValidateFlags(ZX_FS_RIGHT_READABLE));
        EXPECT_EQ(ZX_OK, file->Open(ZX_FS_RIGHT_READABLE, &redirect));
        vnattr_t open_attr;
        EXPECT_EQ(ZX_OK, file->Getattr(&open_attr));
        EXPECT_BYTES_EQ((uint8_t*)&attr, (uint8_t*)&open_attr, sizeof(attr), "");
    }

    // no read handler, write handler
    {
        auto file = fbl::AdoptRef<fs::Vnode>(new fs::BufferedPseudoFile(nullptr, &DummyWriter));
        vnattr_t attr;
        EXPECT_EQ(ZX_OK, file->Getattr(&attr));
        EXPECT_EQ(V_TYPE_FILE | V_IWUSR, attr.mode);
        EXPECT_EQ(1, attr.nlink);

        fbl::RefPtr<fs::Vnode> redirect;
        EXPECT_EQ(ZX_OK, file->ValidateFlags(ZX_FS_RIGHT_WRITABLE));
        EXPECT_EQ(ZX_OK, file->Open(ZX_FS_RIGHT_WRITABLE, &redirect));
        vnattr_t open_attr;
        EXPECT_EQ(ZX_OK, file->Getattr(&open_attr));
        EXPECT_BYTES_EQ((uint8_t*)&attr, (uint8_t*)&open_attr, sizeof(attr), "");
    }

    // read handler, write handler
    {
        auto file = fbl::AdoptRef<fs::Vnode>(new fs::BufferedPseudoFile(&DummyReader, &DummyWriter));
        vnattr_t attr;
        EXPECT_EQ(ZX_OK, file->Getattr(&attr));
        EXPECT_EQ(V_TYPE_FILE | V_IRUSR | V_IWUSR, attr.mode);
        EXPECT_EQ(1, attr.nlink);

        fbl::RefPtr<fs::Vnode> redirect;
        EXPECT_EQ(ZX_OK, file->ValidateFlags(ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE));
        EXPECT_EQ(ZX_OK, file->Open(ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE, &redirect));
        vnattr_t open_attr;
        EXPECT_EQ(ZX_OK, file->Getattr(&open_attr));
        EXPECT_BYTES_EQ((uint8_t*)&attr, (uint8_t*)&open_attr, sizeof(attr), "");
    }

    END_TEST;
}

bool test_getattr_unbuffered() {
    BEGIN_TEST;

    // no read handler, no write handler
    {
        auto file = fbl::AdoptRef<fs::Vnode>(new fs::UnbufferedPseudoFile());
        vnattr_t attr;
        EXPECT_EQ(ZX_OK, file->Getattr(&attr));
        EXPECT_EQ(V_TYPE_FILE, attr.mode);
        EXPECT_EQ(1, attr.nlink);

        EXPECT_EQ(ZX_OK, file->ValidateFlags(ZX_FS_FLAG_VNODE_REF_ONLY));
        vnattr_t path_attr;
        EXPECT_EQ(ZX_OK, file->Getattr(&path_attr));
        EXPECT_BYTES_EQ((uint8_t*) &attr, (uint8_t*) &path_attr, sizeof(attr), "");
    }

    // read handler, no write handler
    {
        auto file = fbl::AdoptRef<fs::Vnode>(new fs::UnbufferedPseudoFile(&DummyReader));
        vnattr_t attr;
        EXPECT_EQ(ZX_OK, file->Getattr(&attr));
        EXPECT_EQ(V_TYPE_FILE | V_IRUSR, attr.mode);
        EXPECT_EQ(1, attr.nlink);

        fbl::RefPtr<fs::Vnode> redirect;
        EXPECT_EQ(ZX_OK, file->ValidateFlags(ZX_FS_RIGHT_READABLE));
        EXPECT_EQ(ZX_OK, file->Open(ZX_FS_RIGHT_READABLE, &redirect));
        vnattr_t open_attr;
        EXPECT_EQ(ZX_OK, file->Getattr(&open_attr));
        EXPECT_BYTES_EQ((uint8_t*)&attr, (uint8_t*)&open_attr, sizeof(attr), "");
    }

    // no read handler, write handler
    {
        auto file = fbl::AdoptRef<fs::Vnode>(new fs::UnbufferedPseudoFile(nullptr, &DummyWriter));
        vnattr_t attr;
        EXPECT_EQ(ZX_OK, file->Getattr(&attr));
        EXPECT_EQ(V_TYPE_FILE | V_IWUSR, attr.mode);
        EXPECT_EQ(1, attr.nlink);

        fbl::RefPtr<fs::Vnode> redirect;
        EXPECT_EQ(ZX_OK, file->ValidateFlags(ZX_FS_RIGHT_WRITABLE));
        EXPECT_EQ(ZX_OK, file->Open(ZX_FS_RIGHT_WRITABLE, &redirect));
        vnattr_t open_attr;
        EXPECT_EQ(ZX_OK, file->Getattr(&open_attr));
        EXPECT_BYTES_EQ((uint8_t*)&attr, (uint8_t*)&open_attr, sizeof(attr), "");
    }

    // read handler, write handler
    {
        auto file = fbl::AdoptRef<fs::Vnode>(new fs::UnbufferedPseudoFile(&DummyReader, &DummyWriter));
        vnattr_t attr;
        EXPECT_EQ(ZX_OK, file->Getattr(&attr));
        EXPECT_EQ(V_TYPE_FILE | V_IRUSR | V_IWUSR, attr.mode);
        EXPECT_EQ(1, attr.nlink);

        fbl::RefPtr<fs::Vnode> redirect;
        EXPECT_EQ(ZX_OK, file->ValidateFlags(ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE));
        EXPECT_EQ(ZX_OK, file->Open(ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE, &redirect));
        vnattr_t open_attr;
        EXPECT_EQ(ZX_OK, file->Getattr(&open_attr));
        EXPECT_BYTES_EQ((uint8_t*)&attr, (uint8_t*)&open_attr, sizeof(attr), "");
    }

    END_TEST;
}

bool test_read_buffered() {
    BEGIN_TEST;

    VectorReader reader{"first", "second", "",
                        fbl::String(fbl::StringPiece("null\0null", 9u))};
    auto file = fbl::AdoptRef<fs::Vnode>(new fs::BufferedPseudoFile(reader.GetHandler()));

    {
        fbl::RefPtr<fs::Vnode> redirect;
        EXPECT_EQ(ZX_OK, file->ValidateFlags(ZX_FS_RIGHT_READABLE));
        EXPECT_EQ(ZX_OK, file->Open(ZX_FS_RIGHT_READABLE, &redirect));
        EXPECT_TRUE(CheckRead(redirect, ZX_OK, 0u, 0u, ""));
        EXPECT_TRUE(CheckRead(redirect, ZX_OK, 4u, 0u, "firs"));
        EXPECT_TRUE(CheckRead(redirect, ZX_OK, 4u, 2u, "rst"));
        EXPECT_TRUE(CheckRead(redirect, ZX_OK, 5u, 0u, "first"));
        EXPECT_TRUE(CheckRead(redirect, ZX_OK, 8u, 0u, "first"));
        EXPECT_EQ(ZX_OK, redirect->Close());
    }

    {
        fbl::RefPtr<fs::Vnode> redirect;
        EXPECT_EQ(ZX_OK, file->ValidateFlags(ZX_FS_RIGHT_READABLE));
        EXPECT_EQ(ZX_OK, file->Open(ZX_FS_RIGHT_READABLE, &redirect));
        EXPECT_TRUE(CheckRead(redirect, ZX_OK, 4u, 2u, "cond"));
        EXPECT_TRUE(CheckRead(redirect, ZX_OK, 6u, 0u, "second"));
        EXPECT_TRUE(CheckRead(redirect, ZX_OK, 8u, 0u, "second"));
        EXPECT_EQ(ZX_OK, redirect->Close());
    }

    {
        fbl::RefPtr<fs::Vnode> redirect;
        EXPECT_EQ(ZX_OK, file->ValidateFlags(ZX_FS_RIGHT_READABLE));
        EXPECT_EQ(ZX_OK, file->Open(ZX_FS_RIGHT_READABLE, &redirect));
        EXPECT_TRUE(CheckRead(redirect, ZX_OK, 4u, 0u, ""));
        EXPECT_TRUE(CheckRead(redirect, ZX_OK, 4u, 2u, ""));
        EXPECT_EQ(ZX_OK, redirect->Close());
    }

    {
        fbl::RefPtr<fs::Vnode> redirect;
        EXPECT_EQ(ZX_OK, file->Open(ZX_FS_RIGHT_READABLE, &redirect));
        EXPECT_TRUE(CheckRead(redirect, ZX_OK, 0u, 0u, ""));
        EXPECT_TRUE(CheckRead(redirect, ZX_OK, 4u, 0u, "null"));
        EXPECT_TRUE(CheckRead(redirect, ZX_OK, 4u, 2u, fbl::StringPiece("ll\0n", 4u)));
        EXPECT_TRUE(CheckRead(redirect, ZX_OK, 9u, 0u, fbl::StringPiece("null\0null", 9u)));
        EXPECT_TRUE(CheckRead(redirect, ZX_OK, 12u, 0u, fbl::StringPiece("null\0null", 9u)));
        EXPECT_EQ(ZX_OK, redirect->Close());
    }

    {
        fbl::RefPtr<fs::Vnode> redirect;
        EXPECT_EQ(ZX_ERR_IO, file->Open(ZX_FS_RIGHT_READABLE, &redirect));
    }

    END_TEST;
}

bool test_read_unbuffered() {
    BEGIN_TEST;

    VectorReader reader{"first", "second", "third", "fourth", "fifth", "",
                        fbl::String(fbl::StringPiece("null\0null", 9u))};
    auto file = fbl::AdoptRef<fs::Vnode>(new fs::UnbufferedPseudoFile(reader.GetHandler()));

    {
        fbl::RefPtr<fs::Vnode> redirect;
        EXPECT_EQ(ZX_OK, file->ValidateFlags(ZX_FS_RIGHT_READABLE));
        EXPECT_EQ(ZX_OK, file->Open(ZX_FS_RIGHT_READABLE, &redirect));
        EXPECT_TRUE(CheckRead(redirect, ZX_OK, 0u, 0u, ""));
        EXPECT_TRUE(CheckRead(redirect, ZX_OK, 4u, 0u, "seco"));
        EXPECT_TRUE(CheckRead(redirect, ZX_OK, 4u, 2u, ""));
        EXPECT_TRUE(CheckRead(redirect, ZX_OK, 3u, 0u, "thi"));
        EXPECT_TRUE(CheckRead(redirect, ZX_OK, 6u, 0u, "fourth"));
        EXPECT_EQ(ZX_OK, redirect->Close());
    }

    {
        fbl::RefPtr<fs::Vnode> redirect;
        EXPECT_EQ(ZX_OK, file->ValidateFlags(ZX_FS_RIGHT_READABLE));
        EXPECT_EQ(ZX_OK, file->Open(ZX_FS_RIGHT_READABLE, &redirect));
        EXPECT_TRUE(CheckRead(redirect, ZX_OK, 8u, 0u, "fifth"));
        EXPECT_TRUE(CheckRead(redirect, ZX_OK, 4u, 0u, ""));
        EXPECT_TRUE(CheckRead(redirect, ZX_OK, 12u, 0u, fbl::StringPiece("null\0null", 9u)));
        EXPECT_TRUE(CheckRead(redirect, ZX_ERR_IO, 0u, 0u, ""));
        EXPECT_EQ(ZX_OK, redirect->Close());
    }

    END_TEST;
}

bool test_write_buffered() {
    BEGIN_TEST;

    VectorWriter writer(6u);
    auto file = fbl::AdoptRef<fs::Vnode>(new fs::BufferedPseudoFile(nullptr, writer.GetHandler(), 10u));

    {
        fbl::RefPtr<fs::Vnode> redirect;
        EXPECT_EQ(ZX_OK, file->ValidateFlags(ZX_FS_RIGHT_WRITABLE));
        EXPECT_EQ(ZX_OK, file->Open(ZX_FS_RIGHT_WRITABLE, &redirect));
        EXPECT_TRUE(CheckWrite(redirect, ZX_OK, 0u, "fixx", 4u));
        EXPECT_TRUE(CheckWrite(redirect, ZX_OK, 0u, "", 0u));
        EXPECT_TRUE(CheckWrite(redirect, ZX_OK, 2u, "rst", 3u));
        EXPECT_EQ(ZX_OK, redirect->Close());
    }

    {
        fbl::RefPtr<fs::Vnode> redirect;
        EXPECT_EQ(ZX_OK, file->ValidateFlags(ZX_FS_RIGHT_WRITABLE));
        EXPECT_EQ(ZX_OK, file->Open(ZX_FS_RIGHT_WRITABLE, &redirect));
        EXPECT_TRUE(CheckWrite(redirect, ZX_OK, 0u, "second", 6u));
        EXPECT_EQ(ZX_OK, redirect->Close());
    }

    {
        fbl::RefPtr<fs::Vnode> redirect;
        EXPECT_EQ(ZX_OK, file->ValidateFlags(ZX_FS_RIGHT_WRITABLE));
        EXPECT_EQ(ZX_OK, file->Open(ZX_FS_RIGHT_WRITABLE, &redirect));
        EXPECT_EQ(ZX_OK, redirect->Close());
    }

    {
        fbl::RefPtr<fs::Vnode> redirect;
        EXPECT_EQ(ZX_OK, file->ValidateFlags(ZX_FS_RIGHT_WRITABLE));
        EXPECT_EQ(ZX_OK, file->Open(ZX_FS_RIGHT_WRITABLE, &redirect));
        EXPECT_TRUE(CheckAppend(redirect, ZX_OK, "thxrxxx", 7u, 7u));
        EXPECT_TRUE(CheckWrite(redirect, ZX_OK, 2u, "i", 1u));
        EXPECT_EQ(ZX_OK, redirect->Truncate(4u));
        EXPECT_TRUE(CheckAppend(redirect, ZX_OK, "d", 5u, 1u));
        EXPECT_EQ(ZX_OK, redirect->Close());
    }

    {
        fbl::RefPtr<fs::Vnode> redirect;
        EXPECT_EQ(ZX_OK, file->ValidateFlags(ZX_FS_RIGHT_WRITABLE));
        EXPECT_EQ(ZX_OK, file->Open(ZX_FS_RIGHT_WRITABLE, &redirect));
        EXPECT_TRUE(CheckWrite(redirect, ZX_OK, 0u, "null", 4u));
        EXPECT_EQ(ZX_OK, redirect->Truncate(5u));
        EXPECT_TRUE(CheckAppend(redirect, ZX_OK, "null", 9u, 4u));
        EXPECT_EQ(ZX_OK, redirect->Close());
    }

    {
        fbl::RefPtr<fs::Vnode> redirect;
        EXPECT_EQ(ZX_OK, file->ValidateFlags(ZX_FS_RIGHT_WRITABLE));
        EXPECT_EQ(ZX_OK, file->Open(ZX_FS_RIGHT_WRITABLE, &redirect));
        EXPECT_EQ(ZX_ERR_NO_SPACE, redirect->Truncate(11u));
        EXPECT_TRUE(CheckAppend(redirect, ZX_OK, "too-long", 8u, 8u));
        EXPECT_TRUE(CheckAppend(redirect, ZX_OK, "-off-the-end", 10u, 2u));
        EXPECT_TRUE(CheckAppend(redirect, ZX_ERR_NO_SPACE, "-overflow", 0u, 0u));
        EXPECT_EQ(ZX_OK, redirect->Close());
    }

    {
        fbl::RefPtr<fs::Vnode> redirect;
        EXPECT_EQ(ZX_OK, file->ValidateFlags(ZX_FS_RIGHT_WRITABLE));
        EXPECT_EQ(ZX_OK, file->Open(ZX_FS_RIGHT_WRITABLE, &redirect));
        EXPECT_EQ(ZX_ERR_IO, redirect->Close());
    }

    EXPECT_EQ(6u, writer.strings().size());
    EXPECT_FSTR_EQ(writer.strings()[0], fbl::String("first"));
    EXPECT_FSTR_EQ(writer.strings()[1], fbl::String("second"));
    EXPECT_FSTR_EQ(writer.strings()[2], fbl::String(""));
    EXPECT_FSTR_EQ(writer.strings()[3], fbl::String("third"));
    EXPECT_FSTR_EQ(writer.strings()[4], fbl::String("null\0null", 9u));
    EXPECT_FSTR_EQ(writer.strings()[5], fbl::String("too-long-o"));

    END_TEST;
}

bool test_write_unbuffered() {
    BEGIN_TEST;

    VectorWriter writer(12u);
    auto file = fbl::AdoptRef<fs::Vnode>(new fs::UnbufferedPseudoFile(nullptr, writer.GetHandler()));

    {
        fbl::RefPtr<fs::Vnode> redirect;
        EXPECT_EQ(ZX_OK, file->ValidateFlags(ZX_FS_RIGHT_WRITABLE));
        EXPECT_EQ(ZX_OK, file->Open(ZX_FS_RIGHT_WRITABLE, &redirect));
        EXPECT_TRUE(CheckWrite(redirect, ZX_OK, 0u, "first", 5u));
        EXPECT_TRUE(CheckWrite(redirect, ZX_ERR_NO_SPACE, 2u, "xxx", 0u));
        EXPECT_TRUE(CheckWrite(redirect, ZX_OK, 0u, "second", 6u));
        EXPECT_EQ(ZX_OK, redirect->Close());
    }

    {
        fbl::RefPtr<fs::Vnode> redirect;
        EXPECT_EQ(ZX_OK, file->ValidateFlags(ZX_FS_RIGHT_WRITABLE));
        EXPECT_EQ(ZX_OK, file->Open(ZX_FS_RIGHT_WRITABLE, &redirect));
        EXPECT_TRUE(CheckWrite(redirect, ZX_OK, 0u, "", 0u));
        EXPECT_TRUE(CheckAppend(redirect, ZX_OK, "third", 5u, 5u));
        EXPECT_TRUE(CheckAppend(redirect, ZX_OK, fbl::StringPiece("null\0null", 9u), 9u, 9u));
        EXPECT_EQ(ZX_OK, redirect->Close());
    }

    {
        fbl::RefPtr<fs::Vnode> redirect;
        EXPECT_EQ(ZX_OK, file->ValidateFlags(ZX_FS_RIGHT_WRITABLE | ZX_FS_FLAG_TRUNCATE));
        EXPECT_EQ(ZX_OK, file->Open(ZX_FS_RIGHT_WRITABLE | ZX_FS_FLAG_TRUNCATE, &redirect));
        EXPECT_EQ(ZX_OK, redirect->Close());
    }

    {
        fbl::RefPtr<fs::Vnode> redirect;
        EXPECT_EQ(ZX_OK, file->ValidateFlags(ZX_FS_RIGHT_WRITABLE | ZX_FS_FLAG_CREATE));
        EXPECT_EQ(ZX_OK, file->Open(ZX_FS_RIGHT_WRITABLE | ZX_FS_FLAG_CREATE, &redirect));
        EXPECT_EQ(ZX_OK, redirect->Close());
    }

    {
        fbl::RefPtr<fs::Vnode> redirect;
        EXPECT_EQ(ZX_OK, file->ValidateFlags(ZX_FS_RIGHT_WRITABLE));
        EXPECT_EQ(ZX_OK, file->Open(ZX_FS_RIGHT_WRITABLE, &redirect));
        EXPECT_EQ(ZX_OK, redirect->Truncate(0u));
        EXPECT_EQ(ZX_OK, redirect->Close());
    }

    {
        fbl::RefPtr<fs::Vnode> redirect;
        EXPECT_EQ(ZX_OK, file->ValidateFlags(ZX_FS_RIGHT_WRITABLE));
        EXPECT_EQ(ZX_OK, file->Open(ZX_FS_RIGHT_WRITABLE, &redirect));
        EXPECT_TRUE(CheckAppend(redirect, ZX_OK, "fourth", 6u, 6u));
        EXPECT_EQ(ZX_OK, redirect->Close());
    }

    {
        fbl::RefPtr<fs::Vnode> redirect;
        EXPECT_EQ(ZX_OK, file->ValidateFlags(ZX_FS_RIGHT_WRITABLE));
        EXPECT_EQ(ZX_OK, file->Open(ZX_FS_RIGHT_WRITABLE, &redirect));
        EXPECT_EQ(ZX_OK, redirect->Close());
    }

    {
        fbl::RefPtr<fs::Vnode> redirect;
        EXPECT_EQ(ZX_OK, file->ValidateFlags(ZX_FS_RIGHT_WRITABLE));
        EXPECT_EQ(ZX_OK, file->Open(ZX_FS_RIGHT_WRITABLE, &redirect));
        EXPECT_TRUE(CheckAppend(redirect, ZX_OK, "fifth", 5u, 5u));
        EXPECT_EQ(ZX_ERR_INVALID_ARGS, redirect->Truncate(10u));
        EXPECT_EQ(ZX_OK, redirect->Truncate(0u));
        EXPECT_EQ(ZX_OK, redirect->Close());
    }

    {
        fbl::RefPtr<fs::Vnode> redirect;
        EXPECT_EQ(ZX_OK, file->ValidateFlags(ZX_FS_RIGHT_WRITABLE));
        EXPECT_EQ(ZX_OK, file->Open(ZX_FS_RIGHT_WRITABLE, &redirect));
        EXPECT_TRUE(CheckWrite(redirect, ZX_OK, 0u, "a long string", 13u));
        EXPECT_EQ(ZX_OK, redirect->Truncate(0u));
        EXPECT_EQ(ZX_ERR_IO, redirect->Close());
    }

    EXPECT_EQ(12u, writer.strings().size());
    EXPECT_FSTR_EQ(writer.strings()[0], fbl::String("first"));
    EXPECT_FSTR_EQ(writer.strings()[1], fbl::String("second"));
    EXPECT_FSTR_EQ(writer.strings()[2], fbl::String(""));
    EXPECT_FSTR_EQ(writer.strings()[3], fbl::String("third"));
    EXPECT_FSTR_EQ(writer.strings()[4], fbl::String("null\0null", 9u));
    EXPECT_FSTR_EQ(writer.strings()[5], fbl::String(""));
    EXPECT_FSTR_EQ(writer.strings()[6], fbl::String(""));
    EXPECT_FSTR_EQ(writer.strings()[7], fbl::String(""));
    EXPECT_FSTR_EQ(writer.strings()[8], fbl::String("fourth"));
    EXPECT_FSTR_EQ(writer.strings()[9], fbl::String("fifth"));
    EXPECT_FSTR_EQ(writer.strings()[10], fbl::String(""));
    EXPECT_FSTR_EQ(writer.strings()[11], fbl::String("a long string"));

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(pseudo_file_tests)
RUN_TEST(test_open_validation_buffered)
RUN_TEST(test_open_validation_unbuffered)
RUN_TEST(test_getattr_buffered)
RUN_TEST(test_getattr_unbuffered)
RUN_TEST(test_read_buffered)
RUN_TEST(test_read_unbuffered)
RUN_TEST(test_write_buffered)
RUN_TEST(test_write_unbuffered)
END_TEST_CASE(pseudo_file_tests)
