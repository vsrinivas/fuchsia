#[repr({alignment})]
#[derive(Copy, Clone)]
pub union {name} {{
{union_fields}
}}

// unions can't autoderive debug, but it's useful for their parent types to
impl std::fmt::Debug for {name} {{
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {{
        write!(f, "<{name}>")
    }}
}}
