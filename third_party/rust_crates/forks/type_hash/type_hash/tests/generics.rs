use type_hash::TypeHash;

#[derive(TypeHash)]
#[allow(unused)]
enum Foo<A, B> {
    A(A),
    B(Vec<B>),
}

#[test]
fn different_generic_args_give_different_hashes() {
    assert_ne!(Foo::<i32, bool>::type_hash(), Foo::<i32, u64>::type_hash())
}

#[test]
fn same_generic_args_give_same_hashes() {
    assert_eq!(Foo::<i32, bool>::type_hash(), Foo::<i32, bool>::type_hash())
}
