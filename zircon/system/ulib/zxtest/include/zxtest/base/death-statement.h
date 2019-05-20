// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <lib/fit/function.h>
#include <lib/zx/channel.h>
#include <lib/zx/port.h>
#include <zxtest/base/test-driver.h>
#include <zxtest/base/test-internal.h>
#include <zxtest/base/test.h>

namespace zxtest {
namespace internal {
// A Statement to be executed which can throw an exception.
// This statement will be executed in a separate thread. The calling thread
// will be blocked until the statement completes its execution.
//
// The statement being executed is allowed to use ASSERT_/EXPECT_ mechanisms.
class DeathStatement {
public:
    // Possible results of executing this statement.
    enum class State {
        // Statement was never executed.
        kUnknown,
        // Statement execution started.
        kStarted,
        // Part of the setup required to execute the death statement might have failed.
        kInternalError,
        // Statement executed without exceptions.
        kSuccess,
        // Statement executed with exceptions, but handled gracefully.
        kException,
        // Statement executed with exceptions, but was not handled properly (leaked resources).
        kBadState,
    };

    DeathStatement() = delete;
    // Take ownership of the closure, explicit move semantics.
    explicit DeathStatement(fit::function<void()> statement);
    DeathStatement(const DeathStatement&) = delete;
    DeathStatement(DeathStatement&&) = default;
    DeathStatement& operator=(const DeathStatement&) = delete;
    DeathStatement& operator=(DeathStatement&&) = delete;
    ~DeathStatement() = default;

    // Executes the statement in separate thread.
    void Execute();

    // Returns the current state of the statement.
    State state() const { return state_; }

    const std::string_view error_message() const { return error_message_; }

private:
    fit::function<void()> statement_;

    // Blocks the main thread until |event_port| receives a packet notifying that the
    // exception_channel has become readable.
    void Listen(const zx::port& event_port, const zx::channel& exception_channel);

    // Returns true if the exception was handled and false if it was ignored.
    bool HandleException(const zx::channel& exception_channel);

    // Internal error description.
    std::string error_message_;
    State state_ = State::kUnknown;
};
} // namespace internal

} // namespace zxtest
