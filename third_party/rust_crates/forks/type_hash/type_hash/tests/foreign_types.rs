#![allow(unused)]
use type_hash::TypeHash;

#[test]
fn foreign_type_hashes_different_for_different_foreign_types() {
    assert_ne!(v1::MyStruct::type_hash(), v2::MyStruct::type_hash(),);
}

#[test]
fn foreign_type_hashes_in_unnamed_field_different_for_different_foreign_types() {
    assert_ne!(
        v1::MyTupleStruct::type_hash(),
        v2::MyTupleStruct::type_hash(),
    );
}

mod v1 {
    use type_hash::TypeHash;
    #[derive(TypeHash)]
    pub struct MyStruct {
        #[type_hash(foreign_type)]
        foreign: ForeignType,
    }

    #[derive(TypeHash)]
    pub struct MyTupleStruct(#[type_hash(foreign_type)] ForeignType);

    // Does not implement TypeHash
    pub struct ForeignType {
        a: u64,
    }
}

mod v2 {
    use type_hash::TypeHash;
    #[derive(TypeHash)]
    pub struct MyStruct {
        #[type_hash(foreign_type)]
        foreign: DifferentForeignType,
    }

    #[derive(TypeHash)]
    pub struct MyTupleStruct(#[type_hash(foreign_type)] DifferentForeignType);

    // Does not implement TypeHash
    pub struct DifferentForeignType {
        a: u64,
    }
}
