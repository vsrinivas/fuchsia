{protocol_docs}
template <typename D>
class {protocol_name} : public {base_class} {{
public:
    {protocol_name}() {{
        internal::Check{protocol_name}Subclass<D>();
{constructor_definition}
    }}

protected:
    {protocol_name_snake}_ops_t {protocol_name_snake}_ops_ = {{}};

private:
{protocol_definitions}
}};

class {protocol_name}Proxy {{
public:
    {protocol_name}Proxy()
        : ops_(nullptr), ctx_(nullptr) {{}}
    {protocol_name}Proxy(const {protocol_name_snake}_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {{}}

    void GetProto({protocol_name_snake}_t* proto) {{
        proto->ctx = ctx_;
        proto->ops = ops_;
    }}
    bool is_valid() {{
        return ops_ != nullptr;
    }}
    void clear() {{
        ctx_ = nullptr;
        ops_ = nullptr;
    }}

{proxy_definitions}
private:
    {protocol_name_snake}_ops_t* ops_;
    void* ctx_;
}};
