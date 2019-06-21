// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include <lib/fit/function.h>
#include <zircon/assert.h>
#include <zxtest/base/death-statement.h>

#include "test-registry.h"

namespace zxtest {
namespace test {
using internal::DeathStatement;

void DeathStatementCrash() {
    DeathStatement crashing_statement([]() { ZX_ASSERT(false); });

    ZX_ASSERT(crashing_statement.state() == DeathStatement::State::kUnknown);
    crashing_statement.Execute();
    ZX_ASSERT(crashing_statement.state() == DeathStatement::State::kException);
}

void DeathStatementNoCrash() {
    DeathStatement statement([]() { ZX_ASSERT(true); });

    ZX_ASSERT(statement.state() == DeathStatement::State::kUnknown);
    statement.Execute();
    ZX_ASSERT(statement.state() == DeathStatement::State::kSuccess);
}

void DeathStatementInternalError() {
    std::function<void()> fn;
    DeathStatement error_statement(std::move(fn));

    ZX_ASSERT(error_statement.state() == DeathStatement::State::kUnknown);
    error_statement.Execute();
    ZX_ASSERT(error_statement.state() == DeathStatement::State::kInternalError);
}

} // namespace test
} // namespace zxtest
