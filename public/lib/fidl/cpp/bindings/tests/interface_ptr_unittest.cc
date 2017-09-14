// Copyright 2013 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>
#include <functional>

#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/tests/util/test_waiter.h"
#include "lib/fidl/compiler/interfaces/tests/math_calculator.fidl.h"
#include "lib/fidl/compiler/interfaces/tests/sample_interfaces.fidl.h"
#include "lib/fidl/compiler/interfaces/tests/sample_service.fidl.h"
#include "lib/fidl/compiler/interfaces/tests/scoping.fidl.h"

using std::placeholders::_1;

namespace fidl {
namespace test {
namespace {

typedef std::function<void(double)> CalcCallback;

class MathCalculatorImpl : public math::Calculator {
 public:
  explicit MathCalculatorImpl(InterfaceRequest<math::Calculator> request)
      : total_(0.0), binding_(this, std::move(request)) {}
  ~MathCalculatorImpl() override {}

  void CloseMessagePipe() { binding_.Close(); }

  bool WaitForIncomingMethodCall() { return binding_.WaitForIncomingMethodCall(); }

  void Clear(const CalcCallback& callback) override {
    total_ = 0.0;
    callback(total_);
  }

  void Add(double value, const CalcCallback& callback) override {
    total_ += value;
    callback(total_);
  }

  void Multiply(double value, const CalcCallback& callback) override {
    total_ *= value;
    callback(total_);
  }

 private:
  double total_;
  Binding<math::Calculator> binding_;
};

class MathCalculatorUI {
 public:
  explicit MathCalculatorUI(math::CalculatorPtr calculator)
      : calculator_(std::move(calculator)),
        output_(0.0),
        callback_(std::bind(&MathCalculatorUI::Output, this, _1)) {}

  bool WaitForIncomingResponse() {
    return calculator_.WaitForIncomingResponse();
  }

  bool WaitForIncomingResponseWithTimeout(fxl::TimeDelta timeout) {
    return calculator_.WaitForIncomingResponseWithTimeout(timeout);
  }

  bool encountered_error() const { return calculator_.encountered_error(); }

  void Add(double value) { calculator_->Add(value, callback_); }

  void Subtract(double value) { calculator_->Add(-value, callback_); }

  void Multiply(double value) { calculator_->Multiply(value, callback_); }

  void Divide(double value) { calculator_->Multiply(1.0 / value, callback_); }

  double GetOutput() const { return output_; }

 private:
  void Output(double output) { output_ = output; }

  math::CalculatorPtr calculator_;
  double output_;
  std::function<void(double)> callback_;
};

class SelfDestructingMathCalculatorUI {
 public:
  explicit SelfDestructingMathCalculatorUI(math::CalculatorPtr calculator)
      : calculator_(std::move(calculator)), nesting_level_(0) {
    ++num_instances_;
  }

  void BeginTest(bool nested) {
    nesting_level_ = nested ? 2 : 1;
    calculator_->Add(
        1.0, std::bind(&SelfDestructingMathCalculatorUI::Output, this, _1));
  }

  static int num_instances() { return num_instances_; }

  void Output(double value) {
    if (--nesting_level_ > 0) {
      // Add some more and wait for re-entrant call to Output!
      calculator_->Add(
          1.0, std::bind(&SelfDestructingMathCalculatorUI::Output, this, _1));

      WaitForAsyncWaiter();
    } else {
      delete this;
    }
  }

 private:
  ~SelfDestructingMathCalculatorUI() { --num_instances_; }

  math::CalculatorPtr calculator_;
  int nesting_level_;
  static int num_instances_;
};

// static
int SelfDestructingMathCalculatorUI::num_instances_ = 0;

class ReentrantServiceImpl : public sample::Service {
 public:
  ~ReentrantServiceImpl() override {}

  explicit ReentrantServiceImpl(InterfaceRequest<sample::Service> request)
      : call_depth_(0), max_call_depth_(0), binding_(this, std::move(request)) {}

  int max_call_depth() { return max_call_depth_; }

  void Frobinate(sample::FooPtr foo,
                 sample::Service::BazOptions baz,
                 fidl::InterfaceHandle<sample::Port> port,
                 const sample::Service::FrobinateCallback& callback) override {
    max_call_depth_ = std::max(++call_depth_, max_call_depth_);
    if (call_depth_ == 1) {
      EXPECT_TRUE(binding_.WaitForIncomingMethodCall());
    }
    call_depth_--;
    callback(5);
  }

  void GetPort(fidl::InterfaceRequest<sample::Port> port) override {}

 private:
  int call_depth_;
  int max_call_depth_;
  Binding<sample::Service> binding_;
};

class IntegerAccessorImpl : public sample::IntegerAccessor {
 public:
  IntegerAccessorImpl() : integer_(0) {}
  ~IntegerAccessorImpl() override {}

  int64_t integer() const { return integer_; }

 private:
  // sample::IntegerAccessor implementation.
  void GetInteger(const GetIntegerCallback& callback) override {
    callback(integer_, sample::Enum::VALUE);
  }
  void SetInteger(int64_t data, sample::Enum type) override { integer_ = data; }

  int64_t integer_;
};

class InterfacePtrTest : public testing::Test {
 public:
  ~InterfacePtrTest() override {}
  virtual void TearDown() override {
    ClearAsyncWaiter();
  }

  void PumpMessages() { WaitForAsyncWaiter(); }
};

TEST_F(InterfacePtrTest, IsBound) {
  math::CalculatorPtr calc;
  EXPECT_FALSE(calc.is_bound());
  EXPECT_FALSE(calc);
  MathCalculatorImpl calc_impl(calc.NewRequest());
  EXPECT_TRUE(calc.is_bound());
  EXPECT_TRUE(calc);
}

TEST_F(InterfacePtrTest, EndToEnd) {
  math::CalculatorPtr calc;
  MathCalculatorImpl calc_impl(calc.NewRequest());

  // Suppose this is instantiated in a process that has pipe1_.
  MathCalculatorUI calculator_ui(std::move(calc));

  calculator_ui.Add(2.0);
  calculator_ui.Multiply(5.0);

  PumpMessages();

  EXPECT_EQ(10.0, calculator_ui.GetOutput());
}

TEST_F(InterfacePtrTest, EndToEnd_Synchronous) {
  math::CalculatorPtr calc;
  MathCalculatorImpl calc_impl(calc.NewRequest());

  // Suppose this is instantiated in a process that has pipe1_.
  MathCalculatorUI calculator_ui(std::move(calc));

  EXPECT_EQ(0.0, calculator_ui.GetOutput());

  calculator_ui.Add(2.0);
  EXPECT_EQ(0.0, calculator_ui.GetOutput());
  calc_impl.WaitForIncomingMethodCall();
  calculator_ui.WaitForIncomingResponse();
  EXPECT_EQ(2.0, calculator_ui.GetOutput());

  calculator_ui.Multiply(5.0);
  EXPECT_EQ(2.0, calculator_ui.GetOutput());
  calc_impl.WaitForIncomingMethodCall();
  calculator_ui.WaitForIncomingResponse();
  EXPECT_EQ(10.0, calculator_ui.GetOutput());

  EXPECT_FALSE(calculator_ui.WaitForIncomingResponseWithTimeout(fxl::TimeDelta::Zero()));
  EXPECT_FALSE(calculator_ui.encountered_error());
  calculator_ui.Multiply(3.0);
  EXPECT_TRUE(calc_impl.WaitForIncomingMethodCall());
  EXPECT_TRUE(calculator_ui.WaitForIncomingResponseWithTimeout(fxl::TimeDelta::Max()));
  EXPECT_EQ(30.0, calculator_ui.GetOutput());
}

TEST_F(InterfacePtrTest, Movable) {
  math::CalculatorPtr a;
  math::CalculatorPtr b;
  MathCalculatorImpl calc_impl(b.NewRequest());

  EXPECT_TRUE(!a);
  EXPECT_FALSE(!b);

  a = std::move(b);

  EXPECT_FALSE(!a);
  EXPECT_TRUE(!b);
}

TEST_F(InterfacePtrTest, Resettable) {
  math::CalculatorPtr a;

  EXPECT_TRUE(!a);

  zx::channel handle0, handle1;
  zx::channel::create(0, &handle0, &handle1);

  // Save this so we can test it later.
  zx_handle_t handle = handle0.get();

  a = math::CalculatorPtr::Create(
      InterfaceHandle<math::Calculator>(std::move(handle0), 0u));

  EXPECT_FALSE(!a);

  a.reset();

  EXPECT_TRUE(!a);
  EXPECT_FALSE(a.internal_state()->router_for_testing());

  // Test that handle was closed.
  EXPECT_EQ(ZX_ERR_BAD_HANDLE, zx_handle_close(handle));
}

TEST_F(InterfacePtrTest, BindInvalidHandle) {
  math::CalculatorPtr ptr;
  EXPECT_FALSE(ptr.get());
  EXPECT_FALSE(ptr);

  ptr.Bind(InterfaceHandle<math::Calculator>());
  EXPECT_FALSE(ptr.get());
  EXPECT_FALSE(ptr);
}

TEST_F(InterfacePtrTest, EncounteredError) {
  math::CalculatorPtr proxy;
  MathCalculatorImpl calc_impl(proxy.NewRequest());

  MathCalculatorUI calculator_ui(std::move(proxy));

  calculator_ui.Add(2.0);
  PumpMessages();
  EXPECT_EQ(2.0, calculator_ui.GetOutput());
  EXPECT_FALSE(calculator_ui.encountered_error());

  calculator_ui.Multiply(5.0);
  EXPECT_FALSE(calculator_ui.encountered_error());

  // Close the server.
  calc_impl.CloseMessagePipe();

  // The state change isn't picked up locally yet.
  EXPECT_FALSE(calculator_ui.encountered_error());

  PumpMessages();

  // OK, now we see the error.
  EXPECT_TRUE(calculator_ui.encountered_error());
}

TEST_F(InterfacePtrTest, EncounteredErrorCallback) {
  math::CalculatorPtr proxy;
  MathCalculatorImpl calc_impl(proxy.NewRequest());

  bool encountered_error = false;
  proxy.set_connection_error_handler(
      [&encountered_error]() { encountered_error = true; });

  MathCalculatorUI calculator_ui(std::move(proxy));

  calculator_ui.Add(2.0);
  PumpMessages();
  EXPECT_EQ(2.0, calculator_ui.GetOutput());
  EXPECT_FALSE(calculator_ui.encountered_error());

  calculator_ui.Multiply(5.0);
  EXPECT_FALSE(calculator_ui.encountered_error());

  // Close the server.
  calc_impl.CloseMessagePipe();

  // The state change isn't picked up locally yet.
  EXPECT_FALSE(calculator_ui.encountered_error());

  PumpMessages();

  // OK, now we see the error.
  EXPECT_TRUE(calculator_ui.encountered_error());

  // We should have also been able to observe the error through the error
  // handler.
  EXPECT_TRUE(encountered_error);
}

TEST_F(InterfacePtrTest, DestroyInterfacePtrOnMethodResponse) {
  math::CalculatorPtr proxy;
  MathCalculatorImpl calc_impl(proxy.NewRequest());

  EXPECT_EQ(0, SelfDestructingMathCalculatorUI::num_instances());

  SelfDestructingMathCalculatorUI* impl =
      new SelfDestructingMathCalculatorUI(std::move(proxy));
  impl->BeginTest(false);

  PumpMessages();

  EXPECT_EQ(0, SelfDestructingMathCalculatorUI::num_instances());
}

TEST_F(InterfacePtrTest, NestedDestroyInterfacePtrOnMethodResponse) {
  math::CalculatorPtr proxy;
  MathCalculatorImpl calc_impl(proxy.NewRequest());

  EXPECT_EQ(0, SelfDestructingMathCalculatorUI::num_instances());

  SelfDestructingMathCalculatorUI* impl =
      new SelfDestructingMathCalculatorUI(std::move(proxy));
  impl->BeginTest(true);

  PumpMessages();

  EXPECT_EQ(0, SelfDestructingMathCalculatorUI::num_instances());
}

TEST_F(InterfacePtrTest, ReentrantWaitForIncomingMethodCall) {
  sample::ServicePtr proxy;
  ReentrantServiceImpl impl(proxy.NewRequest());

  proxy->Frobinate(nullptr, sample::Service::BazOptions::REGULAR, nullptr,
                   [](uint32_t){});
  proxy->Frobinate(nullptr, sample::Service::BazOptions::REGULAR, nullptr,
                   [](uint32_t){});

  PumpMessages();

  EXPECT_EQ(2, impl.max_call_depth());
}

class StrongMathCalculatorImpl : public math::Calculator {
 public:
  StrongMathCalculatorImpl(zx::channel handle,
                           bool* error_received,
                           bool* destroyed)
      : error_received_(error_received),
        destroyed_(destroyed),
        binding_(this, std::move(handle)) {
    binding_.set_connection_error_handler(
        [this]() { *error_received_ = true; delete this; });
  }
  ~StrongMathCalculatorImpl() override { *destroyed_ = true; }

  // math::Calculator implementation.
  void Clear(const CalcCallback& callback) override { callback(total_); }

  void Add(double value, const CalcCallback& callback) override {
    total_ += value;
    callback(total_);
  }

  void Multiply(double value, const CalcCallback& callback) override {
    total_ *= value;
    callback(total_);
  }

 private:
  double total_ = 0.0;
  bool* error_received_;
  bool* destroyed_;

  Binding<math::Calculator> binding_;
};

TEST(StrongConnectorTest, Math) {
  bool error_received = false;
  bool destroyed = false;

  zx::channel handle0, handle1;
  zx::channel::create(0, &handle0, &handle1);
  new StrongMathCalculatorImpl(std::move(handle0), &error_received,
                               &destroyed);

  math::CalculatorPtr calc;
  calc.Bind(InterfaceHandle<math::Calculator>(std::move(handle1), 0u));

  {
    // Suppose this is instantiated in a process that has the other end of the
    // channel.
    MathCalculatorUI calculator_ui(std::move(calc));

    calculator_ui.Add(2.0);
    calculator_ui.Multiply(5.0);

    WaitForAsyncWaiter();

    EXPECT_EQ(10.0, calculator_ui.GetOutput());
    EXPECT_FALSE(error_received);
    EXPECT_FALSE(destroyed);
  }
  // Destroying calculator_ui should close the pipe and generate an error on the
  // other
  // end which will destroy the instance since it is strongly bound.

  WaitForAsyncWaiter();
  EXPECT_TRUE(error_received);
  EXPECT_TRUE(destroyed);
}

class WeakMathCalculatorImpl : public math::Calculator {
 public:
  WeakMathCalculatorImpl(zx::channel handle,
                         bool* error_received,
                         bool* destroyed)
      : error_received_(error_received),
        destroyed_(destroyed),
        binding_(this, std::move(handle)) {
    binding_.set_connection_error_handler(
        [this]() { *error_received_ = true; });
  }
  ~WeakMathCalculatorImpl() override { *destroyed_ = true; }

  void Clear(const CalcCallback& callback) override { callback(total_); }

  void Add(double value, const CalcCallback& callback) override {
    total_ += value;
    callback(total_);
  }

  void Multiply(double value, const CalcCallback& callback) override {
    total_ *= value;
    callback(total_);
  }

 private:
  double total_ = 0.0;
  bool* error_received_;
  bool* destroyed_;

  Binding<math::Calculator> binding_;
};

TEST(WeakConnectorTest, Math) {
  bool error_received = false;
  bool destroyed = false;

  zx::channel handle0, handle1;
  zx::channel::create(0, &handle0, &handle1);
  WeakMathCalculatorImpl impl(std::move(handle0), &error_received, &destroyed);

  math::CalculatorPtr calc;
  calc.Bind(InterfaceHandle<math::Calculator>(std::move(handle1), 0u));

  {
    // Suppose this is instantiated in a process that has the other end of the
    // channel.
    MathCalculatorUI calculator_ui(std::move(calc));

    calculator_ui.Add(2.0);
    calculator_ui.Multiply(5.0);

    WaitForAsyncWaiter();

    EXPECT_EQ(10.0, calculator_ui.GetOutput());
    EXPECT_FALSE(error_received);
    EXPECT_FALSE(destroyed);
    // Destroying calculator_ui should close the pipe and generate an error on
    // the other
    // end which will destroy the instance since it is strongly bound.
  }

  WaitForAsyncWaiter();
  EXPECT_TRUE(error_received);
  EXPECT_FALSE(destroyed);
}

class CImpl : public C {
 public:
  CImpl(bool* d_called, InterfaceRequest<C> request)
      : d_called_(d_called), binding_(this, std::move(request)) {
     binding_.set_connection_error_handler([this] { delete this; });
  }
  ~CImpl() override {}

 private:
  void D() override { *d_called_ = true; }

  bool* d_called_;
  Binding<C> binding_;
};

class BImpl : public B {
 public:
  BImpl(bool* d_called, InterfaceRequest<B> request)
      : d_called_(d_called), binding_(this, std::move(request)) {
    binding_.set_connection_error_handler([this] { delete this; });
  }
  ~BImpl() override {}

 private:
  void GetC(InterfaceRequest<C> c) override { new CImpl(d_called_, std::move(c)); }

  bool* d_called_;
  Binding<B> binding_;
};

class AImpl : public A {
 public:
  explicit AImpl(InterfaceRequest<A> request)
      : d_called_(false), binding_(this, std::move(request)) {}
  ~AImpl() override {}

  bool d_called() const { return d_called_; }

 private:
  void GetB(InterfaceRequest<B> b) override { new BImpl(&d_called_, std::move(b)); }

  bool d_called_;
  Binding<A> binding_;
};

TEST_F(InterfacePtrTest, Scoping) {
  APtr a;
  AImpl a_impl(a.NewRequest());

  EXPECT_FALSE(a_impl.d_called());

  {
    BPtr b;
    a->GetB(b.NewRequest());
    CPtr c;
    b->GetC(c.NewRequest());
    c->D();
  }

  // While B & C have fallen out of scope, the pipes will remain until they are
  // flushed.
  EXPECT_FALSE(a_impl.d_called());
  PumpMessages();
  EXPECT_TRUE(a_impl.d_called());
}

}  // namespace
}  // namespace test
}  // namespace fidl
