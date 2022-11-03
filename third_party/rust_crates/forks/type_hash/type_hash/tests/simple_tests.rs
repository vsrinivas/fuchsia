use type_hash::TypeHash;

#[test]
fn same_built_in_type_has_same_hash() {
    assert_eq!(u64::type_hash(), u64::type_hash());
    assert_eq!(bool::type_hash(), bool::type_hash());
    assert_eq!(
        <(String, Vec::<&'static str>) as TypeHash>::type_hash(),
        <(String, Vec::<&'static str>) as TypeHash>::type_hash()
    );
}

#[test]
fn different_built_in_type_has_different_hash() {
    assert_ne!(u64::type_hash(), i64::type_hash());
    assert_ne!(bool::type_hash(), u8::type_hash());
    assert_ne!(
        <(String, Vec::<&'static mut str>) as TypeHash>::type_hash(),
        <(String, Vec::<&'static str>) as TypeHash>::type_hash()
    );
}

#[test]
fn same_custom_type_has_same_hash() {
    assert_eq!(v1::Foo::type_hash(), v1::Foo::type_hash());
    assert_eq!(v1::Bar::type_hash(), v1::Bar::type_hash());
    assert_eq!(v1::Wibble::type_hash(), v1::Wibble::type_hash());
}

#[test]
fn different_custom_type_with_same_name_and_structure_has_same_hash() {
    assert_eq!(v1::SameStruct::type_hash(), v2::SameStruct::type_hash());
    assert_eq!(v1::SameEnum::type_hash(), v2::SameEnum::type_hash());
}

#[test]
fn different_custom_type_with_overridden_field_type_has_same_hash() {
    assert_eq!(v1::Foo::type_hash(), v3::Foo::type_hash());
    assert_eq!(v1::SameStruct::type_hash(), v3::SameStruct::type_hash());
}

#[test]
fn custom_type_with_different_structure_has_different_hash() {
    assert_ne!(v1::Foo::type_hash(), v2::Foo::type_hash());
    assert_ne!(v1::Bar::type_hash(), v2::Bar::type_hash());
    assert_ne!(v1::Wibble::type_hash(), v2::Wibble::type_hash());
}

#[allow(unused)]
mod v1 {
    use type_hash::TypeHash;

    #[derive(TypeHash)]
    pub struct Foo {
        a: i64,
        b: String,
    }

    #[derive(TypeHash)]
    pub enum Bar {
        A = 1,
        B = 2,
    }

    #[derive(TypeHash)]
    pub enum Wibble {
        A(Bar),
        B(Foo, Foo),
        C { foo: Foo },
    }

    #[derive(TypeHash)]
    pub struct SameStruct {
        x: Vec<u32>,
        y: SameEnum,
    }

    #[derive(TypeHash)]
    pub enum SameEnum {
        X,
        Y(Box<[String]>, bool),
    }
}

#[allow(unused)]
mod v2 {
    use type_hash::TypeHash;

    #[derive(TypeHash)]
    pub struct Foo {
        a: u64, // <-- this field has a different type
        b: String,
    }

    #[derive(TypeHash)]
    pub enum Bar {
        A = 1,
        B = 3,
    }

    #[derive(TypeHash)]
    pub enum Wibble {
        A(Bar),
        B(Foo, Foo),
        C { foo: Foo },
    }

    #[derive(TypeHash)]
    pub struct SameStruct {
        x: Vec<u32>,
        y: SameEnum,
    }

    #[derive(TypeHash)]
    pub enum SameEnum {
        X,
        Y(Box<[String]>, bool),
    }
}

#[allow(unused)]
mod v3 {
    use type_hash::TypeHash;

    #[derive(TypeHash)]
    pub struct Foo {
        #[type_hash(as = "i64")]
        a: u64, // <-- this field has a different type, but treat it as the same
        b: String,
    }

    #[derive(TypeHash)]
    pub enum Bar {
        A = 1,
        B = 3,
    }

    #[derive(TypeHash)]
    pub enum Wibble {
        A(Bar),
        B(Foo, Foo),
        C { foo: Foo },
    }

    #[derive(TypeHash)]
    pub struct SameStruct {
        x: Vec<u32>,
        #[type_hash(as = "super::v1::SameEnum")]
        y: DifferentEnum,
    }

    #[derive(TypeHash)]
    pub enum DifferentEnum {
        X,
        Abc(bool),
    }
}
