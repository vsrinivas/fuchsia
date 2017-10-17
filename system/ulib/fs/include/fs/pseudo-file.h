// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/function.h>
#include <fbl/macros.h>
#include <fbl/ref_ptr.h>
#include <fbl/string.h>

#include "vnode.h"

namespace fs {

// A pseudo-file is a file-like object whose content is generated and modified
// dynamically on-the-fly by invoking handler functions rather than being
// directly persisted as a sequence of bytes.
//
// This class is designed to allow programs to publish read-only, write-only,
// or read-write properties such as configuration options, debug flags,
// and dumps of internal state which may change dynamically.
//
// A pseudo-file is readable when it has a non-null |ReadHandler|.  Typically
// the read handler will output a UTF-8 representation of some element of
// the program's state, or return an error if the requested information is not
// available.  The read handler is not expected to have side-effects (but it can).
//
// A pseudo-file is writable when it has a non-null |WriteHandler|.  Typically
// the write handler will parse the input in a UTF-8 representation and update
// the program's state in response, or return an error if the input is invalid.
//
// Although pseudo-files usually contain text, they can also be used for binary data.
//
// There is no guarantee that data written to the pseudo-file can be read back
// from the pseudo-file in the same form; it's not a real file after all.
//
// This is an abstract class.  The concrete implementations are
// |BufferedPseudoFile| and |UnbufferedPseudoFile|.
class PseudoFile : public Vnode {
public:
    // Handler called to read from the pseudo-file.
    using ReadHandler = fbl::Function<zx_status_t(fbl::String* output)>;

    // Handler called to write into the pseudo-file.
    using WriteHandler = fbl::Function<zx_status_t(fbl::StringPiece input)>;

    ~PseudoFile() override;

    // |Vnode| implementation:
    zx_status_t ValidateFlags(uint32_t flags) override;
    zx_status_t Getattr(vnattr_t* a) final;

protected:
    PseudoFile(ReadHandler read_handler, WriteHandler write_handler);

    ReadHandler const read_handler_;
    WriteHandler const write_handler_;

    DISALLOW_COPY_ASSIGN_AND_MOVE(PseudoFile);
};

// Buffered pseudo-file.
//
// This variant is optimized for incrementally reading and writing properties
// which are larger than can typically be read or written by the client in
// a single I/O transaction.
//
// In read mode, the pseudo-file invokes its read handler when the file is opened
// and retains the content in an output buffer which the client incrementally reads
// from and can seek within.
//
// In write mode, the client incrementally writes into and seeks within an input
// buffer which the pseudo-file delivers as a whole to the write handler when the
// file is closed.  Truncation is also supported.
//
// Each client has its own separate output and input buffers.  Writing into the
// output buffer does not affect the contents of the client's input buffer or that
// of any other client.  Changes to the underlying state of the pseudo-file are not
// observed by the client until it closes and re-opens the file.
//
// This class is thread-safe.
class BufferedPseudoFile : public PseudoFile {
public:
    // Creates a buffered pseudo-file.
    //
    // If the |read_handler| is null, then the pseudo-file is considered not readable.
    // If the |write_handler| is null, then the pseudo-file is considered not writable.
    // The |input_buffer_capacity| determines the maximum number of bytes which can be
    // written to the pseudo-file's input buffer when it it opened for writing.
    BufferedPseudoFile(ReadHandler read_handler = ReadHandler(),
                       WriteHandler write_handler = WriteHandler(),
                       size_t input_buffer_capacity = 1024);

    ~BufferedPseudoFile() override;

    // |Vnode| implementation:
    zx_status_t Open(uint32_t flags, fbl::RefPtr<Vnode>* out_redirect) final;

private:
    class Content final : public Vnode {
    public:
        Content(fbl::RefPtr<BufferedPseudoFile> file, uint32_t flags, fbl::String output);
        ~Content() override;

        // |Vnode| implementation:
        zx_status_t ValidateFlags(uint32_t flags) final;
        zx_status_t Close() final;
        zx_status_t Getattr(vnattr_t* a) final;
        zx_status_t Read(void* data, size_t length, size_t offset, size_t* out_actual) final;
        zx_status_t Write(const void* data, size_t length, size_t offset, size_t* out_actual) final;
        zx_status_t Append(const void* data, size_t length, size_t* out_end, size_t* out_actual) final;
        zx_status_t Truncate(size_t length) final;

    private:
        void SetInputLength(size_t length);

        fbl::RefPtr<BufferedPseudoFile> const file_;
        uint32_t const flags_;
        fbl::String const output_;

        char* input_data_ = nullptr;
        size_t input_length_ = 0u;
    };

    size_t const input_buffer_capacity_;

    DISALLOW_COPY_ASSIGN_AND_MOVE(BufferedPseudoFile);
};

// Unbuffered pseudo-file.
//
// This variant is optimized for atomically reading and writing small properties.
// Unlike buffered pseudo-files, it is not necessary to re-open the pseudo-file to
// observe side-effects; the client can simply seek back to the zero offset and
// read or write again.
//
// Because reads and writes are not buffered, the maximum size of the property
// is limited to what will fit in a single I/O transaction.  Unbuffered pseudo-files
// generally work best for properties which are likely to be polled or repeatedly
// modified and which are no larger than the nominal I/O buffer size used by the
// intended clients.
//
// As a conservative guideline, we recommend using |BufferedPseudoFile| instead
// for content larger than |PAGE_SIZE|.
//
// In read mode, the pseudo-file invokes its read handler each time |Read()|
// is called with a seek offset of 0, returning at most as many bytes as the
// client requested and discarding the remainder (if any).
//
// Reading with a non-zero seek offset returns empty data, indicating end of file.
//
// In write mode, the pseudo-file invokes its write handler each time |Write()|
// with a seek offset of 0 is called, passing all of the bytes written by the
// client as the input string.  Likewise, |Append()| invokes the write handler
// each time it is called and returns a new end of file offset of 0.
//
// Writing with a non-zero seek offset returns |ZX_ERR_NO_SPACE|, indicating an
// attempt to write data beyond what was accepted by the write handler.
//
// Opening the file in create mode or truncating it to zero length then closing
// it without an intervening write is equivalent to writing 0 bytes.  This
// adaptation improves compatibility with command-line operations which are
// intended to modify the file in-place such as: `echo "data" > pseudo-file`.
//
// Truncating to a non-zero length returns |ZX_ERR_INVALID_ARGS|.
//
// This class is thread-safe.
class UnbufferedPseudoFile : public PseudoFile {
public:
    // Creates an unbuffered pseudo-file.
    //
    // If the |read_handler| is null, then the pseudo-file is considered not readable.
    // If the |write_handler| is null, then the pseudo-file is considered not writable.
    UnbufferedPseudoFile(ReadHandler read_handler = ReadHandler(),
                         WriteHandler write_handler = WriteHandler());

    ~UnbufferedPseudoFile() override;

    // |Vnode| implementation:
    zx_status_t Open(uint32_t flags, fbl::RefPtr<Vnode>* out_redirect) final;

private:
    class Content final : public Vnode {
    public:
        Content(fbl::RefPtr<UnbufferedPseudoFile> file, uint32_t flags);
        ~Content() override;

        // |Vnode| implementation:
        zx_status_t Open(uint32_t flags, fbl::RefPtr<Vnode>* out_redirect) final;
        zx_status_t Close() final;
        zx_status_t Getattr(vnattr_t* a) final;
        zx_status_t Read(void* data, size_t length, size_t offset, size_t* out_actual) final;
        zx_status_t Write(const void* data, size_t length, size_t offset, size_t* out_actual) final;
        zx_status_t Append(const void* data, size_t length, size_t* out_end, size_t* out_actual) final;
        zx_status_t Truncate(size_t length) final;

    private:
        fbl::RefPtr<UnbufferedPseudoFile> const file_;
        uint32_t const flags_;

        bool truncated_since_last_successful_write_;
    };

    DISALLOW_COPY_ASSIGN_AND_MOVE(UnbufferedPseudoFile);
};

} // namespace fs
