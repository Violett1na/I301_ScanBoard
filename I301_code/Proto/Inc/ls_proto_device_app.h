#ifndef LS_PROTO_DEVICE_APP_H
#define LS_PROTO_DEVICE_APP_H


#include "ls_proto_receive.h"
#include "ls_proto_trans.h"
#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SCANXY_VERSION 0x0001


void ls_app_init(void);

#ifdef __cplusplus
}
#endif

#endif // LS_PROTO_DEVICE_APP_H