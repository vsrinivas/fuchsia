// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/bindings/synchronous_interface_ptr.h"

#include <thread>
#include <utility>

#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fidl/cpp/bindings/strong_binding.h"
#include "lib/fidl/compiler/interfaces/tests/math_calculator.fidl-sync.h"
#include "lib/fidl/compiler/interfaces/tests/math_calculator.fidl.h"
#include "lib/fidl/compiler/interfaces/tests/scoping.fidl-sync.h"
#include "lib/fidl/compiler/interfaces/tests/scoping.fidl.h"

namespace fidl {
namespace test {
namespace {

using CalcCallback = fidl::Callback<void(double)>;

// This runs in a separate thread.
class MathCalculatorImpl : public math::Calculator {
 public:
  explicit MathCalculatorImpl(InterfaceRequest<math::Calculator> request)
      : total_(0.0), binding_(this, request.Pass()) {}
  ~MathCalculatorImpl() override {}

  void Clear(const CalcCallback& callback) override {
    total_ = 0.0;
    callback.Run(total_);
  }

  void Add(double value, const CalcCallback& callback) override {
    total_ += value;
    callback.Run(total_);
  }

  void Multiply(double value, const CalcCallback& callback) override {
    total_ *= value;
    callback.Run(total_);
  }

 private:
  double total_;
  Binding<math::Calculator> binding_;
};

class SynchronousInterfacePtrTest : public testing::Test {
 public:
  ~SynchronousInterfacePtrTest() override { loop_.RunUntilIdle(); }

  void PumpMessages() { loop_.RunUntilIdle(); }

  // This is meant to be passed in to an std::thread -- the thread will have its
  // own RunLoop!
  static void StartMathCalculator(InterfaceRequest<math::Calculator> server) {
    // Runloop is thread-local, and this is what the MathCalculatorImpl will end
    // up using.
    RunLoop loop;
    MathCalculatorImpl calc_impl(std::move(server));
    loop.Run();
  }

 private:
  RunLoop loop_;
};

TEST_F(SynchronousInterfacePtrTest, IsBound) {
  SynchronousInterfacePtr<math::Calculator> calc;
  EXPECT_FALSE(calc.is_bound());
  EXPECT_FALSE(calc);

  MathCalculatorImpl calc_impl(GetSynchronousProxy(&calc));
  EXPECT_TRUE(calc.is_bound());
  EXPECT_TRUE(calc);
}

// Do an end to end
TEST_F(SynchronousInterfacePtrTest, EndToEnd) {
  SynchronousInterfacePtr<math::Calculator> calc;
  std::thread server(StartMathCalculator, GetSynchronousProxy(&calc));

  double out;
  calc->Add(2.0, &out);
  calc->Multiply(5.0, &out);
  EXPECT_EQ(10.0, out);

  calc.PassInterfaceHandle();
  server.join();
}

// Move them around.
TEST_F(SynchronousInterfacePtrTest, Movable) {
  SynchronousInterfacePtr<math::Calculator> a;
  SynchronousInterfacePtr<math::Calculator> b;
  MathCalculatorImpl calc_impl(GetSynchronousProxy(&b));

  EXPECT_TRUE(!a);
  EXPECT_FALSE(!b);

  a = std::move(b);

  EXPECT_FALSE(!a);
  EXPECT_TRUE(!b);
}

// Test ::reset() and the explicit bool operator.
TEST_F(SynchronousInterfacePtrTest, Resettable) {
  MessagePipe pipe;
  // Save this so we can test it later.
  Handle handle = pipe.handle0.get();

  SynchronousInterfacePtr<math::Calculator> a;
  EXPECT_TRUE(!a);
  a = SynchronousInterfacePtr<math::Calculator>::Create(
      InterfaceHandle<math::Calculator>(std::move(pipe.handle0)));
  EXPECT_FALSE(!a);

  a.reset();
  EXPECT_TRUE(!a);

  // Test that handle was closed.
  EXPECT_EQ(MOJO_SYSTEM_RESULT_INVALID_ARGUMENT, CloseRaw(handle));
}

TEST_F(SynchronousInterfacePtrTest, SetNull) {
  MessagePipe pipe;
  auto a = SynchronousInterfacePtr<math::Calculator>::Create(
      InterfaceHandle<math::Calculator>(std::move(pipe.handle0)));

  EXPECT_TRUE(a);
  a = nullptr;
  EXPECT_FALSE(a);
}

// SynchronousInterfacePtr<>  will return false on method invocations if its
// underlying end of the pipe is dead.
TEST_F(SynchronousInterfacePtrTest, EncounteredError) {
  SynchronousInterfacePtr<math::Calculator> calc;
  {
    MathCalculatorImpl calc_impl(GetSynchronousProxy(&calc));
    EXPECT_TRUE(calc.is_bound());
  }

  EXPECT_TRUE(calc.is_bound());

  double out = 0.0;
  EXPECT_FALSE(calc->Add(2.0, &out));
  EXPECT_EQ(0.0, out);
}

class CImpl : public C {
 public:
  CImpl(bool* d_called, InterfaceRequest<C> request)
      : d_called_(d_called), binding_(this, request.Pass()) {}
  ~CImpl() override {}

 private:
  void D() override { *d_called_ = true; }

  bool* d_called_;
  StrongBinding<C> binding_;
};

class BImpl : public B {
 public:
  BImpl(bool* d_called, InterfaceRequest<B> request)
      : d_called_(d_called), binding_(this, request.Pass()) {}
  ~BImpl() override {}

 private:
  void GetC(InterfaceRequest<C> c) override { new CImpl(d_called_, c.Pass()); }

  bool* d_called_;
  StrongBinding<B> binding_;
};

class AImpl : public A {
 public:
  explicit AImpl(InterfaceRequest<A> request)
      : d_called_(false), binding_(this, request.Pass()) {}
  ~AImpl() override {}

  bool d_called() const { return d_called_; }

 private:
  void GetB(InterfaceRequest<B> b) override { new BImpl(&d_called_, b.Pass()); }

  bool d_called_;
  Binding<A> binding_;
};

// Test that, for synchronous method calls that don't have return args, the
// bindings return right away, leaving the messages in the channel while
// closing their end.
TEST_F(SynchronousInterfacePtrTest, Scoping) {
  SynchronousInterfacePtr<A> a;
  AImpl a_impl(GetSynchronousProxy(&a));

  EXPECT_FALSE(a_impl.d_called());

  {
    SynchronousInterfacePtr<B> b;
    a->GetB(GetSynchronousProxy(&b));
    SynchronousInterfacePtr<C> c;
    b->GetC(GetSynchronousProxy(&c));
    c->D();
  }

  // While B & C have fallen out of scope, the service-side endpoints of the
  // pipes will remain until they are flushed.
  EXPECT_FALSE(a_impl.d_called());
  PumpMessages();
  EXPECT_TRUE(a_impl.d_called());
}

}  // namespace
}  // namespace test
}  // namespace fidl
