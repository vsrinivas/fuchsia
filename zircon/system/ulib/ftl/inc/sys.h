// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>
#include <targetos.h>

/***********************************************************************/
/* Symbol Definitions                                                  */
/***********************************************************************/
#define VERBOSE 1

// Removable Card/Device Types

#define DEV_FAT_VOL 1
#define DEV_NI 2
#define DEV_UART 3

/***********************************************************************/
/* Definitions related to reading/writing NVRAM memory.                */
/***********************************************************************/
void NvLoad(void);
int NvRead(const char* name, void* data, int type);
int NvReadBin(const char* name, void* data, int len);
int NvReadStr(const char* name, void* data, int maxlen);
int NvWrite(const char* name, const void* data, int type);
int NvWriteBin(const char* name, const void* data, int len);
int NvReadBinSize(const char* name);
#define NV_BYTE 0
#define NV_SHORT 1
#define NV_LONG 2
#define NV_STRING 3
#define NV_IP_ADDR 4
#define NV_ETH_ADDR 5
#define NV_ERR_LOG 6
#define NV_IP6_ADDR 7
#define NV_BINARY 8
int NvDelete(const char* name, int type);
void NvSave(void);
extern uint NvSize;
extern ui8* NvBuffer;

/***********************************************************************/
/* Definitions related to interactive I/O.                             */
/***********************************************************************/

// Shell Command List Entry
typedef struct {
    const char* cmd;
    void (*func)(char* cmd_line);
    const char* help;
} CmdEntry;

char* SysGetArg(char** cmd_linep);
int getString(char* str, int max_val);
int getCmdKey(void);
int getKey(void);
ui32 getInt(ui32 def_val, ui32 min_val, ui32 max_val);
ui32 getHex(ui32 def_val, ui32 min_val, ui32 max_val);
ui8 getDigit(ui8 value, ui8 min_val, ui8 max_val);

/***********************************************************************/
/* Miscellaneous Routines                                              */
/***********************************************************************/
int printSize(char* label, ui32 num_blks, ui32 blk_size);
void SysEditTOD(void);
void StartApp(int (*func)(void));
void SysWait50ms(void);
void spin_wait_us(ui32 us);
void spin_wait_ms(ui32 ms);
ui32 SysCounter(void);
ui64 SysCounter64(void);
void SysMon(void);
void SysShell(void);
void cliLabel(char* banner);
void AssertError(int line, const char* file);
void SyncCache(void);
void dCacheInval(const volatile void* base, long length);
void dCacheStore(const volatile void* base, long length);
void netWaitNi(int verbose);
int StrMatch(const char* str1, const char* str2);
void Spaces(int num);
int bspTick(void);
void bspReset(void);
void dbg_print(const char* format, ...);
void extra_free(ui32 start, ui32 end);
void free_clear(void* alloc_ptr_addr);
int testLoop(void);
void testPassed(void);
void DParse(void (*parse)(ui32 sample));
void DLog(const volatile ui32* reg_addr, ui32 mask);

// Cache Line-Aligned Allocation/Deallocation Routines
void* aalloc(size_t size);
void afree_clear(void* aaloc_ptr_addr);

/***********************************************************************/
/* Password Related Definitions                                        */
/***********************************************************************/

// Structure of entries in Application Access List.
typedef struct secret Secret;
struct secret {
    char* username;
    char* password;
    char* home_dir;
    ui16 uid;
    ui16 gid;
};

// Login Data
typedef struct {
    char* username;
    char* password;
    void* addr; // pointer to IP address
} SysLoginData;

extern Secret Secrets[];
extern int (*SysLoginReport)(uint event, SysLoginData* login_data);

void* SysPassword(const void* username);
Secret* SysLogin(SysLoginData* login_data);

// Login Events
#define SYSLOGIN_PASSED 0
#define SYSLOGIN_FAILED 1
#define SYSLOGIN_LOGOUT 2



/***********************************************************************/
/* CRC32 Related Definitions/Declaration                               */
/***********************************************************************/
extern const ui32 Crc32Tbl[256];
#define CRC32_START 0xFFFFFFFF // starting CRC bit string
#define CRC32_FINAL 0xDEBB20E3 // summed over data and CRC
#define CRC32_UPDATE(crc, c) ((crc >> 8) ^ Crc32Tbl[(ui8)(crc ^ c)])

/***********************************************************************/
/* Miscellaneous Data Declarations                                     */
/***********************************************************************/
extern char SysWarmBoot;         // TRUE iff executing RAM build
extern struct unknown SysHeapLo; // base address of heap
extern struct unk SysDataLo;     // base address of variables
extern ui32 SysHeapHi;           // ending address of heap
extern ui32 SysRamBase;          // base address of RAM
extern ui32 SysRamSize;          // size of RAM
extern char SysMenuRequested;    // TRUE iff user requests system menu
extern ui32 SysTtyBaud;
extern const char* AppName;
extern ui32 SysCountFreq;
extern int SysMonFlag;
extern char* MonPrompt;
extern char* ShellPrompt;
extern const char* StartAppName;
extern int EnableDST;
extern int UtcOffsetMinutes;

/***********************************************************************/
/* Fatal Error Codes (written to errno) and Log Structure              */
/***********************************************************************/
typedef enum sys_errs { SYS_ASSERT = 200, SYS_BUS_ERROR, SYS_STRAY_INTR } SysErrors;

// Fatal Error Log Format
typedef struct {
    int err_code;
    ui32 intr_lvl;
    ui32 pc;
    ui32 sp;
    time_t sec_cnt;
    ui32 lword1;
    ui32 lword2;
    char tname[8];
} FatErrType;
extern FatErrType SysFatErr;

#ifdef __cplusplus
}
#endif
