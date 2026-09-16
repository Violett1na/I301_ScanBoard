/* test_ls_proto.c —— ls_proto 协议层宿主单测(TDD, 规范 8-1/8-2)
 * 构建/运行: sh tests/ls_proto/run.sh
 * 覆盖(边界优先):
 *   1. ls_crc16 已知向量;
 *   2. ls_pack / ls_unpack 常规往返
 *      (类型/命令字/长度/载荷逐字节);
 *   3. 错误分支:
 *      长度不足(-1)、包头错(-2)、
 *      data_len 不符(-3)、CRC 错(-4);
 *   4. 大小端互转 ls_swap_endian_16;
 *   5. 发送层(trans)四个新发包函数: 逐字节校验包头/长度/载荷,
 *      含 NULL 指针拒发与补偿值大端序。 */
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "ls_proto.h"
#include "ls_proto_trans.h"   /* 发送层: ls_trans_callbacks_t / ls_ctrl_* */
#include "ls_proto_receive.h" /* 接收层: ls_receive_callbacks_t / ls_parse */

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

static void test_profile_types(void)
{
    /* 协议常量取值: 一旦被无意改动, 两仓镜像会静默错位 */
    CHECK_EQ(LS_BASE_REPLY_ALL,     0x0104, "cmd reply_all");
    CHECK_EQ(LS_CTRL_SET_PROFILE,   0x0305, "cmd set_profile");
    CHECK_EQ(LS_CTRL_GET_ALL,       0x0306, "cmd get_all");
    CHECK_EQ(LS_CTRL_FORCE_PROFILE, 0x0307, "cmd force_profile");
    CHECK_EQ(LS_PROFILE_NONE,       0xFF,   "profile none");
    CHECK_EQ(LS_PROFILE_NUM,        3,      "profile num");

    /* 结构体尺寸: 决定两仓字节流一致性, 必须逐字节对上 */
    CHECK_EQ(sizeof(ls_profile_t),           10, "sizeof ls_profile_t");
    CHECK_EQ(sizeof(ls_ctrl_set_profile_t),  11,
             "sizeof ls_ctrl_set_profile_t");
    CHECK_EQ(sizeof(ls_ctrl_get_all_t),       1, "sizeof ls_ctrl_get_all_t");
    CHECK_EQ(sizeof(ls_ctrl_force_t),         1, "sizeof ls_ctrl_force_t");
    CHECK_EQ(sizeof(ls_all_reply_t),         36, "sizeof ls_all_reply_t");

    /* 字段偏移: radc 在前、comp 在后, 不得因对齐改变 */
    CHECK_EQ(offsetof(ls_profile_t, radc),   0, "offset radc");
    CHECK_EQ(offsetof(ls_profile_t, comp_x), 6, "offset comp_x");
    CHECK_EQ(offsetof(ls_profile_t, comp_y), 8, "offset comp_y");
}

/* ---- 发送捕获桩: 挂到 trans 层 send 回调上 ---- */

/* 最近一帧待发送字节流; s_tx_count 记录累计发送次数 */
static uint8_t  s_tx_buf[1024];
static uint16_t s_tx_len   = 0;
static int      s_tx_count = 0;

/* 捕获 send 回调的字节流, 供逐字节校验 */
static void stub_send(uint8_t *buf, uint16_t len)
{
    if ((size_t)len > sizeof(s_tx_buf))
    {
        len = (uint16_t)sizeof(s_tx_buf);
    }
    memcpy(s_tx_buf, buf, len);
    s_tx_len = len;
    s_tx_count++;
}

/* 全量回复的数据源桩 */
static ls_all_reply_t s_stub_all;

/* 整份拷贝预置内容, 不做任何加工 */
static void stub_get_device_info_all(ls_all_reply_t *reply)
{
    *reply = s_stub_all;
}

/* 清空捕获状态, 使各用例的计数断言相互独立 */
static void reset_tx(void)
{
    s_tx_len   = 0;
    s_tx_count = 0;
    memset(s_tx_buf, 0, sizeof(s_tx_buf));
}

/* 从捕获到的字节流中解出包 */
static int tx_unpack(ls_packet_t *out)
{
    return ls_unpack(s_tx_buf, s_tx_len, out);
}

/* 0x0305: 整包写一套配置——头部单字节原样, 补偿值大端 */
static void test_send_set_profile(void)
{
    static const ls_trans_callbacks_t cbs = { .send = stub_send };
    ls_trans_init_callbacks(&cbs);

    ls_profile_t p;

    memset(&p, 0, sizeof(p));
    p.radc[0] = 11;
    p.radc[1] = 22;
    p.radc[2] = 33;
    p.radc[3] = 44;
    p.radc[4] = 55;
    p.radc[5] = 66;
    p.comp_x  = -80;
    p.comp_y  = 1234;

    reset_tx();
    CHECK_EQ(ls_ctrl_set_profile(2, &p), 0, "set_profile ret");
    CHECK_EQ(s_tx_count, 1, "sent once");

    ls_packet_t out;

    memset(&out, 0, sizeof(out));
    CHECK_EQ(tx_unpack(&out), 0, "tx unpack");
    CHECK_EQ(out.type, (uint8_t)(LS_CTRL_SET_PROFILE >> 8), "tx type");
    CHECK_EQ(out.cmd,  (uint8_t)(LS_CTRL_SET_PROFILE & 0xFF), "tx cmd");
    CHECK_EQ(out.data_len, sizeof(ls_ctrl_set_profile_t), "tx data_len");
    CHECK_EQ(out.data[0], 2, "tx profile index");

    /* radc 单字节原样 */
    CHECK_EQ(out.data[1], 11, "tx radc x1");
    CHECK_EQ(out.data[6], 66, "tx radc y3");

    /* comp 大端: -80 = 0xFFB0 -> 字节序 FF B0 */
    CHECK_EQ(out.data[7],  0xFF, "tx comp_x hi");
    CHECK_EQ(out.data[8],  0xB0, "tx comp_x lo");
    /* 1234 = 0x04D2 -> 04 D2 */
    CHECK_EQ(out.data[9],  0x04, "tx comp_y hi");
    CHECK_EQ(out.data[10], 0xD2, "tx comp_y lo");

    /* NULL 指针拒发 */
    reset_tx();
    CHECK_EQ(ls_ctrl_set_profile(0, NULL), -1, "set_profile null");
    CHECK_EQ(s_tx_count, 0, "no send on null");
}

/* 0x0306 请求全量回读 / 0x0307 强制套与解除: 载荷均为 1 字节 */
static void test_send_get_all_and_force(void)
{
    static const ls_trans_callbacks_t cbs = { .send = stub_send };
    ls_trans_init_callbacks(&cbs);

    ls_packet_t out;

    reset_tx();
    CHECK_EQ(ls_ctrl_get_all(), 0, "get_all ret");
    memset(&out, 0, sizeof(out));
    CHECK_EQ(tx_unpack(&out), 0, "get_all unpack");
    CHECK_EQ(out.cmd, (uint8_t)(LS_CTRL_GET_ALL & 0xFF), "get_all cmd");
    CHECK_EQ(out.data_len, 1, "get_all data_len");
    CHECK_EQ(out.data[0], 0xFF, "get_all payload");

    reset_tx();
    CHECK_EQ(ls_ctrl_force_profile(1), 0, "force ret");
    memset(&out, 0, sizeof(out));
    CHECK_EQ(tx_unpack(&out), 0, "force unpack");
    CHECK_EQ(out.cmd, (uint8_t)(LS_CTRL_FORCE_PROFILE & 0xFF), "force cmd");
    CHECK_EQ(out.data[0], 1, "force profile");

    reset_tx();
    CHECK_EQ(ls_ctrl_force_profile(LS_PROFILE_NONE), 0, "force release ret");
    memset(&out, 0, sizeof(out));
    CHECK_EQ(tx_unpack(&out), 0, "force release unpack");
    CHECK_EQ(out.data[0], 0xFF, "force release payload");
}

/* 0x0104: 全量回复 36B——头部字段与各套补偿值大端, 各套按声明序排列 */
static void test_send_base_reply_all(void)
{
    static const ls_trans_callbacks_t cbs = {
        .send                = stub_send,
        .get_device_info_all = stub_get_device_info_all,
    };
    ls_trans_init_callbacks(&cbs);

    ls_packet_t out;

    memset(&s_stub_all, 0, sizeof(s_stub_all));
    s_stub_all.device_id      = 0x1234;
    s_stub_all.device_version = 0x5678;
    s_stub_all.active_profile = 1;
    s_stub_all.forced_profile = LS_PROFILE_NONE;

    s_stub_all.profiles[0].radc[0] = 40;
    s_stub_all.profiles[2].radc[5] = 45;
    s_stub_all.profiles[1].comp_x  = -80;

    reset_tx();
    CHECK_EQ(ls_base_reply_all(), 0, "reply_all ret");

    memset(&out, 0, sizeof(out));
    CHECK_EQ(tx_unpack(&out), 0, "reply_all unpack");
    CHECK_EQ(out.type, (uint8_t)(LS_BASE_REPLY_ALL >> 8), "reply_all type");
    CHECK_EQ(out.cmd,  (uint8_t)(LS_BASE_REPLY_ALL & 0xFF), "reply_all cmd");
    CHECK_EQ(out.data_len, sizeof(ls_all_reply_t), "reply_all data_len");

    /* 头部字段大端 */
    CHECK_EQ(out.data[0], 0x12, "id hi");
    CHECK_EQ(out.data[1], 0x34, "id lo");
    CHECK_EQ(out.data[2], 0x56, "ver hi");
    CHECK_EQ(out.data[3], 0x78, "ver lo");
    CHECK_EQ(out.data[4], 1,    "active");
    CHECK_EQ(out.data[5], 0xFF, "forced");

    /* profiles[0].radc[0] 在偏移 6+0 */
    CHECK_EQ(out.data[6], 40, "p0 radc x1");
    /* profiles[2].radc[5] 在偏移 6 + 2*10 + 5 */
    CHECK_EQ(out.data[31], 45, "p2 radc y3");
    /* profiles[1].comp_x = -80 -> 0xFFB0, 偏移 6 + 1*10 + 6 */
    CHECK_EQ(out.data[22], 0xFF, "p1 comp_x hi");
    CHECK_EQ(out.data[23], 0xB0, "p1 comp_x lo");
}

/* ---- 接收回调桩 ---- */
static ls_ctrl_set_profile_t s_rx_set_profile;  /* 最近一次收到的写套载荷 */
static int  s_rx_set_profile_n   = 0;           /* 写套回调调用次数 */
static int  s_rx_set_profile_ret = 0;           /* 写套回调返回值桩 */

static int stub_ctrl_set_profile(const ls_ctrl_set_profile_t *m)
{
    s_rx_set_profile = *m;
    s_rx_set_profile_n++;
    return s_rx_set_profile_ret;
}

static int s_rx_get_all_n = 0;  /* 全量回读请求回调调用次数 */
static int stub_ctrl_get_all(void)
{
    s_rx_get_all_n++;
    return 0;
}

static ls_ctrl_force_t s_rx_force;  /* 最近一次收到的强制套载荷 */
static int s_rx_force_n = 0;        /* 强制套回调调用次数 */
static int stub_ctrl_force_profile(const ls_ctrl_force_t *m)
{
    s_rx_force = *m;
    s_rx_force_n++;
    return 0;
}

static ls_all_reply_t s_rx_all;  /* 最近一次收到的全量回包 */
static int s_rx_all_n = 0;       /* 全量回包回调调用次数 */
static void stub_on_reply_all(const ls_all_reply_t *r)
{
    s_rx_all = *r;
    s_rx_all_n++;
}

/* 造一个可被 ls_parse 解析的整包 */
static uint16_t make_packet(uint16_t typeCmd, const void *payload,
                            uint16_t payload_len, uint8_t *buf)
{
    ls_packet_t pkt;    /* 待打包的包体：类型/命令字/载荷 */
    uint16_t len = 0;   /* 打包后的字节流长度 */

    memset(&pkt, 0, sizeof(pkt));
    pkt.type     = (uint8_t)(typeCmd >> 8);
    pkt.cmd      = (uint8_t)(typeCmd & 0xFF);
    pkt.data_len = payload_len;
    if (payload_len > 0U)
    {
        memcpy(pkt.data, payload, payload_len);
    }
    pkt.pck_len  = LS_DATA_BASE_LEN + payload_len;

    CHECK_EQ(ls_pack(&pkt, buf, &len), 0, "make_packet pack");
    return len;
}

/* 接收层 0x0305: 载荷解析(含大端补偿值)、回调下发与控制应答 */
static void test_handle_set_profile(void)
{
    ls_packet_t pkt;   /* 声明于外侧会被下面复用, 便于逐分支校验 */

    /* 先初始化发送桩, 否则 ls_ctrl_reply 无法发出 */
    static const ls_trans_callbacks_t t_cbs = { .send = stub_send };
    ls_trans_init_callbacks(&t_cbs);

    static const ls_receive_callbacks_t r_cbs = {
        .ctrl_set_profile = stub_ctrl_set_profile,
    };
    ls_receiver_init_callbacks(&r_cbs);

    /* 造一个设备侧会收到的写配置包: 载荷为【大端】comp */
    ls_ctrl_set_profile_t in;   /* 待发载荷：套号 2 + 码值 + 大端补偿值 */

    memset(&in, 0, sizeof(in));
    in.profile  = 2;
    in.data.radc[0] = 11;
    in.data.radc[5] = 66;
    in.data.comp_x  = (int16_t)ls_swap_endian_16((uint16_t)(int16_t)(-80));
    in.data.comp_y  = (int16_t)ls_swap_endian_16((uint16_t)(int16_t)1234);

    uint8_t buf[256];   /* 打包后的待解析字节流 */
    uint16_t len = make_packet(LS_CTRL_SET_PROFILE, &in, sizeof(in), buf);

    s_rx_set_profile_n = 0;
    reset_tx();
    CHECK_EQ(ls_parse(&pkt, buf, len), 0, "parse set_profile");
    CHECK_EQ(s_rx_set_profile_n, 1, "callback invoked");
    CHECK_EQ(s_rx_set_profile.profile, 2, "parsed profile");
    CHECK_EQ(s_rx_set_profile.data.radc[0], 11, "parsed radc x1");
    CHECK_EQ(s_rx_set_profile.data.radc[5], 66, "parsed radc y3");
    CHECK_EQ(s_rx_set_profile.data.comp_x, -80,  "parsed comp_x (endian)");
    CHECK_EQ(s_rx_set_profile.data.comp_y, 1234, "parsed comp_y (endian)");
    /* 回送控制应答 */
    CHECK_EQ(s_tx_count, 1, "acked once");

    /* 回调返回非 0 → 失败码透传, 但应答帧仍须发出(契约核心) */
    s_rx_set_profile_ret = -3;  /* 桩置非 0: 模拟应用层处理失败 */
    reset_tx();
    CHECK_EQ(ls_parse(&pkt, buf, len), -3, "callback failure propagated");
    CHECK_EQ(s_tx_count, 1, "acked even on callback failure");
    s_rx_set_profile_ret = 0;   /* 复位, 避免影响后续用例 */

    /* 长度不符 → 拒收且不回调 */
    s_rx_set_profile_n = 0;
    len = make_packet(LS_CTRL_SET_PROFILE, &in, sizeof(in) - 1U, buf);
    CHECK_EQ(ls_parse(&pkt, buf, len), -1, "short payload rejected");
    CHECK_EQ(s_rx_set_profile_n, 0, "no callback on bad len");
}

/* 接收层 0x0306/0x0307: 全量回读(只发一帧全量回复, 无 0x0301 应答)
 * 与强制套/解除 */
static void test_handle_get_all_and_force(void)
{
    static const ls_trans_callbacks_t t_cbs = {
        .send                = stub_send,
        .get_device_info_all = stub_get_device_info_all,
    };
    ls_trans_init_callbacks(&t_cbs);

    static const ls_receive_callbacks_t r_cbs = {
        .ctrl_get_all      = stub_ctrl_get_all,
        .ctrl_force_profile = stub_ctrl_force_profile,
    };
    ls_receiver_init_callbacks(&r_cbs);

    ls_packet_t pkt;    /* 复用：解析入包与解包发出的帧 */
    uint8_t buf[256];   /* 打包后的待解析字节流 */
    uint16_t len;       /* 当前帧长度 */

    /* 0x0306 → 只发一帧, 且该帧即全量回复(spec §4.5 例外: 不回
     * 0x0301 应答; 发送通路忙即拒发, 同趟发两帧第二帧必被丢弃) */
    ls_ctrl_get_all_t req;  /* 回读请求载荷：占位 0xFF */

    req.req = 0xFF;
    memset(&s_stub_all, 0, sizeof(s_stub_all));
    s_stub_all.active_profile = 1;
    s_stub_all.profiles[1].radc[0] = 120;

    len = make_packet(LS_CTRL_GET_ALL, &req, sizeof(req), buf);
    s_rx_get_all_n = 0;
    reset_tx();
    CHECK_EQ(ls_parse(&pkt, buf, len), 0, "parse get_all");
    CHECK_EQ(s_rx_get_all_n, 1, "get_all callback");
    CHECK_EQ(s_tx_count, 1, "single frame, no ack");
    memset(&pkt, 0, sizeof(pkt));
    CHECK_EQ(tx_unpack(&pkt), 0, "sole frame unpack");
    CHECK_EQ(pkt.cmd, (uint8_t)(LS_BASE_REPLY_ALL & 0xFF),
             "sole frame is reply_all");
    CHECK_EQ(pkt.data_len, sizeof(ls_all_reply_t), "reply_all data_len");
    CHECK_EQ(pkt.data[4], 1, "active in reply_all");
    CHECK_EQ(pkt.data[16], 120, "p1 radc x1 in reply_all");

    /* 长度不符 → 拒收且不回调(0x0306 载荷固定 1 字节) */
    len = make_packet(LS_CTRL_GET_ALL, &req, sizeof(req) - 1U, buf);
    s_rx_get_all_n = 0;
    reset_tx();
    CHECK_EQ(ls_parse(&pkt, buf, len), -1, "short get_all rejected");
    CHECK_EQ(s_rx_get_all_n, 0, "no get_all callback on bad len");

    /* 0x0307 强制 */
    ls_ctrl_force_t fo;     /* 强制套载荷：目标套号 */

    fo.profile = 2;
    len = make_packet(LS_CTRL_FORCE_PROFILE, &fo, sizeof(fo), buf);
    s_rx_force_n = 0;
    CHECK_EQ(ls_parse(&pkt, buf, len), 0, "parse force");
    CHECK_EQ(s_rx_force_n, 1, "force callback");
    CHECK_EQ(s_rx_force.profile, 2, "parsed force profile");

    /* 0x0307 解除 */
    fo.profile = LS_PROFILE_NONE;
    len = make_packet(LS_CTRL_FORCE_PROFILE, &fo, sizeof(fo), buf);
    CHECK_EQ(ls_parse(&pkt, buf, len), 0, "parse force release");
    CHECK_EQ(s_rx_force.profile, LS_PROFILE_NONE, "parsed release");
}

/* 接收层 0x0104: 全量回包解出设备信息/工况/三套配置(补偿值大端还原) */
static void test_handle_base_reply_all(void)
{
    static const ls_trans_callbacks_t t_cbs = { .send = stub_send };
    ls_trans_init_callbacks(&t_cbs);

    static const ls_receive_callbacks_t r_cbs = {
        .on_reply_all = stub_on_reply_all,
    };
    ls_receiver_init_callbacks(&r_cbs);

    /* 主机侧收到的是【大端】载荷 */
    ls_all_reply_t in;  /* 待发全量回包：大端设备信息与各套配置 */

    memset(&in, 0, sizeof(in));
    in.device_id      = ls_swap_endian_16(0x1234);
    in.device_version = ls_swap_endian_16(0x5678);
    in.active_profile = 1;
    in.forced_profile = 2;
    in.profiles[0].radc[0] = 40;
    in.profiles[2].comp_y  =
        (int16_t)ls_swap_endian_16((uint16_t)(int16_t)(-95));

    ls_packet_t pkt;    /* 解析结果包 */
    uint8_t buf[256];   /* 打包后的待解析字节流 */
    uint16_t len = make_packet(LS_BASE_REPLY_ALL, &in, sizeof(in), buf);

    s_rx_all_n = 0;
    CHECK_EQ(ls_parse(&pkt, buf, len), 0, "parse reply_all");
    CHECK_EQ(s_rx_all_n, 1, "on_reply_all invoked");
    CHECK_EQ(s_rx_all.device_id, 0x1234, "parsed device_id (endian)");
    CHECK_EQ(s_rx_all.device_version, 0x5678,
             "parsed device_version (endian)");
    CHECK_EQ(s_rx_all.active_profile, 1, "parsed active");
    CHECK_EQ(s_rx_all.forced_profile, 2, "parsed forced");
    CHECK_EQ(s_rx_all.profiles[0].radc[0], 40, "parsed p0 radc x1");
    CHECK_EQ(s_rx_all.profiles[2].comp_y, -95, "parsed p2 comp_y (endian)");
}

int main(void)
{
    test_crc16();
    test_swap_endian();
    test_profile_types();
    test_pack_unpack_roundtrip();
    test_unpack_errors();
    test_send_set_profile();
    test_send_get_all_and_force();
    test_send_base_reply_all();
    test_handle_set_profile();
    test_handle_get_all_and_force();
    test_handle_base_reply_all();

    printf("ls_proto: %d passed, %d failed\n", s_pass, s_fail);
    return (s_fail == 0) ? 0 : 1;
}
