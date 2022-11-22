// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test.types/cpp/fidl.h>
#include <lib/stdcompat/type_traits.h>

#include <zxtest/zxtest.h>

namespace {

TEST(Response, DefaultConstruction) {
  fidl::Response<test_types::Baz::Foo> response;
  EXPECT_EQ(0, response.res().bar());
}

TEST(Response, FromPayload) {
  test_types::FooResponse res{{.bar = 42}};
  test_types::BazFooResponse payload{{.res = std::move(res)}};
  fidl::Response<test_types::Baz::Foo> response{std::move(payload)};
  EXPECT_EQ(42, response.res().bar());
}

TEST(Response, InheritFromDomainObject) {
  static_assert(
      cpp17::is_base_of_v<test_types::BazFooResponse, fidl::Response<test_types::Baz::Foo>>);
  static_assert(sizeof(test_types::BazFooResponse) == sizeof(fidl::Response<test_types::Baz::Foo>),
                "Message wrapper must not add any state");
}

TEST(Response, DefaultConstructionErrorSyntax) {
  static_assert(
      !std::is_default_constructible_v<fidl::Response<test_types::ErrorSyntax::EmptyPayload>>);
  static_assert(
      !std::is_default_constructible_v<fidl::Response<test_types::ErrorSyntax::FooPayload>>);
}

TEST(Response, FromPayloadErrorSyntaxSuccess) {
  test_types::FooResponse res{{.bar = 42}};
  test_types::ErrorSyntaxFooPayloadTopResponse domain_object{
      {.result = test_types::ErrorSyntaxFooPayloadResult::WithResponse(std::move(res))}};
  fidl::Result<test_types::ErrorSyntax::FooPayload> result =
      fidl::internal::ConvertResponseDomainObjectToResult<test_types::ErrorSyntax::FooPayload>(
          domain_object);
  EXPECT_TRUE(result.is_ok());
  EXPECT_EQ(42, result.value().bar());
}

TEST(Response, FromPayloadErrorSyntaxError) {
  test_types::ErrorSyntaxFooPayloadTopResponse domain_object{
      {.result = test_types::ErrorSyntaxFooPayloadResult::WithErr(42)}};
  fidl::Result<test_types::ErrorSyntax::FooPayload> result =
      fidl::internal::ConvertResponseDomainObjectToResult<test_types::ErrorSyntax::FooPayload>(
          domain_object);
  EXPECT_FALSE(result.is_ok());
  EXPECT_TRUE(result.error_value().is_domain_error());
  EXPECT_EQ(42, result.error_value().domain_error());
}

TEST(Response, FromPayloadErrorSyntaxEmptyStructSuccess) {
  test_types::ErrorSyntaxEmptyPayloadTopResponse domain_object{
      {.result = test_types::ErrorSyntaxEmptyPayloadResult::WithResponse({})}};
  fidl::Result<test_types::ErrorSyntax::EmptyPayload> result =
      fidl::internal::ConvertResponseDomainObjectToResult<test_types::ErrorSyntax::EmptyPayload>(
          domain_object);
  EXPECT_TRUE(result.is_ok());
}

TEST(Response, FromPayloadErrorSyntaxEmptyStructError) {
  test_types::ErrorSyntaxEmptyPayloadTopResponse domain_object{
      {.result = test_types::ErrorSyntaxEmptyPayloadResult::WithErr(42)}};
  fidl::Result<test_types::ErrorSyntax::EmptyPayload> result =
      fidl::internal::ConvertResponseDomainObjectToResult<test_types::ErrorSyntax::EmptyPayload>(
          domain_object);
  EXPECT_FALSE(result.is_ok());
  EXPECT_TRUE(result.error_value().is_domain_error());
  EXPECT_EQ(42, result.error_value().domain_error());
}

TEST(Response, InheritFromDomainObjectErrorSyntax) {
  static_assert(cpp17::is_base_of_v<fit::result<int32_t, test_types::FooResponse>,
                                    fidl::Response<test_types::ErrorSyntax::FooPayload>>);
  static_assert(sizeof(fit::result<int32_t, test_types::FooResponse>) ==
                    sizeof(fidl::Response<test_types::ErrorSyntax::FooPayload>),
                "Message wrapper must not add any state");

  static_assert(cpp17::is_base_of_v<fit::result<int32_t>,
                                    fidl::Response<test_types::ErrorSyntax::EmptyPayload>>);
  static_assert(
      sizeof(fit::result<int32_t>) == sizeof(fidl::Response<test_types::ErrorSyntax::EmptyPayload>),
      "Message wrapper must not add any state");
}

TEST(Request, DefaultConstruction) {
  fidl::Request<test_types::Baz::Foo> request;
  EXPECT_EQ(0, request.req().bar());
}

TEST(Request, FromPayload) {
  test_types::FooRequest req{{.bar = 42}};
  test_types::BazFooRequest domain_object{{.req = std::move(req)}};
  fidl::Request<test_types::Baz::Foo> request{std::move(domain_object)};
  EXPECT_EQ(42, request.req().bar());
}

TEST(Request, InheritFromDomainObject) {
  static_assert(
      cpp17::is_base_of_v<test_types::BazFooRequest, fidl::Request<test_types::Baz::Foo>>);
  static_assert(sizeof(test_types::BazFooRequest) == sizeof(fidl::Request<test_types::Baz::Foo>),
                "Message wrapper must not add any state");
}

TEST(Event, DefaultConstruction) {
  fidl::Event<test_types::Baz::FooEvent> event;
  EXPECT_EQ(0, event.bar());
}

TEST(Event, FromPayload) {
  test_types::FooEvent body{{.bar = 42}};
  fidl::Event<test_types::Baz::FooEvent> event{std::move(body)};
  EXPECT_EQ(42, event.bar());
}

TEST(Event, InheritFromDomainObject) {
  static_assert(cpp17::is_base_of_v<test_types::FooEvent, fidl::Event<test_types::Baz::FooEvent>>);
  static_assert(sizeof(test_types::FooEvent) == sizeof(fidl::Event<test_types::Baz::FooEvent>),
                "Message wrapper must not add any state");
}

TEST(Event, DefaultConstructionErrorSyntax) {
  static_assert(
      !std::is_default_constructible_v<fidl::Event<test_types::ErrorSyntax::EventEmptyPayload>>);
  static_assert(
      !std::is_default_constructible_v<fidl::Event<test_types::ErrorSyntax::EventFooPayload>>);
}

TEST(Event, InheritFromDomainObjectErrorSyntax) {
  static_assert(cpp17::is_base_of_v<test_types::ErrorSyntaxEventFooPayloadRequest,
                                    fidl::Event<test_types::ErrorSyntax::EventFooPayload>>);
  static_assert(sizeof(test_types::ErrorSyntaxEventFooPayloadRequest) ==
                    sizeof(fidl::Event<test_types::ErrorSyntax::EventFooPayload>),
                "Message wrapper must not add any state");

  static_assert(cpp17::is_base_of_v<test_types::ErrorSyntaxEventEmptyPayloadRequest,
                                    fidl::Event<test_types::ErrorSyntax::EventEmptyPayload>>);
  static_assert(sizeof(test_types::ErrorSyntaxEventEmptyPayloadRequest) ==
                    sizeof(fidl::Event<test_types::ErrorSyntax::EventEmptyPayload>),
                "Message wrapper must not add any state");
}

TEST(Result, DefaultConstruction) {
  static_assert(
      !cpp17::is_default_constructible_v<fidl::Result<test_types::ErrorSyntax::EmptyPayload>>,
      "Cannot default construct invalid result type");
}
}  // namespace
