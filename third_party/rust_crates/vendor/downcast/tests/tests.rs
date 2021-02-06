#[macro_use]
extern crate downcast;
use downcast::Any;

trait Simple: Any {}
downcast!(Simple);

trait WithParams<T, U>: Any {}
downcast!(<T, U> WithParams<T, U>);
struct Param1;
struct Param2;

struct ImplA {
    data: String
}
impl Simple for ImplA {}
impl WithParams<Param1, Param2> for ImplA {}

struct ImplB;
impl Simple for ImplB {}
impl WithParams<Param1, Param2> for ImplB {}

#[test]
fn simple(){
    let mut a: Box<Simple> = Box::new(ImplA{ data: "data".into() });

    assert_eq!(a.downcast_ref::<ImplA>().unwrap().data, "data");
    assert!(a.downcast_ref::<ImplB>().is_err());

    assert_eq!(a.downcast_mut::<ImplA>().unwrap().data, "data");
    assert!(a.downcast_mut::<ImplB>().is_err());

    assert_eq!(a.downcast::<ImplA>().unwrap().data, "data");
}

#[test]
fn with_params(){
    let mut a: Box<WithParams<Param1, Param2>> = Box::new(ImplA{ data: "data".into() });

    assert_eq!(a.downcast_ref::<ImplA>().unwrap().data, "data");
    assert!(a.downcast_ref::<ImplB>().is_err());

    assert_eq!(a.downcast_mut::<ImplA>().unwrap().data, "data");
    assert!(a.downcast_mut::<ImplB>().is_err());

    assert_eq!(a.downcast::<ImplA>().unwrap().data, "data");
}
