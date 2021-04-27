#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct {name}(pub {ty});

impl {name} {{
{enum_decls}
}}

impl std::ops::BitAnd for {name} {{
    type Output = Self;
    fn bitand(self, rhs: Self) -> Self {{
        Self(self.0 & rhs.0)
    }}
}}

impl std::ops::BitAndAssign for {name} {{
    fn bitand_assign(&mut self, rhs: Self) {{
        *self = Self(self.0 & rhs.0)
    }}
}}

impl std::ops::BitOr for {name} {{
    type Output = Self;
    fn bitor(self, rhs: Self) -> Self {{
        Self(self.0 | rhs.0)
    }}
}}

impl std::ops::BitOrAssign for {name} {{
    fn bitor_assign(&mut self, rhs: Self) {{
        *self = Self(self.0 | rhs.0)
    }}
}}

impl std::ops::BitXor for {name} {{
    type Output = Self;
    fn bitxor(self, rhs: Self) -> Self {{
        Self(self.0 ^ rhs.0)
    }}
}}

impl std::ops::BitXorAssign for {name} {{
    fn bitxor_assign(&mut self, rhs: Self) {{
        *self = Self(self.0 ^ rhs.0)
    }}
}}
