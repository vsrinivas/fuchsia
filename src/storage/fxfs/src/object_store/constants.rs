pub const INVALID_OBJECT_ID: u64 = 0xffffffffffffffff;
pub const ROOT_PARENT_STORE_OBJECT_ID: u64 = 0xfffffffffffffffe;
// This only exists in the root parent store.
pub const SUPER_BLOCK_OBJECT_ID: u64 = 0xfffffffffffffffd;

pub const LOWEST_SPECIAL_OBJECT_ID: u64 = SUPER_BLOCK_OBJECT_ID;

pub const RESERVED_OBJECT_ID: u64 = 0;

// The first two blocks on the disk are reserved for the super block.
pub const MIN_SUPER_BLOCK_SIZE: u64 = 8192;
