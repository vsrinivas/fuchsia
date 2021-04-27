    virtual Mock{protocol_name}& Expect{method_name}({params}) {{
        mock_{method_name_snake}_.ExpectCall({args});
        return *this;
    }}
