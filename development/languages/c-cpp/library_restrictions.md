# Library restrictions

## third_party/googletest

*** aside
Note that the googletest library includes both the former gtest and gmock
projects.
***

Decision: **do not use** the mocking functionality of gmock (`MOCK_METHOD` and
`EXPECT_CALL`). It is allowed to use gmock matchers (such as `ElementsAre()`).
