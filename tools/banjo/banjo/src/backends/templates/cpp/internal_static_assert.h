    static_assert(internal::has_{protocol_name_snake}_protocol_{method_name_snake}<D>::value,
        "{protocol_name}Protocol subclasses must implement "
        "{return_param} {protocol_name}{method_name}({params});");
