#pragma once
#define FF_DEFINED 80386 // makes the real ff.h skip itself
#include <cstdio>

typedef unsigned int UINT;
typedef unsigned char BYTE;
typedef char TCHAR;
struct FATFS
{
    int unused;
};
struct FIL
{
    FILE *fp;
};
typedef enum
{
    FR_OK = 0,
    FR_DISK_ERR
} FRESULT;

#define FA_WRITE 0x02
#define FA_OPEN_APPEND 0x30

inline FRESULT f_mount(FATFS *, const TCHAR *, BYTE) { return FR_OK; }
inline FRESULT f_open(FIL *f, const TCHAR *path, BYTE)
{
    f->fp = std::fopen(path, "ab");
    return f->fp ? FR_OK : FR_DISK_ERR;
}
inline FRESULT f_write(FIL *f, const void *buf, UINT n, UINT *bw)
{
    *bw = (UINT)std::fwrite(buf, 1, n, f->fp);
    return *bw == n ? FR_OK : FR_DISK_ERR;
}
inline FRESULT f_close(FIL *f)
{
    std::fclose(f->fp);
    return FR_OK;
}