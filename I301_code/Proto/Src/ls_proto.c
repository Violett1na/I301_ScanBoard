#include "ls_proto.h"
#include <string.h>

ls_packet_t ls_host_pkt;    //主机发送使用
ls_packet_t ls_device_pkt;    //从机回复使用

// 使用查表法计算CRC16
static const uint16_t ls_crc16_table[256] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7, 0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD,
    0xE1CE, 0xF1EF, 0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6, 0x9339, 0x8318, 0xB37B, 0xA35A,
    0xD3BD, 0xC39C, 0xF3FF, 0xE3DE, 0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485, 0xA56A, 0xB54B,
    0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D, 0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
    0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC, 0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861,
    0x2802, 0x3823, 0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B, 0x5AF5, 0x4AD4, 0x7AB7, 0x6A96,
    0x1A71, 0x0A50, 0x3A33, 0x2A12, 0xDBFD, 0xCBDC, 0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A, 0x6CA6, 0x7C87,
    0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41, 0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49,
    0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70, 0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A,
    0x9F59, 0x8F78, 0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F, 0x1080, 0x00A1, 0x30C2, 0x20E3,
    0x5004, 0x4025, 0x7046, 0x6067, 0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E, 0x02B1, 0x1290,
    0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256, 0xB5EA, 0xA5CB, 0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D,
    0x34E2, 0x24C3, 0x14A0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405, 0xA7DB, 0xB7FA, 0x8799, 0x97B8, 0xE75F, 0xF77E,
    0xC71D, 0xD73C, 0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657, 0x7676, 0x4615, 0x5634, 0xD94C, 0xC96D, 0xF90E, 0xE92F,
    0x99C8, 0x89E9, 0xB98A, 0xA9AB, 0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882, 0x28A3, 0xCB7D, 0xDB5C,
    0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A, 0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92,
    0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9, 0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83,
    0x1CE0, 0x0CC1, 0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8, 0x6E17, 0x7E36, 0x4E55, 0x5E74,
    0x2E93, 0x3EB2, 0x0ED1, 0x1EF0
};

/**
 * @brief 计算CRC16校验值
 * 
 * @param data 数据指针
 * @param length 数据长度
 * @return uint16_t CRC16校验值
 */
uint16_t ls_crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < length; i++)
    {
    crc = (crc << 8) ^ ls_crc16_table[((crc >> 8) ^ data[i]) & 0xFF];
    }
    return crc;
}

// 16位大小端互转
uint16_t ls_swap_endian_16(uint16_t val)
{
    return ((val & 0xFF00U) >> 8) | ((val & 0x00FFU) << 8);
}

// 32位大小端互转
uint32_t ls_swap_endian_32(uint32_t val)
{
    return ((val & 0xFF000000U) >> 24) | ((val & 0x00FF0000U) >> 8) | ((val & 0x0000FF00U) << 8) |
       ((val & 0x000000FFU) << 24);
}

// 64位大小端互转
uint64_t ls_swap_endian_64(uint64_t val)
{
    return ((val & 0xFF00000000000000ULL) >> 56) | ((val & 0x00FF000000000000ULL) >> 40) |
       ((val & 0x0000FF0000000000ULL) >> 24) | ((val & 0x000000FF00000000ULL) >> 8) |
       ((val & 0x00000000FF000000ULL) << 8) | ((val & 0x0000000000FF0000ULL) << 24) |
       ((val & 0x000000000000FF00ULL) << 40) | ((val & 0x00000000000000FFULL) << 56);
}

/**
 * @brief 打包lsnet_packet_t结构体为字节流
 * 
 * @param pkt 指向lsnet_packet_t结构体的指针
 * @param out_buf 输出字节流缓冲区
 * @param out_len 输出字节流长度指针
 * @return int 0表示成功，-1表示失败
 */
int ls_pack(ls_packet_t* pkt, uint8_t* out_buf, uint16_t *out_len)
{

    uint16_t offset = 0;
    // 1. 协议头
    memcpy(&out_buf[offset], LS_HEADER_STR, LS_HEADER_LEN);
    offset += LS_HEADER_LEN;
    // 2. pck_len
    out_buf[offset++] = (pkt->pck_len >> 8) & 0xFF;
    out_buf[offset++] = (pkt->pck_len) & 0xFF;
    // 5. version
    out_buf[offset++] = (LS_VERSION >> 8) & 0xFF;
    out_buf[offset++] = (LS_VERSION) & 0xFF;
    // 6. type
    out_buf[offset++] = pkt->type;
    // 7. cmd
    out_buf[offset++] = pkt->cmd;
    // 8. data_len
    out_buf[offset++] = (pkt->data_len >> 8) & 0xFF;
    out_buf[offset++] = (pkt->data_len) & 0xFF;
    // 9. data
    memcpy(&out_buf[offset], pkt->data, pkt->data_len);
    offset += pkt->data_len;

    // 10. CRC
    uint16_t crc = ls_crc16(out_buf, pkt->pck_len - 2);
    out_buf[offset++] = (crc >> 8) & 0xFF;
    out_buf[offset++] = crc & 0xFF;

    *out_len = offset;

    return 0;
}

/**
 * @brief 解包字节流为lsnet_packet_t结构体
 * 
 * @param in_buf 输入字节流缓冲区
 * @param in_len 输入字节流长度
 * @param pkt 指向lsnet_packet_t结构体的指针
 * @return int 0表示成功，-1表示失败，-2表示协议头错误，-3表示数据长度错误，-4表示CRC校验错误
 */
int ls_unpack(uint8_t* in_buf, uint16_t in_len, ls_packet_t* pkt)
{
    uint16_t offset = 0;
    if (in_buf == NULL || pkt == NULL)
        return -1;

    if(in_len < LS_DATA_BASE_LEN)   // 最小长度判断
        return -1;
    // 1. 协议头检查
    if(memcmp(&in_buf[offset], LS_HEADER_STR, LS_HEADER_LEN) != 0)
        return -2;
    offset += LS_HEADER_LEN;
    // 2. pck_len
    pkt->pck_len = (in_buf[offset] << 8) | in_buf[offset+1];
    offset += 2;

    // 4. version
    pkt->version = (in_buf[offset] << 8) | in_buf[offset+1];
    offset += 2;
    // 5. type
    pkt->type = in_buf[offset++];
    // 6. cmd
    pkt->cmd = in_buf[offset++];
    // 7. data_len
    pkt->data_len = (in_buf[offset] << 8) | in_buf[offset+1];
    offset += 2;
    if (pkt->pck_len != (uint16_t)(LS_DATA_BASE_LEN + pkt->data_len))
        return -3;
    if(pkt->data_len > (LS_MAX_DATA_LEN - LS_DATA_BASE_LEN))
        return -3;

    // 8. data
    memcpy(pkt->data, &in_buf[offset], pkt->data_len);
    offset += pkt->data_len; 
    // 9. CRC 校验
    uint16_t recv_crc = (in_buf[offset] << 8) | in_buf[offset+1];
    uint16_t calc_crc = ls_crc16(in_buf, pkt->pck_len - 2);

    if(recv_crc != calc_crc)
        return -4;
    pkt->crc = recv_crc;

    return 0;
}
