
#[macro_use]
pub(crate) mod crate_macros {
    /// Macro to read an exact buffer
    macro_rules! read_exact_buff {
        ($bufid:ident, $rdr:expr, $buflen:expr) => {
            {
                let mut $bufid = [0_u8; $buflen];
                let _ = $rdr.read_exact(&mut $bufid)?;
                $bufid
            }
        }
    }
}

#[macro_use]
pub mod pub_macros {

    /// Macro to create const for partition types. 
    macro_rules! partition_types {
    (
        $(
            $(#[$docs:meta])*
            ($upcase:ident, $guid:expr, $os:expr)$(,)*
        )+
    ) => {
        $(
            $(#[$docs])*
            pub const $upcase: Type = Type {
                guid: $guid,
                os: $os,
            };
        )+

        impl FromStr for Type {
            type Err = String;
            fn from_str(s: &str) -> Result<Self, Self::Err> {
                match s {
                    $(
                        $guid => Ok($upcase),
                        stringify!($upcase) => Ok($upcase),
                    )+
                    _ => Err("Invalid or unknown Partition Type GUID.".to_string()),
                }
            }
        }
    }
}
}