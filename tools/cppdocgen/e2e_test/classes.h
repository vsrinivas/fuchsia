// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_CPPDOCGEN_E2E_TEST_CLASSES_H_
#define TOOLS_CPPDOCGEN_E2E_TEST_CLASSES_H_

class SimpleTestClass {
 public:
  SimpleTestClass();
  explicit SimpleTestClass(int a);
  SimpleTestClass(int a = 1, int b = 2);
  ~SimpleTestClass();

  int value() const { return value_; }

  enum EnumInsideClass { kValue1, kValue2 };

  struct StructInsideClass {
    int a;
  };

  typedef StructInsideClass StructInsideClassTypedef;
  using StructInsideClassUsing = StructInsideClass;

  // Some documentation for the public value.
  //
  // This violates the style guide but should still work.
  int public_value = 19;

  int public_value2 = 20;  // End-of-line comment. Scary!

  // Undocumented public data member $nodoc
  int secret_public_value = 42;

  // This is a documented pure virtual function.
  virtual int TheFunction() = 0;

  // This member function shouldn't be documented because of the $nodoc annotation.
  void UndocumentedFunction();

  // This member shouldn't have a declaration because of the $nodecl annotation.
  void FunctionWithNoGeneratedDeclaration();

 private:
  /// This is a well-documented private member. It should not be emitted in the markdown.
  void PrivateFn();

  int value_ = 0;
};

class BaseClass1 {
 public:
  // Complicated documentation for BaseClass1Function.
  virtual int BaseClass1Function();
};

class BaseClass2 {
 public:
  // Insightful documentation for BaseClass2Function.
  virtual void BaseClass2Function() = 0;
};

class DerivedClass : public BaseClass1, private BaseClass2 {
 public:
  // An override with documentation. Note that the BaseClass1Function() is not overridden.
  void BaseClass2Function() override;
};

// This class should be omitted because of the $nodoc annotation.
class UndocumentedClass {
 public:
  int SomeFunction();
};

// This class should not have a generated declaration becaose of the $nodecl annotation.
class NoDeclarationClass {
 public:
  int SomeFunction();
};

#endif  // TOOLS_CPPDOCGEN_E2E_TEST_CLASSES_H_
