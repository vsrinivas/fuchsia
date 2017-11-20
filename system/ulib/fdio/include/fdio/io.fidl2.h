// header file
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <fidl/coding.h>
#include <fidl/types.h>
#include <zircon/syscalls/object.h>

#if defined(__cplusplus)
extern "C" {
#endif


// Forward declarations

typedef uint32_t SeekOrigin;
#define SeekOrigin_Start UINT32_C(0)
#define SeekOrigin_Current UINT32_C(1)
#define SeekOrigin_End UINT32_C(2)

typedef struct Clone_request Clone_request;
typedef struct Close_request Close_request;
typedef struct Close_response Close_response;
typedef struct ListInterfaces_request ListInterfaces_request;
typedef struct ListInterfaces_response ListInterfaces_response;
typedef struct Bind_request Bind_request;
typedef struct Describe_request Describe_request;
typedef struct Describe_response Describe_response;
typedef struct OnOpen_response OnOpen_response;
typedef struct Sync_request Sync_request;
typedef struct Sync_response Sync_response;
typedef struct GetAttr_request GetAttr_request;
typedef struct GetAttr_response GetAttr_response;
typedef struct SetAttr_request SetAttr_request;
typedef struct SetAttr_response SetAttr_response;
typedef struct Ioctl_request Ioctl_request;
typedef struct Ioctl_response Ioctl_response;
typedef struct Ioctl1H_request Ioctl1H_request;
typedef struct Ioctl1H_response Ioctl1H_response;
typedef struct Read_request Read_request;
typedef struct Read_response Read_response;
typedef struct ReadAt_request ReadAt_request;
typedef struct ReadAt_response ReadAt_response;
typedef struct Write_request Write_request;
typedef struct Write_response Write_response;
typedef struct WriteAt_request WriteAt_request;
typedef struct WriteAt_response WriteAt_response;
typedef struct Seek_request Seek_request;
typedef struct Seek_response Seek_response;
typedef struct Truncate_request Truncate_request;
typedef struct Truncate_response Truncate_response;
typedef struct GetFlags_request GetFlags_request;
typedef struct GetFlags_response GetFlags_response;
typedef struct SetFlags_request SetFlags_request;
typedef struct SetFlags_response SetFlags_response;
typedef struct GetVmo_request GetVmo_request;
typedef struct GetVmo_response GetVmo_response;
typedef struct GetVmoAt_request GetVmoAt_request;
typedef struct GetVmoAt_response GetVmoAt_response;
typedef struct Open_request Open_request;
typedef struct Unlink_request Unlink_request;
typedef struct Unlink_response Unlink_response;
typedef struct ReadDirents_request ReadDirents_request;
typedef struct ReadDirents_response ReadDirents_response;
typedef struct Rewind_request Rewind_request;
typedef struct GetToken_request GetToken_request;
typedef struct GetToken_response GetToken_response;
typedef struct Rename_request Rename_request;
typedef struct Rename_response Rename_response;
typedef struct Link_request Link_request;
typedef struct Link_response Link_response;
typedef struct Service Service;
typedef struct File File;
typedef struct Directory Directory;
typedef struct Pipe Pipe;
typedef struct Vmofile Vmofile;
typedef struct Device Device;
typedef struct NodeAttributes NodeAttributes;
typedef struct ObjectInfo ObjectInfo;

// Extern declarations

extern const fidl_type_t Clone_request_coded_type;
extern const fidl_type_t Close_request_coded_type;
extern const fidl_type_t Close_response_coded_type;
extern const fidl_type_t ListInterfaces_request_coded_type;
extern const fidl_type_t ListInterfaces_response_coded_type;
extern const fidl_type_t Bind_request_coded_type;
extern const fidl_type_t Describe_request_coded_type;
extern const fidl_type_t Describe_response_coded_type;
extern const fidl_type_t OnOpen_response_coded_type;
extern const fidl_type_t Sync_request_coded_type;
extern const fidl_type_t Sync_response_coded_type;
extern const fidl_type_t GetAttr_request_coded_type;
extern const fidl_type_t GetAttr_response_coded_type;
extern const fidl_type_t SetAttr_request_coded_type;
extern const fidl_type_t SetAttr_response_coded_type;
extern const fidl_type_t Ioctl_request_coded_type;
extern const fidl_type_t Ioctl_response_coded_type;
extern const fidl_type_t Ioctl1H_request_coded_type;
extern const fidl_type_t Ioctl1H_response_coded_type;
extern const fidl_type_t Read_request_coded_type;
extern const fidl_type_t Read_response_coded_type;
extern const fidl_type_t ReadAt_request_coded_type;
extern const fidl_type_t ReadAt_response_coded_type;
extern const fidl_type_t Write_request_coded_type;
extern const fidl_type_t Write_response_coded_type;
extern const fidl_type_t WriteAt_request_coded_type;
extern const fidl_type_t WriteAt_response_coded_type;
extern const fidl_type_t Seek_request_coded_type;
extern const fidl_type_t Seek_response_coded_type;
extern const fidl_type_t Truncate_request_coded_type;
extern const fidl_type_t Truncate_response_coded_type;
extern const fidl_type_t GetFlags_request_coded_type;
extern const fidl_type_t GetFlags_response_coded_type;
extern const fidl_type_t SetFlags_request_coded_type;
extern const fidl_type_t SetFlags_response_coded_type;
extern const fidl_type_t GetVmo_request_coded_type;
extern const fidl_type_t GetVmo_response_coded_type;
extern const fidl_type_t GetVmoAt_request_coded_type;
extern const fidl_type_t GetVmoAt_response_coded_type;
extern const fidl_type_t Open_request_coded_type;
extern const fidl_type_t Unlink_request_coded_type;
extern const fidl_type_t Unlink_response_coded_type;
extern const fidl_type_t ReadDirents_request_coded_type;
extern const fidl_type_t ReadDirents_response_coded_type;
extern const fidl_type_t Rewind_request_coded_type;
extern const fidl_type_t GetToken_request_coded_type;
extern const fidl_type_t GetToken_response_coded_type;
extern const fidl_type_t Rename_request_coded_type;
extern const fidl_type_t Rename_response_coded_type;
extern const fidl_type_t Link_request_coded_type;
extern const fidl_type_t Link_response_coded_type;

// Declarations

































struct Clone_request {
    fidl_message_header_t header;
    zx_handle_t object;
};

struct Close_request {
    fidl_message_header_t header;
};

struct Close_response {
    fidl_message_header_t header;
    zx_status_t s;
};

struct ListInterfaces_request {
    fidl_message_header_t header;
};

struct ListInterfaces_response {
    fidl_message_header_t header;
    fidl_vector_t interfaces;
};

struct Bind_request {
    fidl_message_header_t header;
    fidl_string_t iface;
};

struct Describe_request {
    fidl_message_header_t header;
};

struct Describe_response {
    fidl_message_header_t header;
    ObjectInfo info;
};

struct OnOpen_response {
    fidl_message_header_t header;
    zx_status_t s;
    ObjectInfo* info;
};

struct Sync_request {
    fidl_message_header_t header;
};

struct Sync_response {
    fidl_message_header_t header;
    zx_status_t s;
};

struct GetAttr_request {
    fidl_message_header_t header;
};

struct GetAttr_response {
    fidl_message_header_t header;
    zx_status_t s;
    NodeAttributes attributes;
};

struct SetAttr_request {
    fidl_message_header_t header;
    uint32_t flags;
    NodeAttributes attributes;
};

struct SetAttr_response {
    fidl_message_header_t header;
    zx_status_t s;
};

struct Ioctl_request {
    fidl_message_header_t header;
    uint32_t opcode;
    uint64_t max_out;
    fidl_vector_t in;
};

struct Ioctl_response {
    fidl_message_header_t header;
    zx_status_t s;
    fidl_vector_t out;
};

struct Ioctl1H_request {
    fidl_message_header_t header;
    zx_handle_t h;
    uint32_t opcode;
    uint64_t max_out;
    fidl_vector_t in;
};

struct Ioctl1H_response {
    fidl_message_header_t header;
    zx_status_t s;
    fidl_vector_t out;
};

struct Read_request {
    fidl_message_header_t header;
    uint64_t count;
};

struct Read_response {
    fidl_message_header_t header;
    zx_status_t s;
    fidl_vector_t data;
};

struct ReadAt_request {
    fidl_message_header_t header;
    uint64_t count;
    uint64_t offset;
};

struct ReadAt_response {
    fidl_message_header_t header;
    zx_status_t s;
    fidl_vector_t data;
};

struct Write_request {
    fidl_message_header_t header;
    fidl_vector_t data;
};

struct Write_response {
    fidl_message_header_t header;
    zx_status_t s;
    uint64_t actual;
};

struct WriteAt_request {
    fidl_message_header_t header;
    fidl_vector_t data;
    uint64_t offset;
};

struct WriteAt_response {
    fidl_message_header_t header;
    zx_status_t s;
    uint64_t actual;
};

struct Seek_request {
    fidl_message_header_t header;
    int64_t offset;
    SeekFrom start;
};

struct Seek_response {
    fidl_message_header_t header;
    zx_status_t s;
    uint64_t offset;
};

struct Truncate_request {
    fidl_message_header_t header;
    uint64_t length;
};

struct Truncate_response {
    fidl_message_header_t header;
    zx_status_t s;
};

struct GetFlags_request {
    fidl_message_header_t header;
};

struct GetFlags_response {
    fidl_message_header_t header;
    zx_status_t s;
    uint32_t flags;
};

struct SetFlags_request {
    fidl_message_header_t header;
    uint32_t flags;
};

struct SetFlags_response {
    fidl_message_header_t header;
    zx_status_t s;
};

struct GetVmo_request {
    fidl_message_header_t header;
    uint32_t flags;
};

struct GetVmo_response {
    fidl_message_header_t header;
    zx_status_t s;
    zx_handle_t v;
};

struct GetVmoAt_request {
    fidl_message_header_t header;
    uint32_t flags;
    uint64_t offset;
    uint64_t length;
};

struct GetVmoAt_response {
    fidl_message_header_t header;
    zx_status_t s;
    zx_handle_t v;
};

struct Open_request {
    fidl_message_header_t header;
    uint32_t flags;
    uint32_t mode;
    fidl_string_t path;
    zx_handle_t object;
};

struct Unlink_request {
    fidl_message_header_t header;
    fidl_string_t path;
};

struct Unlink_response {
    fidl_message_header_t header;
    zx_status_t s;
};

struct ReadDirents_request {
    fidl_message_header_t header;
    uint64_t max_out;
};

struct ReadDirents_response {
    fidl_message_header_t header;
    zx_status_t s;
    fidl_vector_t dirents;
};

struct Rewind_request {
    fidl_message_header_t header;
};

struct GetToken_request {
    fidl_message_header_t header;
};

struct GetToken_response {
    fidl_message_header_t header;
    zx_status_t s;
    zx_handle_t token;
};

struct Rename_request {
    fidl_message_header_t header;
    fidl_string_t src;
    zx_handle_t dst_parent_token;
    fidl_string_t dst;
};

struct Rename_response {
    fidl_message_header_t header;
    zx_status_t s;
};

struct Link_request {
    fidl_message_header_t header;
    fidl_string_t src;
    zx_handle_t dst_parent_token;
    fidl_string_t dst;
};

struct Link_response {
    fidl_message_header_t header;
    zx_status_t s;
};

struct Service {
};

struct File {
    zx_handle_t e;
};

struct Directory {
};

struct Pipe {
    zx_handle_t s;
};

struct Vmofile {
    zx_handle_t v;
    uint64_t offset;
    uint64_t length;
};

struct Device {
    zx_handle_t e;
};

struct NodeAttributes {
    uint32_t mode;
    uint64_t id;
    uint64_t content_size;
    uint64_t storage_size;
    uint64_t link_count;
    uint64_t creation_time;
    uint64_t modification_time;
};

struct ObjectInfo {
    fidl_union_tag_t tag;
    union {
        Service service;
        File file;
        Directory directory;
        Pipe pipe;
        Vmofile vmofile;
        Device device;
    };
};
#define ObjectInfo_tag_service UINT32_C(0)
#define ObjectInfo_tag_file UINT32_C(1)
#define ObjectInfo_tag_directory UINT32_C(2)
#define ObjectInfo_tag_pipe UINT32_C(3)
#define ObjectInfo_tag_vmofile UINT32_C(4)
#define ObjectInfo_tag_device UINT32_C(5)

#if defined(__cplusplus)
}
#endif
