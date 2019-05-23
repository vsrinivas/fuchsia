// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <string>

namespace examples {
namespace fuzzing {

// A Foo doohicky
struct Foo {
    int bar_;

    Foo(int bar) : bar_(bar) {}
    ~Foo() {}
};

// A Baz thingamajig
class Baz {
public:
    Baz() : foo_(nullptr), bar_(nullptr) {}
    ~Baz() {}

    int Execute(const std::string &commands);

private:
    void SetFoo(std::unique_ptr<Foo> foo);
    void SetBar(int bar);

    std::unique_ptr<Foo> foo_;
    int *bar_; // Cache bar_ for faster access
};

} // namespace fuzzing
} // namespace examples

