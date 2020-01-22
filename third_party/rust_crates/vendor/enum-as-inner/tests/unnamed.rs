use enum_as_inner::EnumAsInner;

#[derive(Debug, EnumAsInner)]
enum ManyVariants {
    One(u32),
    Two(u32, i32),
    Three(bool, u32, i64),
}

#[test]
fn test_one_unnamed() {
    let many = ManyVariants::One(1);

    assert!(many.as_one().is_some());
    assert!(many.as_two().is_none());
    assert!(many.as_three().is_none());

    assert_eq!(*many.as_one().unwrap(), 1_u32);
    assert_eq!(many.into_one().unwrap(), 1_u32);
}

#[test]
fn test_two_unnamed() {
    let many = ManyVariants::Two(1, 2);

    assert!(many.as_one().is_none());
    assert!(many.as_two().is_some());
    assert!(many.as_three().is_none());

    assert_eq!(many.as_two().unwrap(), (&1_u32, &2_i32));
    assert_eq!(many.into_two().unwrap(), (1_u32, 2_i32));
}

#[test]
fn test_three_unnamed() {
    let many = ManyVariants::Three(true, 1, 2);

    assert!(many.as_one().is_none());
    assert!(many.as_two().is_none());
    assert!(many.as_three().is_some());

    assert_eq!(many.as_three().unwrap(), (&true, &1_u32, &2_i64));
    assert_eq!(many.into_three().unwrap(), (true, 1_u32, 2_i64));
}
