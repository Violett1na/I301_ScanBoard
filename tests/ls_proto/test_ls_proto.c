/* test_ls_proto.c —— ls_proto 协议层宿主单测(TDD, 规范 8-1/8-2)
 * 构建/运行: sh tests/ls_proto/run.sh
 * 覆盖(边界优先):
 *   1. ls_crc16 已知向量;
 *   2. ls_pack / ls_unpack 常规往返
 *      (类型/命令字/长度/载荷逐字节);
 *   3. 错误分支:
 *      长度不足(-1)、包头错(-2)、
 *      data_len 不符(-3)、CRC 错(-4);
 *   4. 大小端互转 ls_swap_endian_16。 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "ls_proto.h"

/* 协议字节偏移(包头 13 字节固定长; 见 Proto/Src/ls_proto.c 的 ls_pack 写入顺序)
 *   [0,12]  包头字符串 LIGHTSPACE-XY
 *   [13,14] pck_len(大端)
 *   [15,16] version(大端)
 *   [17]    type
 *   [18]    cmd
 *   [19,20] data_len(大端)
 *   [21..]  data
 *   CRC 覆盖 [0, pck_len-3], 大端存于 [pck_len-2, pck_len-1] */
#define OFF_PCK_LEN   13
#define OFF_DATA_LEN  19
#define OFF_DATA      21

/* ---- 测试脚手架 ---- */
static int s_fail = 0;
static int s_pass = 0;

#define CHECK_EQ(got, want, msg)                                        \
    do                                                                  \
    {                                                                   \
        if ((long)(got) == (long)(want))                                \
        {                                                               \
            s_pass++;                                                   \
        }                                                               \
        else                                                            \
        {                                                               \
            s_fail++;                                                   \
            printf("FAIL(line %d): %s got=%ld want=%ld\n",              \
                   __LINE__, (msg), (long)(got), (long)(want));         \
        }                                                               \
    } while (0)

#define CHECK_TRUE(cond, msg) CHECK_EQ((cond) ? 1 : 0, 1, (msg))

/* ---- 本节无外部桩: ls_proto.c 零依赖 ---- */

/* 打包一个基础查询包, 供错误分支测试改写 */
static uint16_t build_query_packet(uint8_t *buf)
{
    ls_packet_t pkt;
    uint16_t len = 0;

    memset(&pkt, 0, sizeof(pkt));
    pkt.type     = (uint8_t)(LS_BASE_QUERY >> 8);
    pkt.cmd      = (uint8_t)(LS_BASE_QUERY & 0xFF);
    pkt.data_len = 1;
    pkt.data[0]  = 0xFF;
    pkt.pck_len  = LS_DATA_BASE_LEN + pkt.data_len;

    CHECK_EQ(ls_pack(&pkt, buf, &len), 0, "pack ret");
    return len;
}

static void test_crc16(void)
{
    const uint8_t v[] = { '1', '2', '3', '4', '5', '6', '7', '8', '9' };
    /* CRC-16/CCITT-FALSE: "123456789" -> 0x29B1 */
    CHECK_EQ(ls_crc16(v, sizeof(v)), 0x29B1, "crc16 known vector");
}

static void test_swap_endian(void)
{
    CHECK_EQ(ls_swap_endian_16(0x1234), 0x3412, "swap16");
    CHECK_EQ(ls_swap_endian_16(0x00FF), 0xFF00, "swap16 00ff");
    CHECK_EQ(ls_swap_endian_16(0xFFFF), 0xFFFF, "swap16 ffff");
    CHECK_EQ(ls_swap_endian_16(0x0000), 0x0000, "swap16 0000");
}

static void test_pack_unpack_roundtrip(void)
{
    uint8_t buf[256];
    uint16_t len = 0;
    ls_packet_t out;

    len = build_query_packet(buf);
    CHECK_EQ(len, LS_DATA_BASE_LEN + 1, "packed len");

    /* 包头字符串逐字节 */
    CHECK_TRUE(memcmp(buf, LS_HEADER_STR, LS_HEADER_LEN) == 0,
               "header str");
    /* pck_len 大端 */
    CHECK_EQ(buf[OFF_PCK_LEN], 0x00, "pck_len hi");
    CHECK_EQ(buf[OFF_PCK_LEN + 1], LS_DATA_BASE_LEN + 1, "pck_len lo");

    memset(&out, 0, sizeof(out));
    CHECK_EQ(ls_unpack(buf, len, &out), 0, "unpack ret");
    CHECK_EQ(out.pck_len, LS_DATA_BASE_LEN + 1, "unpacked pck_len");
    CHECK_EQ(out.version, LS_VERSION, "unpacked version");
    CHECK_EQ(out.type, (uint8_t)(LS_BASE_QUERY >> 8), "unpacked type");
    CHECK_EQ(out.cmd, (uint8_t)(LS_BASE_QUERY & 0xFF), "unpacked cmd");
    CHECK_EQ(out.data_len, 1, "unpacked data_len");
    CHECK_EQ(out.data[0], 0xFF, "unpacked payload");
}

static void test_unpack_errors(void)
{
    uint8_t buf[256];
    uint16_t len = build_query_packet(buf);
    ls_packet_t out;

    /* 长度不足 */
    CHECK_EQ(ls_unpack(buf, LS_DATA_BASE_LEN - 1, &out), -1, "too short");

    /* 包头错: 改首字节 */
    buf[0] = (uint8_t)(buf[0] ^ 0xFF);
    CHECK_EQ(ls_unpack(buf, len, &out), -2, "bad header");
    buf[0] = (uint8_t)(buf[0] ^ 0xFF);

    /* data_len 与 pck_len 不符: 把 data_len 字段改大 */
    buf[OFF_DATA_LEN] = 0x00;
    buf[OFF_DATA_LEN + 1] = 0x02;
    CHECK_EQ(ls_unpack(buf, len, &out), -3, "len mismatch");
    buf[OFF_DATA_LEN] = 0x00;
    buf[OFF_DATA_LEN + 1] = 0x01;

    /* CRC 错: 改载荷 */
    buf[OFF_DATA] = (uint8_t)(buf[OFF_DATA] ^ 0xFF);
    CHECK_EQ(ls_unpack(buf, len, &out), -4, "bad crc");
    buf[OFF_DATA] = (uint8_t)(buf[OFF_DATA] ^ 0xFF);

    /* NULL 入参 */
    CHECK_EQ(ls_unpack(NULL, len, &out), -1, "null in_buf");
    CHECK_EQ(ls_unpack(buf, len, NULL), -1, "null pkt");

    /* 修复后应恢复正常 */
    CHECK_EQ(ls_unpack(buf, len, &out), 0, "recovered");
}

int main(void)
{
    test_crc16();
    test_swap_endian();
    test_pack_unpack_roundtrip();
    test_unpack_errors();

    printf("ls_proto: %d passed, %d failed\n", s_pass, s_fail);
    return (s_fail == 0) ? 0 : 1;
}
