//! This crate defines a collection of useful utilities for testing rust code.

/// Asserts that an expression matches some expected pattern. The pattern may
/// be any valid rust pattern, which includes multiple patterns (`Foo | Bar`)
/// and an arm guard (`Foo::Bar(ref val) if val == "foo"`).
///
/// On panic, this macro will print the value of the input expression
/// using its debug representation, along with the expected pattern.
///
/// # Examples
///
/// ```
/// ##[derive(Debug)]
/// enum Foo {
///     Bar(String),
///     Baz,
/// }
///
/// # fn main() {
/// let a = Foo::Baz;
/// assert_matches!(a, Foo::Baz);
///
/// let b = Foo::Bar("foo".to_owned());
/// assert_matches!(b, Foo::Bar(ref val) if val == "foo");
/// # }
#[macro_export]
macro_rules! assert_matches {
    ($input:expr, $($pat:pat)|+) => {
        match $input {
            $($pat)|* => (),
            _ => panic!(
                r#"assertion failed: `(actual matches expected)`
   actual: `{:?}`,
 expected: `{}`"#,
                &$input,
                stringify!($($pat)|*)
            ),
        };
    };

    ($input:expr, $($pat:pat)|+ if $arm_guard:expr) => {
        match $input {
            $($pat)|* if $arm_guard => (),
            _ => panic!(
                r#"assertion failed: `(actual matches expected)`
   actual: `{:?}`,
 expected: `{} if {}`"#,
                &$input,
                stringify!($($pat)|*),
                stringify!($arm_guard)
            ),
        };
    };
}

#[cfg(test)]
mod tests {
    #[derive(Debug)]
    enum Foo {
        Bar(String),
        Baz,
        Bat,
    }

    #[test]
    fn test_simple_match_passes() {
        let input = Foo::Bar("foo".to_owned());
        assert_matches!(input, Foo::Bar(_));
    }

    #[test]
    #[should_panic]
    fn test_simple_match_fails() {
        let input = Foo::Baz;
        assert_matches!(input, Foo::Bar(_));
    }

    #[test]
    fn test_match_with_arm_guard_passes() {
        let input = Foo::Bar("foo".to_owned());
        assert_matches!(input, Foo::Bar(ref val) if val == "foo");
    }

    #[test]
    #[should_panic]
    fn test_match_with_arm_guard_fails() {
        let input = Foo::Bar("foo".to_owned());
        assert_matches!(input, Foo::Bar(ref val) if val == "bar");
    }

    #[test]
    fn test_match_with_multiple_patterns_passes() {
        let input = Foo::Bar("foo".to_owned());
        assert_matches!(input, Foo::Bar(_) | Foo::Baz);
    }

    #[test]
    #[should_panic]
    fn test_match_with_multiple_patterns_fails() {
        let input = Foo::Bat;
        assert_matches!(input, Foo::Bar(_) | Foo::Baz);
    }

    #[test]
    fn test_match_with_multiple_patterns_and_arm_guard_passes() {
        let input = Foo::Bar("foo".to_owned());
        assert_matches!(input, Foo::Bar(_) | Foo::Baz if true);
    }

    #[test]
    #[should_panic]
    fn test_match_with_multiple_patterns_and_arm_guard_fails() {
        let input = Foo::Bat;
        assert_matches!(input, Foo::Bar(_) | Foo::Baz if false);
    }

    #[test]
    fn test_assertion_does_not_consume_input() {
        let input = Foo::Bar("foo".to_owned());
        assert_matches!(input, Foo::Bar(_));
        if let Foo::Bar(ref val) = input {
            assert_eq!(val, "foo");
        }
    }
}
