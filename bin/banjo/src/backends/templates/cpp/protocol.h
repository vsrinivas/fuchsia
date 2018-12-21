{protocol_docs}
template <typename D, typename Base = internal::base_mixin>
class {protocol_name}Protocol : public Base {{
public:
    {protocol_name}Protocol() {{
        internal::Check{protocol_name}ProtocolSubclass<D>();
{constructor_definition}
    }}

protected:
    {protocol_name_snake}_protocol_ops_t {protocol_name_snake}_protocol_ops_ = {{}};

private:
{protocol_definitions}
}};

class {protocol_name}ProtocolProxy {{
public:
    {protocol_name}ProtocolProxy()
        : ops_(nullptr), ctx_(nullptr) {{}}
    {protocol_name}ProtocolProxy(const {protocol_name_snake}_protocol_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {{}}

    void GetProto({protocol_name_snake}_protocol_t* proto) {{
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
    {protocol_name_snake}_protocol_ops_t* ops_;
    void* ctx_;
}};
