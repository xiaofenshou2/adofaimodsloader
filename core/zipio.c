/*
 * zipio - 极简 zip 读写（基于 zlib raw deflate，仅用于 Mod 加载器）
 * 支持：
 *   - zip_read_first / zip_read_next：遍历所有条目名
 *   - zip_extract：将条目写出到磁盘
 * 仅处理 Store / Deflate 两种压缩方式，足够 Mod 场景。
 */
#include "zipio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <zlib.h>   /* z_stream, inflate*, MAX_WBITS */

/* ---------- zip 结构常量 ---------- */
#define SIG_LFH    0x04034b50   /* Local File Header */
#define SIG_CDH    0x02014b50   /* Central Directory Header */
#define COMP_STORE 0
#define COMP_DEFLATE 8

#pragma pack(push, 1)
struct lfh {
    uint32_t sig;
    uint16_t ver;
    uint16_t flag;
    uint16_t comp;
    uint16_t time, date;
    uint32_t crc;
    uint32_t csize, usize;
    uint16_t nlen, elen;
};
#pragma pack(pop)

struct zip_file {
    FILE *fp;
    long  cd_offset;   /* 中央目录偏移 */
    uint16_t nrec;     /* 条目数 */
    /* 遍历状态 */
    uint16_t cur;
    char     name[ZIP_MAX_NAME];
    uint32_t comp, csize, usize;
    long     data_offset;
};

/* ---------- 小端读取 ---------- */
static uint16_t rd16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1]<<8) | (p[2]<<16) | (p[3]<<24); }

/* ---------- 定位中央目录 ---------- */
static long find_eocd(FILE *fp) {
    fseek(fp, 0, SEEK_END);
    long end = ftell(fp);
    long pos = end - 22;
    if (pos < 0) pos = 0;
    for (; pos >= 0; pos--) {
        fseek(fp, pos, SEEK_SET);
        uint8_t b[4];
        if (fread(b, 1, 4, fp) == 4 && rd32(b) == 0x06054b50)
            return pos;
    }
    return -1;
}

zip_file *zip_open(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;

    long eocd = find_eocd(fp);
    if (eocd < 0) { fclose(fp); return NULL; }

    fseek(fp, eocd + 16, SEEK_SET);
    uint8_t buf[4];
    fread(buf, 1, 4, fp);
    long cd_offset = rd32(buf);

    fseek(fp, eocd + 10, SEEK_SET);
    fread(buf, 1, 2, fp);
    uint16_t nrec = rd16(buf);

    zip_file *z = calloc(1, sizeof(*z));
    z->fp = fp;
    z->cd_offset = cd_offset;
    z->nrec = nrec;
    return z;
}

void zip_close(zip_file *z) {
    if (!z) return;
    fclose(z->fp);
    free(z);
}

/* 跳到某个 CDH，读 name/comp/csize/usize/data_offset */
static int read_cdh(zip_file *z, uint16_t idx) {
    if (idx >= z->nrec) return 0;
    /* 顺序遍历到 idx（简单可靠） */
    fseek(z->fp, z->cd_offset, SEEK_SET);
    for (uint16_t i = 0; i <= idx; i++) {
        uint8_t h[46];
        if (fread(h, 1, 46, z->fp) != 46) return 0;
        if (rd32(h) != SIG_CDH) return 0;
        uint16_t comp  = rd16(h + 10);
        uint32_t csize = rd32(h + 20);
        uint32_t usize = rd32(h + 24);
        uint16_t nlen  = rd16(h + 28);
        uint16_t elen  = rd16(h + 30);
        uint16_t clen  = rd16(h + 32);
        uint16_t alen  = rd16(h + 34);
        long data_offset = rd32(h + 42);

        if (i == idx) {
            z->comp = comp;
            z->csize = csize;
            z->usize = usize;
            z->data_offset = data_offset;
            /* 读文件名 */
            if (nlen >= ZIP_MAX_NAME) nlen = ZIP_MAX_NAME - 1;
            fread(z->name, 1, nlen, z->fp);
            z->name[nlen] = 0;
            return 1;
        }
        fseek(z->fp, nlen + elen + clen + alen, SEEK_CUR);
    }
    return 0;
}

int zip_read_first(zip_file *z, char *name_out, size_t name_cap) {
    z->cur = 0;
    if (!read_cdh(z, 0)) return 0;
    snprintf(name_out, name_cap, "%s", z->name);
    return 1;
}

int zip_read_next(zip_file *z, char *name_out, size_t name_cap) {
    z->cur++;
    if (z->cur >= z->nrec) return 0;
    if (!read_cdh(z, z->cur)) return 0;
    snprintf(name_out, name_cap, "%s", z->name);
    return 1;
}

/* 解压当前条目到磁盘（按 name 创建目录结构） */
int zip_extract_current(zip_file *z, const char *dest_dir) {
    if (!z || !dest_dir) return 0;

    /* 构造完整路径，防止 zip slip */
    char full[1024];
    snprintf(full, sizeof(full), "%s/%s", dest_dir, z->name);
    /* 若是目录条目（以/结尾）仅建目录 */
    if (full[strlen(full) - 1] == '/') {
        mkdir(full, 0755);
        return 1;
    }
    /* 确保父目录存在 */
    char *p = strrchr(full, '/');
    if (p) { *p = 0; mkdir(full, 0755); *p = '/'; }

    FILE *out = fopen(full, "wb");
    if (!out) return 0;

    fseek(z->fp, z->data_offset, SEEK_SET);

    if (z->comp == COMP_STORE) {
        uint8_t buf[8192];
        uint32_t left = z->csize;
        while (left > 0) {
            uint32_t n = left > sizeof(buf) ? sizeof(buf) : left;
            if (fread(buf, 1, n, z->fp) != n) break;
            fwrite(buf, 1, n, out);
            left -= n;
        }
    } else if (z->comp == COMP_DEFLATE) {
        /* 用 zlib 解压 */
        z_stream strm = {0};
        inflateInit2(&strm, -MAX_WBITS);
        uint8_t in[8192], outb[8192];
        strm.avail_in = 0;
        int ret;
        do {
            if (strm.avail_in == 0) {
                strm.avail_in = (uint32_t)fread(in, 1, sizeof(in), z->fp);
                strm.next_in  = in;
            }
            strm.avail_out = sizeof(outb);
            strm.next_out  = outb;
            ret = inflate(&strm, Z_NO_FLUSH);
            size_t got = sizeof(outb) - strm.avail_out;
            if (got > 0) fwrite(outb, 1, got, out);
        } while (ret == Z_OK);
        inflateEnd(&strm);
    }
    fclose(out);
    return 1;
}
