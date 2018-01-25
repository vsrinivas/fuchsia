// header file
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <fidl/coding.h>
#include <zircon/fidl.h>
#include <zircon/syscalls/object.h>
#include <zircon/types.h>

#if defined(__cplusplus)
extern "C" {
#endif


// Forward declarations

typedef uint32_t SeekOrigin;
#define SeekOrigin_Start UINT32_C(0)
#define SeekOrigin_Current UINT32_C(1)
#define SeekOrigin_End UINT32_C(2)

typedef struct ObjectCloneMsg ObjectCloneMsg;
typedef struct ObjectCloseMsg ObjectCloseMsg;
typedef struct ObjectCloseRsp ObjectCloseRsp;
typedef struct ObjectListInterfacesMsg ObjectListInterfacesMsg;
typedef struct ObjectListInterfacesRsp ObjectListInterfacesRsp;
typedef struct ObjectBindMsg ObjectBindMsg;
typedef struct ObjectDescribeMsg ObjectDescribeMsg;
typedef struct ObjectDescribeRsp ObjectDescribeRsp;
typedef struct ObjectOnOpenEvt ObjectOnOpenEvt;
typedef struct NodeSyncMsg NodeSyncMsg;
typedef struct NodeSyncRsp NodeSyncRsp;
typedef struct NodeGetAttrMsg NodeGetAttrMsg;
typedef struct NodeGetAttrRsp NodeGetAttrRsp;
typedef struct NodeSetAttrMsg NodeSetAttrMsg;
typedef struct NodeSetAttrRsp NodeSetAttrRsp;
typedef struct NodeIoctlMsg NodeIoctlMsg;
typedef struct NodeIoctlRsp NodeIoctlRsp;
typedef struct FileReadMsg FileReadMsg;
typedef struct FileReadRsp FileReadRsp;
typedef struct FileReadAtMsg FileReadAtMsg;
typedef struct FileReadAtRsp FileReadAtRsp;
typedef struct FileWriteMsg FileWriteMsg;
typedef struct FileWriteRsp FileWriteRsp;
typedef struct FileWriteAtMsg FileWriteAtMsg;
typedef struct FileWriteAtRsp FileWriteAtRsp;
typedef struct FileSeekMsg FileSeekMsg;
typedef struct FileSeekRsp FileSeekRsp;
typedef struct FileTruncateMsg FileTruncateMsg;
typedef struct FileTruncateRsp FileTruncateRsp;
typedef struct FileGetFlagsMsg FileGetFlagsMsg;
typedef struct FileGetFlagsRsp FileGetFlagsRsp;
typedef struct FileSetFlagsMsg FileSetFlagsMsg;
typedef struct FileSetFlagsRsp FileSetFlagsRsp;
typedef struct FileGetVmoMsg FileGetVmoMsg;
typedef struct FileGetVmoRsp FileGetVmoRsp;
typedef struct FileGetVmoAtMsg FileGetVmoAtMsg;
typedef struct FileGetVmoAtRsp FileGetVmoAtRsp;
typedef struct DirectoryOpenMsg DirectoryOpenMsg;
typedef struct DirectoryUnlinkMsg DirectoryUnlinkMsg;
typedef struct DirectoryUnlinkRsp DirectoryUnlinkRsp;
typedef struct DirectoryReadDirentsMsg DirectoryReadDirentsMsg;
typedef struct DirectoryReadDirentsRsp DirectoryReadDirentsRsp;
typedef struct DirectoryRewindMsg DirectoryRewindMsg;
typedef struct DirectoryRewindRsp DirectoryRewindRsp;
typedef struct DirectoryGetTokenMsg DirectoryGetTokenMsg;
typedef struct DirectoryGetTokenRsp DirectoryGetTokenRsp;
typedef struct DirectoryRenameMsg DirectoryRenameMsg;
typedef struct DirectoryRenameRsp DirectoryRenameRsp;
typedef struct DirectoryLinkMsg DirectoryLinkMsg;
typedef struct DirectoryLinkRsp DirectoryLinkRsp;
typedef struct Service Service;
typedef struct File File;
typedef struct Directory Directory;
typedef struct Pipe Pipe;
typedef struct Vmofile Vmofile;
typedef struct Device Device;
typedef struct NodeAttributes NodeAttributes;
typedef struct ObjectInfo ObjectInfo;

// Extern declarations

extern const fidl_type_t ObjectCloneReqCoded;
extern const fidl_type_t ObjectCloseReqCoded;
extern const fidl_type_t ObjectCloseRspCoded;
extern const fidl_type_t ObjectListInterfacesReqCoded;
extern const fidl_type_t ObjectListInterfacesRspCoded;
extern const fidl_type_t ObjectBindReqCoded;
extern const fidl_type_t ObjectDescribeReqCoded;
extern const fidl_type_t ObjectDescribeRspCoded;
extern const fidl_type_t ObjectOnOpenEvtCoded;
extern const fidl_type_t NodeSyncReqCoded;
extern const fidl_type_t NodeSyncRspCoded;
extern const fidl_type_t NodeGetAttrReqCoded;
extern const fidl_type_t NodeGetAttrRspCoded;
extern const fidl_type_t NodeSetAttrReqCoded;
extern const fidl_type_t NodeSetAttrRspCoded;
extern const fidl_type_t NodeIoctlReqCoded;
extern const fidl_type_t NodeIoctlRspCoded;
extern const fidl_type_t FileReadReqCoded;
extern const fidl_type_t FileReadRspCoded;
extern const fidl_type_t FileReadAtReqCoded;
extern const fidl_type_t FileReadAtRspCoded;
extern const fidl_type_t FileWriteReqCoded;
extern const fidl_type_t FileWriteRspCoded;
extern const fidl_type_t FileWriteAtReqCoded;
extern const fidl_type_t FileWriteAtRspCoded;
extern const fidl_type_t FileSeekReqCoded;
extern const fidl_type_t FileSeekRspCoded;
extern const fidl_type_t FileTruncateReqCoded;
extern const fidl_type_t FileTruncateRspCoded;
extern const fidl_type_t FileGetFlagsReqCoded;
extern const fidl_type_t FileGetFlagsRspCoded;
extern const fidl_type_t FileSetFlagsReqCoded;
extern const fidl_type_t FileSetFlagsRspCoded;
extern const fidl_type_t FileGetVmoReqCoded;
extern const fidl_type_t FileGetVmoRspCoded;
extern const fidl_type_t FileGetVmoAtReqCoded;
extern const fidl_type_t FileGetVmoAtRspCoded;
extern const fidl_type_t DirectoryOpenReqCoded;
extern const fidl_type_t DirectoryUnlinkReqCoded;
extern const fidl_type_t DirectoryUnlinkRspCoded;
extern const fidl_type_t DirectoryReadDirentsReqCoded;
extern const fidl_type_t DirectoryReadDirentsRspCoded;
extern const fidl_type_t DirectoryRewindReqCoded;
extern const fidl_type_t DirectoryRewindRspCoded;
extern const fidl_type_t DirectoryGetTokenReqCoded;
extern const fidl_type_t DirectoryGetTokenRspCoded;
extern const fidl_type_t DirectoryRenameReqCoded;
extern const fidl_type_t DirectoryRenameRspCoded;
extern const fidl_type_t DirectoryLinkReqCoded;
extern const fidl_type_t DirectoryLinkRspCoded;

// Declarations

struct Service {
    uint8_t reserved; // Manually inserted to cope with zero-size struct issues
};

struct File {
    zx_handle_t e;
};

struct Directory {
    uint8_t reserved; // Manually inserted to cope with zero-size struct issues
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

































struct ObjectCloneMsg {
    fidl_message_header_t hdr;
    uint32_t flags;
    zx_handle_t object;
};

struct ObjectCloseMsg {
    fidl_message_header_t hdr;
};

struct ObjectCloseRsp {
    fidl_message_header_t hdr;
    zx_status_t s;
};

struct ObjectListInterfacesMsg {
    fidl_message_header_t hdr;
};

struct ObjectListInterfacesRsp {
    fidl_message_header_t hdr;
    fidl_vector_t interfaces;
};

struct ObjectBindMsg {
    fidl_message_header_t hdr;
    fidl_string_t iface;
};

struct ObjectDescribeMsg {
    fidl_message_header_t hdr;
};

struct ObjectDescribeRsp {
    fidl_message_header_t hdr;
    ObjectInfo info;
};

struct ObjectOnOpenEvt {
    fidl_message_header_t hdr;
    zx_status_t s;
    ObjectInfo* info;
};

struct NodeSyncMsg {
    fidl_message_header_t hdr;
};

struct NodeSyncRsp {
    fidl_message_header_t hdr;
    zx_status_t s;
};

struct NodeGetAttrMsg {
    fidl_message_header_t hdr;
};

struct NodeGetAttrRsp {
    fidl_message_header_t hdr;
    zx_status_t s;
    NodeAttributes attributes;
};

struct NodeSetAttrMsg {
    fidl_message_header_t hdr;
    uint32_t flags;
    NodeAttributes attributes;
};

struct NodeSetAttrRsp {
    fidl_message_header_t hdr;
    zx_status_t s;
};

struct NodeIoctlMsg {
    fidl_message_header_t hdr;
    uint32_t opcode;
    uint64_t max_out;
    fidl_vector_t handles;
    fidl_vector_t in;
};

struct NodeIoctlRsp {
    fidl_message_header_t hdr;
    zx_status_t s;
    fidl_vector_t handles;
    fidl_vector_t out;
};

struct FileReadMsg {
    fidl_message_header_t hdr;
    uint64_t count;
};

struct FileReadRsp {
    fidl_message_header_t hdr;
    zx_status_t s;
    fidl_vector_t data;
};

struct FileReadAtMsg {
    fidl_message_header_t hdr;
    uint64_t count;
    uint64_t offset;
};

struct FileReadAtRsp {
    fidl_message_header_t hdr;
    zx_status_t s;
    fidl_vector_t data;
};

struct FileWriteMsg {
    fidl_message_header_t hdr;
    fidl_vector_t data;
};

struct FileWriteRsp {
    fidl_message_header_t hdr;
    zx_status_t s;
    uint64_t actual;
};

struct FileWriteAtMsg {
    fidl_message_header_t hdr;
    fidl_vector_t data;
    uint64_t offset;
};

struct FileWriteAtRsp {
    fidl_message_header_t hdr;
    zx_status_t s;
    uint64_t actual;
};

struct FileSeekMsg {
    fidl_message_header_t hdr;
    int64_t offset;
    SeekOrigin start;
};

struct FileSeekRsp {
    fidl_message_header_t hdr;
    zx_status_t s;
    uint64_t offset;
};

struct FileTruncateMsg {
    fidl_message_header_t hdr;
    uint64_t length;
};

struct FileTruncateRsp {
    fidl_message_header_t hdr;
    zx_status_t s;
};

struct FileGetFlagsMsg {
    fidl_message_header_t hdr;
};

struct FileGetFlagsRsp {
    fidl_message_header_t hdr;
    zx_status_t s;
    uint32_t flags;
};

struct FileSetFlagsMsg {
    fidl_message_header_t hdr;
    uint32_t flags;
};

struct FileSetFlagsRsp {
    fidl_message_header_t hdr;
    zx_status_t s;
};

struct FileGetVmoMsg {
    fidl_message_header_t hdr;
    uint32_t flags;
};

struct FileGetVmoRsp {
    fidl_message_header_t hdr;
    zx_status_t s;
    zx_handle_t v;
};

struct FileGetVmoAtMsg {
    fidl_message_header_t hdr;
    uint32_t flags;
    uint64_t offset;
    uint64_t length;
};

struct FileGetVmoAtRsp {
    fidl_message_header_t hdr;
    zx_status_t s;
    zx_handle_t v;
};

struct DirectoryOpenMsg {
    fidl_message_header_t hdr;
    uint32_t flags;
    uint32_t mode;
    fidl_string_t path;
    zx_handle_t object;
};

struct DirectoryUnlinkMsg {
    fidl_message_header_t hdr;
    fidl_string_t path;
};

struct DirectoryUnlinkRsp {
    fidl_message_header_t hdr;
    zx_status_t s;
};

struct DirectoryReadDirentsMsg {
    fidl_message_header_t hdr;
    uint64_t max_out;
};

struct DirectoryReadDirentsRsp {
    fidl_message_header_t hdr;
    zx_status_t s;
    fidl_vector_t dirents;
};

struct DirectoryRewindMsg {
    fidl_message_header_t hdr;
};

struct DirectoryRewindRsp {
    fidl_message_header_t hdr;
    zx_status_t s;
};

struct DirectoryGetTokenMsg {
    fidl_message_header_t hdr;
};

struct DirectoryGetTokenRsp {
    fidl_message_header_t hdr;
    zx_status_t s;
    zx_handle_t token;
};

struct DirectoryRenameMsg {
    fidl_message_header_t hdr;
    fidl_string_t src;
    zx_handle_t dst_parent_token;
    fidl_string_t dst;
};

struct DirectoryRenameRsp {
    fidl_message_header_t hdr;
    zx_status_t s;
};

struct DirectoryLinkMsg {
    fidl_message_header_t hdr;
    fidl_string_t src;
    zx_handle_t dst_parent_token;
    fidl_string_t dst;
};

struct DirectoryLinkRsp {
    fidl_message_header_t hdr;
    zx_status_t s;
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
