use enum_as_inner::EnumAsInner;

#[derive(EnumAsInner)]
enum UnitVariants {
    Zero,
    One,
    Two,
}

#[test]
fn test_zero_unit() {
    let unit = UnitVariants::Zero;

    assert!(unit.as_zero().is_some());
    assert!(unit.as_one().is_none());
    assert!(unit.as_two().is_none());

    assert_eq!(unit.as_zero().unwrap(), ());
}

#[test]
fn test_one_unit() {
    let unit = UnitVariants::One;

    assert!(unit.as_zero().is_none());
    assert!(unit.as_one().is_some());
    assert!(unit.as_two().is_none());

    assert_eq!(unit.as_one().unwrap(), ());
}

#[test]
fn test_two_unit() {
    let unit = UnitVariants::Two;

    assert!(unit.as_zero().is_none());
    assert!(unit.as_one().is_none());
    assert!(unit.as_two().is_some());

    assert_eq!(unit.as_two().unwrap(), ());
}
