cfg_if! {
    if #[cfg(all(
        crc32fast_stdarchx86,
        any(target_arch = "x86", target_arch = "x86_64")
    ))] {
        mod pclmulqdq;
        pub use self::pclmulqdq::State;
    } else {
        #[derive(Clone)]
        pub enum State {}
        impl State {
            pub fn new() -> Option<Self> {
                None
            }

            pub fn update(&mut self, _buf: &[u8]) {
                match *self {}
            }

            pub fn finalize(self) -> u32 {
                match self{}
            }

            pub fn reset(&mut self) {
                match *self {}
            }

            pub fn combine(&mut self, _other: u32, _amount: u64) {
                match *self {}
            }
        }
    }
}
