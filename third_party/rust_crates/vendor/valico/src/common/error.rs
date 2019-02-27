use std::error::Error;
use std::fmt::Debug;
use std::any::{Any, TypeId};
use std::mem;
use serde::{Serialize, Serializer};
use serde_json::{Value, to_value};

pub trait GetTypeId: Any { fn typeid(&self) -> TypeId { TypeId::of::<Self>() } }
impl<T: Any> GetTypeId for T {}

pub fn get_data_ptr<T: ?Sized>(d: *const T) -> *const () {
    d as *const ()
}

pub trait ValicoError : Error + Send + Debug + GetTypeId {
    fn get_code(&self) -> &str;
    fn get_path(&self) -> &str;
    fn get_title(&self) -> &str;
    fn get_detail(&self) -> Option<&str> { None }
}

impl ValicoError {
    /// Is this `Error` object of type `E`?
    pub fn is<E: ValicoError>(&self) -> bool { self.typeid() == TypeId::of::<E>() }

    /// If this error is `E`, downcast this error to `E`, by reference.
    pub fn downcast<E: ValicoError>(&self) -> Option<&E> {
        if self.is::<E>() {
            unsafe { Some(mem::transmute(get_data_ptr(self))) }
        } else {
            None
        }
    }
}

impl Serialize for ValicoError {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error> where S: Serializer {
        let mut map = ::serde_json::Map::new();
        map.insert("code".to_string(), to_value(self.get_code()).unwrap());
        map.insert("title".to_string(), to_value(self.get_title()).unwrap());
        map.insert("path".to_string(), to_value(self.get_path()).unwrap());
        match self.get_detail() {
            Some(ref detail) => { map.insert("detail".to_string(), to_value(detail).unwrap()); },
            None => ()
        }
        Value::Object(map).serialize(serializer)
    }
}

pub type ValicoErrors = Vec<Box<ValicoError>>;

macro_rules! impl_basic_err {
    ($err:ty, $code:expr) => {
        impl ::std::error::Error for $err {
            fn description(&self) -> &str {
                $code
            }
        }

        impl ::std::fmt::Display for $err {
            fn fmt(&self, formatter: &mut ::std::fmt::Formatter) -> ::std::fmt::Result {
                self.description().fmt(formatter)
            }
        }
    }
}

macro_rules! impl_err {
    ($err:ty, $code:expr, $title:expr) => {
        impl_basic_err!($err, $code);

        impl $crate::common::error::ValicoError for $err {
            fn get_code(&self) -> &str { $code }
            fn get_title(&self) -> &str { $title }
            fn get_path(&self) -> &str { self.path.as_ref() }
        }
    };

    ($err:ty, $code:expr, $title:expr, +detail) => {
        impl_basic_err!($err, $code);

        impl $crate::common::error::ValicoError for $err {
            fn get_code(&self) -> &str { $code }
            fn get_title(&self) -> &str { $title }
            fn get_path(&self) -> &str { self.path.as_ref() }
            fn get_detail(&self) -> Option<&str> { Some(self.detail.as_ref()) }
        }
    };

    ($err:ty, $code:expr, $title:expr, +opt_detail) => {
        impl_basic_err!($err, $code);

        impl $crate::common::error::ValicoError for $err {
            fn get_code(&self) -> &str { $code }
            fn get_title(&self) -> &str { $title }
            fn get_path(&self) -> &str { self.path.as_ref() }
            fn get_detail(&self) -> Option<&str> { self.detail.as_ref().map(|s| s.as_ref()) }
        }
    }
}

macro_rules! impl_serialize{
    ($err:ty) => {
        impl Serialize for $err {
            fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error> where S: Serializer {
                let mut map = ::serde_json::Map::new();
                map.insert("code".to_string(), to_value(self.get_code()).unwrap());
                map.insert("title".to_string(), to_value(self.get_title()).unwrap());
                map.insert("path".to_string(), to_value(self.get_path()).unwrap());
                match self.get_detail() {
                    Some(ref detail) => { map.insert("detail".to_string(), to_value(detail).unwrap()); },
                    None => ()
                }
                Value::Object(map).serialize(serializer)
            }
        }
    };
    ($err:ty, $($sp:expr),+) => {
        impl Serialize for $err {
            fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error> where S: Serializer {
                let mut map = ::serde_json::Map::new();
                map.insert("code".to_string(), to_value(self.get_code()).unwrap());
                map.insert("title".to_string(), to_value(self.get_title()).unwrap());
                map.insert("path".to_string(), to_value(self.get_path()).unwrap());
                match self.get_detail() {
                    Some(ref detail) => { map.insert("detail".to_string(), to_value(detail).unwrap()); },
                    None => ()
                }
                $({
                    let closure = $sp;
                    closure(self, &mut map);
                })+
                Value::Object(map).serialize(serializer)
            }
        }
    }
}
