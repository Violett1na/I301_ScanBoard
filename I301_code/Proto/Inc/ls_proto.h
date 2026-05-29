#ifndef LSPROTO_H
#define LSPROTO_H

#include "ls_proto_data.h"

#ifdef __cplusplus
extern "C" {  
#endif

extern ls_packet_t ls_host_pkt;
extern ls_packet_t ls_device_pkt;

int ls_pack(ls_packet_t* pkt, uint8_t* out_buf, uint16_t *out_len);

int ls_unpack(uint8_t* in_buf, uint16_t in_len, ls_packet_t* pkt);

uint16_t ls_crc16(const uint8_t *data, size_t length);

uint16_t ls_swap_endian_16(uint16_t val);
uint32_t ls_swap_endian_32(uint32_t val);
uint64_t ls_swap_endian_64(uint64_t val);

/************************     命令字功能函数     ***********************/




#ifdef __cplusplus
}
#endif

#endif // LSNET_H
