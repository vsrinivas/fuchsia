class Mock{protocol_name} : ddk::{protocol_name}Protocol<Mock{protocol_name}> {{
public:
    Mock{protocol_name} : proto_{{&{protocol_name_snake}_protocol_ops_, this}} {{}}

    const {protocol_name_snake}_protocol_t* GetProto() const {{ return &proto_; }}

{mock_expects}
    void VerifyAndClear() {{
{mock_verify}
    }}

{protocol_definitions}
private:
    const {protocol_name_snake}_protocol_t proto_;
{mock_definitions}
}};

