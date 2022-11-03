#![allow(unused)]
use type_hash::TypeHash;

#[test]
fn type_hashes_same_for_skipped_field_as_type_without_field() {
    assert_eq!(v1::MyStruct::type_hash(), v2::MyStruct::type_hash(),);
}

#[test]
fn type_hashes_same_for_skipped_unnamed_field_as_type_without_field() {
    assert_eq!(
        v1::MyTupleStruct::type_hash(),
        v2::MyTupleStruct::type_hash(),
    );
}

mod v1 {
    use type_hash::TypeHash;
    #[derive(TypeHash)]
    pub struct MyStruct {
        #[type_hash(skip)]
        a: bool,
        b: u32,
    }
    #[derive(TypeHash)]
    pub struct MyTupleStruct(#[type_hash(skip)] bool, u32);
}

mod v2 {
    use type_hash::TypeHash;
    #[derive(TypeHash)]
    pub struct MyStruct {
        b: u32,
    }

    #[derive(TypeHash)]
    pub struct MyTupleStruct(u32);
}
