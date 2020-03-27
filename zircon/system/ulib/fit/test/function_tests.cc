// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/function.h>
#include <unittest/unittest.h>

namespace {

using Closure = void();
using ClosureWrongReturnType = int();
using BinaryOp = int(int a, int b);
using BinaryOpWrongReturnType = void(int a, int b);
using MoveOp = std::unique_ptr<int>(std::unique_ptr<int> value);
using BooleanGenerator = bool();
using IntGenerator = int();

class BuildableFromInt {
 public:
  BuildableFromInt(int);
  BuildableFromInt& operator=(int);
};

using BuildableFromIntGenerator = BuildableFromInt();

// A big object which causes a function target to be heap allocated.
struct Big {
  int data[64]{};
};
// An object with a very large alignment requirement that cannot be placed in a
// fit::inline_function.
struct alignas(64) BigAlignment {
  int data[64]{};
};
constexpr size_t HugeCallableSize = sizeof(Big) + sizeof(void*) * 4;

// An object that looks like an "empty" std::function.
template <typename>
struct EmptyFunction;
template <typename R, typename... Args>
struct EmptyFunction<R(Args...)> {
  R operator()(Args... args) const { return fptr(args...); }
  bool operator==(decltype(nullptr)) const { return true; }

  R(*fptr)
  (Args...) = nullptr;
};

// An object whose state we can examine from the outside.
struct SlotMachine {
  void operator()() { value++; }
  int operator()(int a, int b) {
    value += a * b;
    return value;
  }

  int value = 0;
};

// A move-only object which increments a counter when uniquely destroyed.
class DestructionObserver {
 public:
  DestructionObserver(int* counter) : counter_(counter) {}
  DestructionObserver(DestructionObserver&& other) : counter_(other.counter_) {
    other.counter_ = nullptr;
  }
  DestructionObserver(const DestructionObserver& other) = delete;

  ~DestructionObserver() {
    if (counter_)
      *counter_ += 1;
  }

  DestructionObserver& operator=(const DestructionObserver& other) = delete;
  DestructionObserver& operator=(DestructionObserver&& other) {
    if (counter_)
      *counter_ += 1;
    counter_ = other.counter_;
    other.counter_ = nullptr;
    return *this;
  }

 private:
  int* counter_;
};

template <typename ClosureFunction>
bool closure() {
  static_assert(fit::is_nullable<ClosureFunction>::value, "");

  BEGIN_TEST;

  // default initialization
  ClosureFunction fdefault;
  EXPECT_FALSE(!!fdefault);

  // nullptr initialization
  ClosureFunction fnull(nullptr);
  EXPECT_FALSE(!!fnull);

  // null function pointer initialization
  Closure* fptr = nullptr;
  ClosureFunction ffunc(fptr);
  EXPECT_FALSE(!!ffunc);

  // "empty std::function" initialization
  EmptyFunction<Closure> empty;
  ClosureFunction fwrapper(empty);
  EXPECT_FALSE(!!fwrapper);

  // inline callable initialization
  int finline_value = 0;
  ClosureFunction finline([&finline_value] { finline_value++; });
  EXPECT_TRUE(!!finline);
  finline();
  EXPECT_EQ(1, finline_value);
  finline();
  EXPECT_EQ(2, finline_value);

  // heap callable initialization
  int fheap_value = 0;
  ClosureFunction fheap([&fheap_value, big = Big()] { fheap_value++; });
  EXPECT_TRUE(!!fheap);
  fheap();
  EXPECT_EQ(1, fheap_value);
  fheap();
  EXPECT_EQ(2, fheap_value);

  // move initialization of a nullptr
  ClosureFunction fnull2(std::move(fnull));
  EXPECT_FALSE(!!fnull2);

  // move initialization of an inline callable
  ClosureFunction finline2(std::move(finline));
  EXPECT_TRUE(!!finline2);
  EXPECT_FALSE(!!finline);
  finline2();
  EXPECT_EQ(3, finline_value);
  finline2();
  EXPECT_EQ(4, finline_value);

  // move initialization of a heap callable
  ClosureFunction fheap2(std::move(fheap));
  EXPECT_TRUE(!!fheap2);
  EXPECT_FALSE(!!fheap);
  fheap2();
  EXPECT_EQ(3, fheap_value);
  fheap2();
  EXPECT_EQ(4, fheap_value);

  // inline mutable lambda
  int fmutinline_value = 0;
  ClosureFunction fmutinline([&fmutinline_value, x = 1]() mutable {
    x *= 2;
    fmutinline_value = x;
  });
  EXPECT_TRUE(!!fmutinline);
  fmutinline();
  EXPECT_EQ(2, fmutinline_value);
  fmutinline();
  EXPECT_EQ(4, fmutinline_value);

  // heap-allocated mutable lambda
  int fmutheap_value = 0;
  ClosureFunction fmutheap([&fmutheap_value, big = Big(), x = 1]() mutable {
    x *= 2;
    fmutheap_value = x;
  });
  EXPECT_TRUE(!!fmutheap);
  fmutheap();
  EXPECT_EQ(2, fmutheap_value);
  fmutheap();
  EXPECT_EQ(4, fmutheap_value);

  // move assignment of non-null
  ClosureFunction fnew([] {});
  fnew = std::move(finline2);
  EXPECT_TRUE(!!fnew);
  fnew();
  EXPECT_EQ(5, finline_value);
  fnew();
  EXPECT_EQ(6, finline_value);

  // move assignment of self
  fnew = std::move(fnew);
  EXPECT_TRUE(!!fnew);
  fnew();
  EXPECT_EQ(7, finline_value);

  // move assignment of null
  fnew = std::move(fnull);
  EXPECT_FALSE(!!fnew);

  // callable assignment with operator=
  int fnew_value = 0;
  fnew = [&fnew_value] { fnew_value++; };
  EXPECT_TRUE(!!fnew);
  fnew();
  EXPECT_EQ(1, fnew_value);
  fnew();
  EXPECT_EQ(2, fnew_value);

  // nullptr assignment
  fnew = nullptr;
  EXPECT_FALSE(!!fnew);

  // swap (currently null)
  swap(fnew, fheap2);
  EXPECT_TRUE(!!fnew);
  EXPECT_FALSE(!!fheap);
  fnew();
  EXPECT_EQ(5, fheap_value);
  fnew();
  EXPECT_EQ(6, fheap_value);

  // swap with self
  swap(fnew, fnew);
  EXPECT_TRUE(!!fnew);
  fnew();
  EXPECT_EQ(7, fheap_value);
  fnew();
  EXPECT_EQ(8, fheap_value);

  // swap with non-null
  swap(fnew, fmutinline);
  EXPECT_TRUE(!!fmutinline);
  EXPECT_TRUE(!!fnew);
  fmutinline();
  EXPECT_EQ(9, fheap_value);
  fmutinline();
  EXPECT_EQ(10, fheap_value);
  fnew();
  EXPECT_EQ(8, fmutinline_value);
  fnew();
  EXPECT_EQ(16, fmutinline_value);

  // nullptr comparison operators
  EXPECT_TRUE(fnull == nullptr);
  EXPECT_FALSE(fnull != nullptr);
  EXPECT_TRUE(nullptr == fnull);
  EXPECT_FALSE(nullptr != fnull);
  EXPECT_FALSE(fnew == nullptr);
  EXPECT_TRUE(fnew != nullptr);
  EXPECT_FALSE(nullptr == fnew);
  EXPECT_TRUE(nullptr != fnew);

  // null function pointer assignment
  fnew = fptr;
  EXPECT_FALSE(!!fnew);

  // "empty std::function" assignment
  fmutinline = empty;
  EXPECT_FALSE(!!fmutinline);

  // target access
  ClosureFunction fslot;
  EXPECT_NULL(fslot.template target<decltype(nullptr)>());
  fslot = SlotMachine{42};
  fslot();
  SlotMachine* fslottarget = fslot.template target<SlotMachine>();
  EXPECT_EQ(43, fslottarget->value);
  const SlotMachine* fslottargetconst =
      const_cast<const ClosureFunction&>(fslot).template target<SlotMachine>();
  EXPECT_EQ(fslottarget, fslottargetconst);
  fslot = nullptr;
  EXPECT_NULL(fslot.template target<decltype(nullptr)>());

  END_TEST;
}

template <typename BinaryOpFunction>
bool binary_op() {
  static_assert(fit::is_nullable<BinaryOpFunction>::value, "");

  BEGIN_TEST;

  // default initialization
  BinaryOpFunction fdefault;
  EXPECT_FALSE(!!fdefault);

  // nullptr initialization
  BinaryOpFunction fnull(nullptr);
  EXPECT_FALSE(!!fnull);

  // null function pointer initialization
  BinaryOp* fptr = nullptr;
  BinaryOpFunction ffunc(fptr);
  EXPECT_FALSE(!!ffunc);

  // "empty std::function" initialization
  EmptyFunction<BinaryOp> empty;
  BinaryOpFunction fwrapper(empty);
  EXPECT_FALSE(!!fwrapper);

  // inline callable initialization
  int finline_value = 0;
  BinaryOpFunction finline([&finline_value](int a, int b) {
    finline_value++;
    return a + b;
  });
  EXPECT_TRUE(!!finline);
  EXPECT_EQ(10, finline(3, 7));
  EXPECT_EQ(1, finline_value);
  EXPECT_EQ(10, finline(3, 7));
  EXPECT_EQ(2, finline_value);

  // heap callable initialization
  int fheap_value = 0;
  BinaryOpFunction fheap([&fheap_value, big = Big()](int a, int b) {
    fheap_value++;
    return a + b;
  });
  EXPECT_TRUE(!!fheap);
  EXPECT_EQ(10, fheap(3, 7));
  EXPECT_EQ(1, fheap_value);
  EXPECT_EQ(10, fheap(3, 7));
  EXPECT_EQ(2, fheap_value);

  // move initialization of a nullptr
  BinaryOpFunction fnull2(std::move(fnull));
  EXPECT_FALSE(!!fnull2);

  // move initialization of an inline callable
  BinaryOpFunction finline2(std::move(finline));
  EXPECT_TRUE(!!finline2);
  EXPECT_FALSE(!!finline);
  EXPECT_EQ(10, finline2(3, 7));
  EXPECT_EQ(3, finline_value);
  EXPECT_EQ(10, finline2(3, 7));
  EXPECT_EQ(4, finline_value);

  // move initialization of a heap callable
  BinaryOpFunction fheap2(std::move(fheap));
  EXPECT_TRUE(!!fheap2);
  EXPECT_FALSE(!!fheap);
  EXPECT_EQ(10, fheap2(3, 7));
  EXPECT_EQ(3, fheap_value);
  EXPECT_EQ(10, fheap2(3, 7));
  EXPECT_EQ(4, fheap_value);

  // inline mutable lambda
  int fmutinline_value = 0;
  BinaryOpFunction fmutinline([&fmutinline_value, x = 1](int a, int b) mutable {
    x *= 2;
    fmutinline_value = x;
    return a + b;
  });
  EXPECT_TRUE(!!fmutinline);
  EXPECT_EQ(10, fmutinline(3, 7));
  EXPECT_EQ(2, fmutinline_value);
  EXPECT_EQ(10, fmutinline(3, 7));
  EXPECT_EQ(4, fmutinline_value);

  // heap-allocated mutable lambda
  int fmutheap_value = 0;
  BinaryOpFunction fmutheap([&fmutheap_value, big = Big(), x = 1](int a, int b) mutable {
    x *= 2;
    fmutheap_value = x;
    return a + b;
  });
  EXPECT_TRUE(!!fmutheap);
  EXPECT_EQ(10, fmutheap(3, 7));
  EXPECT_EQ(2, fmutheap_value);
  EXPECT_EQ(10, fmutheap(3, 7));
  EXPECT_EQ(4, fmutheap_value);

  // move assignment of non-null
  BinaryOpFunction fnew([](int a, int b) { return 0; });
  fnew = std::move(finline2);
  EXPECT_TRUE(!!fnew);
  EXPECT_EQ(10, fnew(3, 7));
  EXPECT_EQ(5, finline_value);
  EXPECT_EQ(10, fnew(3, 7));
  EXPECT_EQ(6, finline_value);

  // self-assignment of non-null
  fnew = std::move(fnew);
  EXPECT_TRUE(!!fnew);
  EXPECT_EQ(10, fnew(3, 7));
  EXPECT_EQ(7, finline_value);

  // move assignment of null
  fnew = std::move(fnull);
  EXPECT_FALSE(!!fnew);

  // self-assignment of non-null
  fnew = std::move(fnew);
  EXPECT_FALSE(!!fnew);

  // callable assignment with operator=
  int fnew_value = 0;
  fnew = [&fnew_value](int a, int b) {
    fnew_value++;
    return a + b;
  };
  EXPECT_TRUE(!!fnew);
  EXPECT_EQ(10, fnew(3, 7));
  EXPECT_EQ(1, fnew_value);
  EXPECT_EQ(10, fnew(3, 7));
  EXPECT_EQ(2, fnew_value);

  // nullptr assignment
  fnew = nullptr;
  EXPECT_FALSE(!!fnew);

  // swap (currently null)
  swap(fnew, fheap2);
  EXPECT_TRUE(!!fnew);
  EXPECT_FALSE(!!fheap);
  EXPECT_EQ(10, fnew(3, 7));
  EXPECT_EQ(5, fheap_value);
  EXPECT_EQ(10, fnew(3, 7));
  EXPECT_EQ(6, fheap_value);

  // swap with self
  swap(fnew, fnew);
  EXPECT_TRUE(!!fnew);
  EXPECT_EQ(10, fnew(3, 7));
  EXPECT_EQ(7, fheap_value);
  EXPECT_EQ(10, fnew(3, 7));
  EXPECT_EQ(8, fheap_value);

  // swap with non-null
  swap(fnew, fmutinline);
  EXPECT_TRUE(!!fmutinline);
  EXPECT_TRUE(!!fnew);
  EXPECT_EQ(10, fmutinline(3, 7));
  EXPECT_EQ(9, fheap_value);
  EXPECT_EQ(10, fmutinline(3, 7));
  EXPECT_EQ(10, fheap_value);
  EXPECT_EQ(10, fnew(3, 7));
  EXPECT_EQ(8, fmutinline_value);
  EXPECT_EQ(10, fnew(3, 7));
  EXPECT_EQ(16, fmutinline_value);

  // nullptr comparison operators
  EXPECT_TRUE(fnull == nullptr);
  EXPECT_FALSE(fnull != nullptr);
  EXPECT_TRUE(nullptr == fnull);
  EXPECT_FALSE(nullptr != fnull);
  EXPECT_FALSE(fnew == nullptr);
  EXPECT_TRUE(fnew != nullptr);
  EXPECT_FALSE(nullptr == fnew);
  EXPECT_TRUE(nullptr != fnew);

  // null function pointer assignment
  fnew = fptr;
  EXPECT_FALSE(!!fnew);

  // "empty std::function" assignment
  fmutinline = empty;
  EXPECT_FALSE(!!fmutinline);

  // target access
  BinaryOpFunction fslot;
  EXPECT_NULL(fslot.template target<decltype(nullptr)>());
  fslot = SlotMachine{42};
  EXPECT_EQ(54, fslot(3, 4));
  SlotMachine* fslottarget = fslot.template target<SlotMachine>();
  EXPECT_EQ(54, fslottarget->value);
  const SlotMachine* fslottargetconst =
      const_cast<const BinaryOpFunction&>(fslot).template target<SlotMachine>();
  EXPECT_EQ(fslottarget, fslottargetconst);
  fslot = nullptr;
  EXPECT_NULL(fslot.template target<decltype(nullptr)>());

  END_TEST;
}

bool sized_function_size_bounds() {
  BEGIN_TEST;

  auto empty = [] {};
  fit::function<Closure, sizeof(empty)> fempty(std::move(empty));
  static_assert(sizeof(fempty) >= sizeof(empty), "size bounds");

  auto small = [x = 1, y = 2] {
    (void)x;  // suppress unused lambda capture warning
    (void)y;
  };
  fit::function<Closure, sizeof(small)> fsmall(std::move(small));
  static_assert(sizeof(fsmall) >= sizeof(small), "size bounds");
  fsmall = [] {};

  auto big = [big = Big(), x = 1] { (void)x; };
  fit::function<Closure, sizeof(big)> fbig(std::move(big));
  static_assert(sizeof(fbig) >= sizeof(big), "size bounds");
  fbig = [x = 1, y = 2] {
    (void)x;
    (void)y;
  };
  fbig = [] {};

  // These statements do compile though the lambda will be copied to the heap
  // when they exceed the inline size.
  fempty = [x = 1, y = 2] {
    (void)x;
    (void)y;
  };
  fsmall = [big = Big(), x = 1] { (void)x; };
  fbig = [big = Big(), x = 1, y = 2] {
    (void)x;
    (void)y;
  };

  END_TEST;
}

bool inline_function_size_bounds() {
  BEGIN_TEST;

  auto empty = [] {};
  fit::inline_function<Closure, sizeof(empty)> fempty(std::move(empty));
  static_assert(sizeof(fempty) >= sizeof(empty), "size bounds");

  auto small = [x = 1, y = 2] {
    (void)x;  // suppress unused lambda capture warning
    (void)y;
  };
  fit::inline_function<Closure, sizeof(small)> fsmall(std::move(small));
  static_assert(sizeof(fsmall) >= sizeof(small), "size bounds");
  fsmall = [] {};

  auto big = [big = Big(), x = 1] { (void)x; };
  fit::inline_function<Closure, sizeof(big)> fbig(std::move(big));
  static_assert(sizeof(fbig) >= sizeof(big), "size bounds");
  fbig = [x = 1, y = 2] {
    (void)x;
    (void)y;
  };
  fbig = [] {};

// These statements do not compile because the lambdas are too big to fit.
#if 0
    fempty = [ x = 1, y = 2 ] {
        (void)x;
        (void)y;
    };
    fsmall = [ big = Big(), x = 1 ] { (void)x; };
    fbig = [ big = Big(), x = 1, y = 2 ] {
        (void)x;
        (void)y;
    };
#endif

  END_TEST;
}

bool inline_function_alignment_check() {
  BEGIN_TEST;
// These statements do not compile because the alignment is too large.
#if 0
    auto big = [big = BigAlignment()] { };
    fit::inline_function<Closure, sizeof(big)> fbig(std::move(big));
#endif
  END_TEST;
}

bool move_only_argument_and_result() {
  BEGIN_TEST;

  std::unique_ptr<int> arg(new int());
  fit::function<MoveOp> f([](std::unique_ptr<int> value) {
    *value += 1;
    return value;
  });
  arg = f(std::move(arg));
  EXPECT_EQ(1, *arg);
  arg = f(std::move(arg));
  EXPECT_EQ(2, *arg);

  END_TEST;
}

void implicit_construction_helper(fit::closure closure) {}

bool implicit_construction() {
  BEGIN_TEST;

  // ensure we can implicitly construct from nullptr
  implicit_construction_helper(nullptr);

  // ensure we can implicitly construct from a lambda
  implicit_construction_helper([] {});

  END_TEST;
}

int arg_count(fit::closure) { return 0; }
int arg_count(fit::function<void(int)>) { return 1; }

bool overload_resolution() {
  BEGIN_TEST;
  EXPECT_EQ(0, arg_count([] {}));
  EXPECT_EQ(1, arg_count([](int) {}));
  END_TEST;
}

bool sharing() {
  BEGIN_TEST;

  fit::function<Closure> fnull;
  fit::function<Closure> fnullshare1 = fnull.share();
  fit::function<Closure> fnullshare2 = fnull.share();
  fit::function<Closure> fnullshare3 = fnullshare1.share();
  EXPECT_FALSE(!!fnull);
  EXPECT_FALSE(!!fnullshare1);
  EXPECT_FALSE(!!fnullshare2);
  EXPECT_FALSE(!!fnullshare3);

  int finlinevalue = 1;
  int finlinedestroy = 0;
  fit::function<Closure> finline = [&finlinevalue, d = DestructionObserver(&finlinedestroy)] {
    finlinevalue++;
  };
  fit::function<Closure> finlineshare1 = finline.share();
  fit::function<Closure> finlineshare2 = finline.share();
  fit::function<Closure> finlineshare3 = finlineshare1.share();
  EXPECT_TRUE(!!finline);
  EXPECT_TRUE(!!finlineshare1);
  EXPECT_TRUE(!!finlineshare2);
  EXPECT_TRUE(!!finlineshare3);
  finline();
  EXPECT_EQ(2, finlinevalue);
  finlineshare1();
  EXPECT_EQ(3, finlinevalue);
  finlineshare2();
  EXPECT_EQ(4, finlinevalue);
  finlineshare3();
  EXPECT_EQ(5, finlinevalue);
  finlineshare2();
  EXPECT_EQ(6, finlinevalue);
  finline();
  EXPECT_EQ(7, finlinevalue);
  EXPECT_EQ(0, finlinedestroy);
  finline = nullptr;
  EXPECT_EQ(0, finlinedestroy);
  finlineshare3 = nullptr;
  EXPECT_EQ(0, finlinedestroy);
  finlineshare2 = nullptr;
  EXPECT_EQ(0, finlinedestroy);
  finlineshare1 = nullptr;
  EXPECT_EQ(1, finlinedestroy);

  int fheapvalue = 1;
  int fheapdestroy = 0;
  fit::function<Closure> fheap = [&fheapvalue, big = Big(),
                                  d = DestructionObserver(&fheapdestroy)] { fheapvalue++; };
  fit::function<Closure> fheapshare1 = fheap.share();
  fit::function<Closure> fheapshare2 = fheap.share();
  fit::function<Closure> fheapshare3 = fheapshare1.share();
  EXPECT_TRUE(!!fheap);
  EXPECT_TRUE(!!fheapshare1);
  EXPECT_TRUE(!!fheapshare2);
  EXPECT_TRUE(!!fheapshare3);
  fheap();
  EXPECT_EQ(2, fheapvalue);
  fheapshare1();
  EXPECT_EQ(3, fheapvalue);
  fheapshare2();
  EXPECT_EQ(4, fheapvalue);
  fheapshare3();
  EXPECT_EQ(5, fheapvalue);
  fheapshare2();
  EXPECT_EQ(6, fheapvalue);
  fheap();
  EXPECT_EQ(7, fheapvalue);
  EXPECT_EQ(0, fheapdestroy);
  fheap = nullptr;
  EXPECT_EQ(0, fheapdestroy);
  fheapshare3 = nullptr;
  EXPECT_EQ(0, fheapdestroy);
  fheapshare2 = nullptr;
  EXPECT_EQ(0, fheapdestroy);
  fheapshare1 = nullptr;
  EXPECT_EQ(1, fheapdestroy);

  // target access now available after share()
  using ClosureFunction = fit::function<Closure, HugeCallableSize>;
  ClosureFunction fslot = SlotMachine{42};
  fslot();
  SlotMachine* fslottarget = fslot.template target<SlotMachine>();
  EXPECT_EQ(43, fslottarget->value);

  auto shared_fslot = fslot.share();
  shared_fslot();
  fslottarget = shared_fslot.template target<SlotMachine>();
  EXPECT_EQ(44, fslottarget->value);
  fslot();
  EXPECT_EQ(45, fslottarget->value);
  fslot = nullptr;
  EXPECT_NULL(fslot.template target<decltype(nullptr)>());
  shared_fslot();
  EXPECT_EQ(46, fslottarget->value);
  shared_fslot = nullptr;
  EXPECT_NULL(shared_fslot.template target<decltype(nullptr)>());

// These statements do not compile because inline functions cannot be shared
#if 0
    fit::inline_function<Closure> fbad;
    fbad.share();
#endif

  END_TEST;
}

struct Obj {
  void Call() { calls++; }

  int AddOne(int x) {
    calls++;
    return x + 1;
  }

  int Sum(int a, int b, int c) {
    calls++;
    return a + b + c;
  }

  std::unique_ptr<int> AddAndReturn(std::unique_ptr<int> value) {
    (*value)++;
    return value;
  }

  uint32_t calls = 0;
};

bool bind_member() {
  BEGIN_TEST;

  Obj obj;
  auto move_only_value = std::make_unique<int>(4);

  fit::bind_member(&obj, &Obj::Call)();
  EXPECT_EQ(23, fit::bind_member(&obj, &Obj::AddOne)(22));
  EXPECT_EQ(6, fit::bind_member(&obj, &Obj::Sum)(1, 2, 3));
  move_only_value = fit::bind_member(&obj, &Obj::AddAndReturn)(std::move(move_only_value));
  EXPECT_EQ(5, *move_only_value);
  EXPECT_EQ(3, obj.calls);

  END_TEST;
}

bool callback_once() {
  BEGIN_TEST;

  fit::callback<Closure> cbnull;
  fit::callback<Closure> cbnullshare1 = cbnull.share();
  fit::callback<Closure> cbnullshare2 = cbnull.share();
  fit::callback<Closure> cbnullshare3 = cbnullshare1.share();
  EXPECT_FALSE(!!cbnull);
  EXPECT_FALSE(!!cbnullshare1);
  EXPECT_FALSE(!!cbnullshare2);
  EXPECT_FALSE(!!cbnullshare3);

  int cbinlinevalue = 1;
  int cbinlinedestroy = 0;
  fit::callback<Closure> cbinline = [&cbinlinevalue, d = DestructionObserver(&cbinlinedestroy)] {
    cbinlinevalue++;
  };
  EXPECT_TRUE(!!cbinline);
  EXPECT_FALSE(cbinline == nullptr);
  EXPECT_EQ(1, cbinlinevalue);
  EXPECT_EQ(0, cbinlinedestroy);
  cbinline();  // releases resources even if never shared
  EXPECT_FALSE(!!cbinline);
  EXPECT_TRUE(cbinline == nullptr);
  EXPECT_EQ(2, cbinlinevalue);
  EXPECT_EQ(1, cbinlinedestroy);

  cbinlinevalue = 1;
  cbinlinedestroy = 0;
  cbinline = [&cbinlinevalue, d = DestructionObserver(&cbinlinedestroy)] { cbinlinevalue++; };
  fit::callback<Closure> cbinlineshare1 = cbinline.share();
  fit::callback<Closure> cbinlineshare2 = cbinline.share();
  fit::callback<Closure> cbinlineshare3 = cbinlineshare1.share();
  EXPECT_TRUE(!!cbinline);
  EXPECT_TRUE(!!cbinlineshare1);
  EXPECT_TRUE(!!cbinlineshare2);
  EXPECT_TRUE(!!cbinlineshare3);
  EXPECT_EQ(1, cbinlinevalue);
  EXPECT_EQ(0, cbinlinedestroy);
  cbinline();
  EXPECT_EQ(2, cbinlinevalue);
  EXPECT_EQ(1, cbinlinedestroy);
  EXPECT_FALSE(!!cbinline);
  EXPECT_TRUE(cbinline == nullptr);
  // cbinline(); // should abort
  EXPECT_FALSE(!!cbinlineshare1);
  EXPECT_TRUE(cbinlineshare1 == nullptr);
  // cbinlineshare1(); // should abort
  EXPECT_FALSE(!!cbinlineshare2);
  // cbinlineshare2(); // should abort
  EXPECT_FALSE(!!cbinlineshare3);
  // cbinlineshare3(); // should abort
  EXPECT_EQ(1, cbinlinedestroy);
  cbinlineshare3 = nullptr;
  EXPECT_EQ(1, cbinlinedestroy);
  cbinline = nullptr;
  EXPECT_EQ(1, cbinlinedestroy);

  int cbheapvalue = 1;
  int cbheapdestroy = 0;
  fit::callback<Closure> cbheap = [&cbheapvalue, big = Big(),
                                   d = DestructionObserver(&cbheapdestroy)] { cbheapvalue++; };
  EXPECT_TRUE(!!cbheap);
  EXPECT_FALSE(cbheap == nullptr);
  EXPECT_EQ(1, cbheapvalue);
  EXPECT_EQ(0, cbheapdestroy);
  cbheap();  // releases resources even if never shared
  EXPECT_FALSE(!!cbheap);
  EXPECT_TRUE(cbheap == nullptr);
  EXPECT_EQ(2, cbheapvalue);
  EXPECT_EQ(1, cbheapdestroy);

  cbheapvalue = 1;
  cbheapdestroy = 0;
  cbheap = [&cbheapvalue, big = Big(), d = DestructionObserver(&cbheapdestroy)] { cbheapvalue++; };
  fit::callback<Closure> cbheapshare1 = cbheap.share();
  fit::callback<Closure> cbheapshare2 = cbheap.share();
  fit::callback<Closure> cbheapshare3 = cbheapshare1.share();
  EXPECT_TRUE(!!cbheap);
  EXPECT_TRUE(!!cbheapshare1);
  EXPECT_TRUE(!!cbheapshare2);
  EXPECT_TRUE(!!cbheapshare3);
  EXPECT_EQ(1, cbheapvalue);
  EXPECT_EQ(0, cbheapdestroy);
  cbheap();
  EXPECT_EQ(2, cbheapvalue);
  EXPECT_EQ(1, cbheapdestroy);
  EXPECT_FALSE(!!cbheap);
  EXPECT_TRUE(cbheap == nullptr);
  // cbheap(); // should abort
  EXPECT_FALSE(!!cbheapshare1);
  EXPECT_TRUE(cbheapshare1 == nullptr);
  // cbheapshare1(); // should abort
  EXPECT_FALSE(!!cbheapshare2);
  // cbheapshare2(); // should abort
  EXPECT_FALSE(!!cbheapshare3);
  // cbheapshare3(); // should abort
  EXPECT_EQ(1, cbheapdestroy);
  cbheapshare3 = nullptr;
  EXPECT_EQ(1, cbheapdestroy);
  cbheap = nullptr;
  EXPECT_EQ(1, cbheapdestroy);

  // Verify new design, splitting out fit::callback, still supports
  // assignment of move-only "Callables" (that is, lambdas made move-only
  // because they capture a move-only object, like a fit::function, for
  // example!)
  fit::function<void()> fn_to_wrap = []() {};
  fit::function<void()> fn_from_lambda;
  fn_from_lambda = [fn = fn_to_wrap.share()]() mutable { fn(); };

  // Same test for fit::callback
  fit::callback<void()> cb_to_wrap = []() {};
  fit::callback<void()> cb_from_lambda;
  cb_from_lambda = [cb = std::move(cb_to_wrap)]() mutable { cb(); };

  // |fit::function| objects can be constructed from or assigned from
  // a |fit::callback|, if the result and arguments are compatible.
  fit::function<Closure> fn = []() {};
  fit::callback<Closure> cb = []() {};
  fit::callback<Closure> cb_assign;
  cb_assign = std::move(fn);
  fit::callback<Closure> cb_construct = std::move(fn);
  fit::callback<Closure> cb_share = fn.share();

  static_assert(!std::is_convertible<fit::function<void()>*, fit::callback<void()>*>::value, "");
  static_assert(!std::is_constructible<fit::function<void()>, fit::callback<void()>>::value, "");
  static_assert(!std::is_assignable<fit::function<void()>, fit::callback<void()>>::value, "");
  static_assert(!std::is_constructible<fit::function<void()>, decltype(cb.share())>::value, "");
#if 0
    // These statements do not compile because inline callbacks cannot be shared
    fit::inline_callback<Closure> cbbad;
    cbbad.share();

    {
        // Attempts to copy, move, or share a callback into a fit::function<>
        // should not compile. This is verified by static_assert above, and
        // was verified interactively using the compiler.
        fit::callback<Closure> cb = []() {};
        fit::function<Closure> fn = []() {};
        fit::function<Closure> fn_assign;
        fn_assign = cb;                                       // BAD
        fn_assign = std::move(cb);                            // BAD
        fit::function<Closure> fn_construct = cb;             // BAD
        fit::function<Closure> fn_construct2 = std::move(cb); // BAD
        fit::function<Closure> fn_share = cb.share();         // BAD
    }

#endif

  END_TEST;
}

}  // namespace

namespace test_conversions {
static_assert(std::is_convertible<Closure, fit::function<Closure>>::value, "");
static_assert(std::is_convertible<BinaryOp, fit::function<BinaryOp>>::value, "");
static_assert(std::is_assignable<fit::function<Closure>, Closure>::value, "");
static_assert(std::is_assignable<fit::function<BinaryOp>, BinaryOp>::value, "");

static_assert(std::is_assignable<fit::function<BooleanGenerator>, IntGenerator>::value, "");
static_assert(std::is_assignable<fit::function<BuildableFromIntGenerator>, IntGenerator>::value,
              "");
static_assert(!std::is_assignable<fit::function<IntGenerator>, BuildableFromIntGenerator>::value,
              "");

static_assert(!std::is_convertible<BinaryOp, fit::function<Closure>>::value, "");
static_assert(!std::is_convertible<Closure, fit::function<BinaryOp>>::value, "");
static_assert(!std::is_assignable<fit::function<Closure>, BinaryOp>::value, "");
static_assert(!std::is_assignable<fit::function<BinaryOp>, Closure>::value, "");

static_assert(!std::is_convertible<ClosureWrongReturnType, fit::function<Closure>>::value, "");
static_assert(!std::is_convertible<BinaryOpWrongReturnType, fit::function<BinaryOp>>::value, "");
static_assert(!std::is_assignable<fit::function<Closure>, ClosureWrongReturnType>::value, "");
static_assert(!std::is_assignable<fit::function<BinaryOp>, BinaryOpWrongReturnType>::value, "");

static_assert(!std::is_convertible<void, fit::function<Closure>>::value, "");
static_assert(!std::is_convertible<void, fit::function<BinaryOp>>::value, "");
static_assert(!std::is_assignable<void, fit::function<Closure>>::value, "");
static_assert(!std::is_assignable<void, fit::function<BinaryOp>>::value, "");

static_assert(std::is_same<fit::function<BinaryOp>::result_type, int>::value, "");
static_assert(std::is_same<fit::callback<BinaryOp>::result_type, int>::value, "");
}  // namespace test_conversions

BEGIN_TEST_CASE(function_tests)
RUN_TEST((closure<fit::function<Closure>>))
RUN_TEST((binary_op<fit::function<BinaryOp>>))
RUN_TEST((closure<fit::function<Closure, 0u>>))
RUN_TEST((binary_op<fit::function<BinaryOp, 0u>>))
RUN_TEST((closure<fit::function<Closure, HugeCallableSize>>))
RUN_TEST((binary_op<fit::function<BinaryOp, HugeCallableSize>>))
RUN_TEST((closure<fit::inline_function<Closure, HugeCallableSize>>))
RUN_TEST((binary_op<fit::inline_function<BinaryOp, HugeCallableSize>>))
RUN_TEST(sized_function_size_bounds)
RUN_TEST(inline_function_size_bounds)
RUN_TEST(inline_function_alignment_check)
RUN_TEST(move_only_argument_and_result)
RUN_TEST(implicit_construction)
RUN_TEST(overload_resolution)
RUN_TEST(sharing)
RUN_TEST(callback_once)
RUN_TEST(bind_member);
END_TEST_CASE(function_tests)
