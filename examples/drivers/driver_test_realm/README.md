# Driver Test Realm Examples

Here are a few examples of Driver Test Realm being used in various languages.

There are 2 different ways of using Driver Test Realm.

## Hermetic

This is the recommended way to use Driver Test Realm. Every test gets its
own version of the Driver Test Realm component. This means every test is
hermetic, or isolated, from the other tests. Tests will not share any
state, as each Driver Test Realm is unique to that test.

## Non Hermetic

The non-hermetic way of using Driver Test Realm is to have a single Driver
Test Realm child component that is shared between every test instance.

The test author needs to be extra careful to make sure that their driver's
state is cleared between each tests so that the individual tests do not
interact with each other.

Note: In specific languages (like Rust) all tests are run simultaneously in
their own thread, which means using the non-hermetic Driver Test Realm
has the potential for data races if each test is interacting with the same
driver.

### Simple

This version of a non-hermetic test does not require any setup, it uses
the default configuration of Driver Test Realm. This is the same as
starting Driver Test Realm with empty arguments.