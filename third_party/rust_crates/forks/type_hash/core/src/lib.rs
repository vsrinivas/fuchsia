use std::borrow::ToOwned;
use std::hash::Hasher;

/// A hash of a type's structure
pub trait TypeHash {
    fn type_hash() -> u64 {
        let mut hasher = fnv::FnvHasher::default();
        Self::write_hash(&mut hasher);
        hasher.finish()
    }

    /// Write the structure of the type to the hasher
    fn write_hash(hasher: &mut impl Hasher);
}

macro_rules! impl_type_hash {
    ($( $($ty: ident)::* $(<$($l: lifetime,)* $($T: ident $(: $(? $Sized: ident)? $($(+)? $B: ident)*)?),+>)?,)*) => {
        $(
            impl $(<$($l,)* $($T: $crate::TypeHash $($(+ ?$Sized)? $(+ $B)*)? ),*>)? TypeHash for $($ty)::* $(<$($l,)* $($T),+>)? {
                fn write_hash(hasher: &mut impl std::hash::Hasher) {
                    hasher.write(stringify!($($ty)::*).as_bytes());
                    $($(
                        $T::write_hash(hasher);
                    )+)?
                }
            }
        )*
    };
}

impl_type_hash!(
    bool,
    u8,
    i8,
    u16,
    i16,
    u32,
    i32,
    u64,
    i64,
    u128,
    i128,
    usize,
    isize,
    f32,
    f64,
    str,
    std::any::TypeId,
    std::borrow::Cow<'a, T: ?Sized + ToOwned>,
    std::boxed::Box<T: ?Sized>,
    std::cell::Cell<T: ?Sized>,
    std::cell::Ref<'a, T: ?Sized>,
    std::cell::RefCell<T: ?Sized>,
    std::cell::RefMut<'a, T>,
    std::cell::UnsafeCell<T>,
    std::cmp::Ordering,
    std::cmp::Reverse<T>,
    std::collections::BinaryHeap<T>,
    std::collections::BTreeMap<K, V>,
    std::collections::BTreeSet<T>,
    std::collections::HashMap<K, V>,
    std::collections::HashSet<T>,
    std::collections::LinkedList<T>,
    std::collections::VecDeque<T>,
    std::ffi::c_void,
    std::ffi::CStr,
    std::ffi::CString,
    std::ffi::OsStr,
    std::ffi::OsString,
    std::hash::BuildHasherDefault<T>,
    std::marker::PhantomData<T: ?Sized>,
    std::mem::ManuallyDrop<T: ?Sized>,
    std::mem::MaybeUninit<T>,
    std::net::IpAddr,
    std::net::Ipv4Addr,
    std::net::Ipv6Addr,
    std::net::SocketAddr,
    std::net::SocketAddrV4,
    std::net::SocketAddrV6,
    std::num::FpCategory,
    std::num::NonZeroI128,
    std::num::NonZeroI16,
    std::num::NonZeroI32,
    std::num::NonZeroI64,
    std::num::NonZeroI8,
    std::num::NonZeroIsize,
    std::num::NonZeroU128,
    std::num::NonZeroU16,
    std::num::NonZeroU32,
    std::num::NonZeroU64,
    std::num::NonZeroU8,
    std::num::NonZeroUsize,
    std::num::Wrapping<T>,
    std::ops::Bound<T>,
    std::ops::Range<T>,
    std::ops::RangeFrom<T>,
    std::ops::RangeInclusive<T>,
    std::ops::RangeFull,
    std::ops::RangeTo<T>,
    std::ops::RangeToInclusive<T>,
    std::option::Option<T>,
    std::path::Path,
    std::path::PathBuf,
    std::pin::Pin<T>,
    std::primitive::char,
    std::ptr::NonNull<T: ?Sized>,
    std::rc::Rc<T: ?Sized>,
    std::rc::Weak<T: ?Sized>,
    std::result::Result<T, E>,
    std::string::String,
    std::sync::atomic::AtomicBool,
    std::sync::atomic::AtomicI16,
    std::sync::atomic::AtomicI32,
    std::sync::atomic::AtomicI64,
    std::sync::atomic::AtomicI8,
    std::sync::atomic::AtomicIsize,
    std::sync::atomic::AtomicPtr<T>,
    std::sync::atomic::AtomicU16,
    std::sync::atomic::AtomicU32,
    std::sync::atomic::AtomicU64,
    std::sync::atomic::AtomicU8,
    std::sync::atomic::AtomicUsize,
    std::sync::mpsc::Receiver<T>,
    std::sync::mpsc::Sender<T>,
    std::sync::mpsc::SyncSender<T>,
    std::sync::Arc<T: ?Sized>,
    std::sync::Mutex<T: ?Sized>,
    std::sync::Once,
    std::sync::RwLock<T: ?Sized>,
    std::sync::RwLockReadGuard<'a, T: ?Sized>,
    std::sync::RwLockWriteGuard<'a, T: ?Sized>,
    std::sync::Weak<T: ?Sized>,
    std::thread::Builder,
    std::thread::JoinHandle<T>,
    std::thread::LocalKey<T>,
    std::thread::Thread,
    std::thread::ThreadId,
    std::time::Duration,
    std::time::Instant,
    std::time::SystemTime,
    std::vec::Vec<T>,
);

macro_rules! impl_type_hash_tuple {
    (($($T: ident,)*)) => {
        impl <$($T: $crate::TypeHash),*> TypeHash for ($($T,)*) {
            fn write_hash(hasher: &mut impl std::hash::Hasher) {
                hasher.write(b"()");
                $(
                    $T::write_hash(hasher);
                )*
            }
        }
    };
}

impl_type_hash_tuple!(());
impl_type_hash_tuple!((A,));
impl_type_hash_tuple!((A, B,));
impl_type_hash_tuple!((A, B, C,));
impl_type_hash_tuple!((A, B, C, D,));
impl_type_hash_tuple!((A, B, C, D, E,));
impl_type_hash_tuple!((A, B, C, D, E, F,));
impl_type_hash_tuple!((A, B, C, D, E, F, G,));
impl_type_hash_tuple!((A, B, C, D, E, F, G, H,));
impl_type_hash_tuple!((A, B, C, D, E, F, G, H, I,));
impl_type_hash_tuple!((A, B, C, D, E, F, G, H, I, J,));
impl_type_hash_tuple!((A, B, C, D, E, F, G, H, I, J, K,));
impl_type_hash_tuple!((A, B, C, D, E, F, G, H, I, J, K, L,));

macro_rules! impl_type_hash_array {
    ([$T: ident; $n: literal]) => {
        impl<$T: $crate::TypeHash> TypeHash for [$T; $n] {
            fn write_hash(hasher: &mut impl std::hash::Hasher) {
                hasher.write(b"[;]");
                hasher.write_usize($n);
                $T::write_hash(hasher);
            }
        }
    };
}

impl_type_hash_array!([T; 0]);
impl_type_hash_array!([T; 1]);
impl_type_hash_array!([T; 2]);
impl_type_hash_array!([T; 3]);
impl_type_hash_array!([T; 4]);
impl_type_hash_array!([T; 5]);
impl_type_hash_array!([T; 6]);
impl_type_hash_array!([T; 7]);
impl_type_hash_array!([T; 8]);
impl_type_hash_array!([T; 9]);
impl_type_hash_array!([T; 10]);
impl_type_hash_array!([T; 11]);
impl_type_hash_array!([T; 12]);
impl_type_hash_array!([T; 13]);
impl_type_hash_array!([T; 14]);
impl_type_hash_array!([T; 15]);
impl_type_hash_array!([T; 16]);
impl_type_hash_array!([T; 17]);
impl_type_hash_array!([T; 18]);
impl_type_hash_array!([T; 19]);
impl_type_hash_array!([T; 20]);
impl_type_hash_array!([T; 21]);
impl_type_hash_array!([T; 22]);
impl_type_hash_array!([T; 23]);
impl_type_hash_array!([T; 24]);
impl_type_hash_array!([T; 25]);
impl_type_hash_array!([T; 26]);
impl_type_hash_array!([T; 27]);
impl_type_hash_array!([T; 28]);
impl_type_hash_array!([T; 29]);
impl_type_hash_array!([T; 30]);
impl_type_hash_array!([T; 31]);
impl_type_hash_array!([T; 32]);

impl<T: TypeHash + ?Sized> TypeHash for *const T {
    fn write_hash(hasher: &mut impl Hasher) {
        hasher.write(b"*const");
        T::write_hash(hasher);
    }
}

impl<T: TypeHash + ?Sized> TypeHash for *mut T {
    fn write_hash(hasher: &mut impl Hasher) {
        hasher.write(b"*mut");
        T::write_hash(hasher);
    }
}

impl<T: TypeHash> TypeHash for [T] {
    fn write_hash(hasher: &mut impl Hasher) {
        hasher.write(b"[]");
        T::write_hash(hasher);
    }
}

impl<'a, T: TypeHash + ?Sized> TypeHash for &'a T {
    fn write_hash(hasher: &mut impl Hasher) {
        hasher.write(b"&");
        T::write_hash(hasher);
    }
}

impl<'a, T: TypeHash + ?Sized> TypeHash for &'a mut T {
    fn write_hash(hasher: &mut impl Hasher) {
        hasher.write(b"&mut");
        T::write_hash(hasher);
    }
}
