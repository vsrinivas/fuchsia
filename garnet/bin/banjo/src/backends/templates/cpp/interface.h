{protocol_docs}
template <typename D>
class {protocol_name} : public internal::base_mixin {{
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

class {protocol_name}Client {{
public:
    {protocol_name}Client()
        : ops_(nullptr), ctx_(nullptr) {{}}
    {protocol_name}Client(const {protocol_name_snake}_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {{}}

    void GetProto({protocol_name_snake}_t* proto) const {{
        proto->ctx = ctx_;
        proto->ops = ops_;
    }}
    bool is_valid() const {{
        return ops_ != nullptr;
    }}
    void clear() {{
        ctx_ = nullptr;
        ops_ = nullptr;
    }}

{client_definitions}
private:
    {protocol_name_snake}_ops_t* ops_;
    void* ctx_;
}};
