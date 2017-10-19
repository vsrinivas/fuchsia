use sys;

bitflags! {
    /// Rights associated with a handle.
    ///
    /// See [rights.md](https://fuchsia.googlesource.com/zircon/+/master/docs/rights.md)
    /// for more information.
    #[repr(C)]
    pub struct Rights: sys::zx_rights_t {
        const NONE         = sys::ZX_RIGHT_NONE;
        const DUPLICATE    = sys::ZX_RIGHT_DUPLICATE;
        const TRANSFER     = sys::ZX_RIGHT_TRANSFER;
        const READ         = sys::ZX_RIGHT_READ;
        const WRITE        = sys::ZX_RIGHT_WRITE;
        const EXECUTE      = sys::ZX_RIGHT_EXECUTE;
        const MAP          = sys::ZX_RIGHT_MAP;
        const GET_PROPERTY = sys::ZX_RIGHT_GET_PROPERTY;
        const SET_PROPERTY = sys::ZX_RIGHT_SET_PROPERTY;
        const DEBUG        = sys::ZX_RIGHT_DEBUG;
        const SAME_RIGHTS  = sys::ZX_RIGHT_SAME_RIGHTS;
    }
}