// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdlib>
#include <fbl/string_buffer.h>
#include <lib/log-writer-textfile/log-writer-textfile.h>
#include <lib/log/log.h>
#include <lib/log/log_writer.h>

namespace {

class TextFileWriter final : public log_writer {
public:
    explicit TextFileWriter(FILE* file)
        : log_writer{&kOps}, file_(file) {}

    void Write(const log_message_t* msg);

private:
    static const log_writer_ops_t kOps;

    FILE* file_;
};

void textfile_writer_write(log_writer_t* writer, const log_message_t* message) {
    auto self = static_cast<TextFileWriter*>(writer);
    self->Write(message);
}

const log_writer_ops_t TextFileWriter::kOps = {
    .version = LOG_WRITER_OPS_V1,
    .reserved = 0,
    .v1 = {
        .write = textfile_writer_write,
    },
};

void TextFileWriter::Write(const log_message* message) {
    constexpr size_t kMaxMessageSize = 2043;

    fbl::StringBuffer<kMaxMessageSize> buf;
    buf.Append("[");
    if (message->level >= LOG_LEVEL_INFO) {
        switch (message->level) {
        case LOG_LEVEL_INFO:
            buf.Append("INFO ");
            break;
        case LOG_LEVEL_WARNING:
            buf.Append("WARNING ");
            break;
        case LOG_LEVEL_ERROR:
            buf.Append("ERROR ");
            break;
        case LOG_LEVEL_FATAL:
            buf.Append("FATAL ");
            break;
        default:
            buf.Append("UNKNOWN_LEVEL ");
            break;
        }
    } else {
        buf.AppendPrintf("VERBOSITY:%d ", -(message->level));
    }
    int tag_counter = 0;
    buf.Append("TAGS:[");
    for (size_t i = 0; i < message->num_static_tags; i++) {
        if (++tag_counter > LOG_MAX_TAGS) {
            break;
        }
        if (tag_counter > 1) {
            buf.Append(", ");
        }
        buf.Append(message->static_tags[i]);
    }
    for (size_t i = 0; i < message->num_dynamic_tags; i++) {
        if (++tag_counter > LOG_MAX_TAGS) {
            break;
        }
        if (tag_counter > 1) {
            buf.Append(", ");
        }
        buf.Append(message->dynamic_tags[i]);
    }
    buf.Append("]] ");
    buf.Append(message->text, message->text_len);
    buf.Append("\n");
    fwrite(buf.data(), 1, buf.size(), file_);
}

} // namespace
log_writer_t* log_create_textfile_writer(FILE* log_destination) {
    ZX_DEBUG_ASSERT(log_destination != nullptr);
    return new TextFileWriter(log_destination);
}

void log_destroy_textfile_writer(log_writer_t* writer) {
    delete static_cast<TextFileWriter*>(writer);
}
