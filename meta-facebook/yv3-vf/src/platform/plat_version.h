#ifndef PLAT_VERSION_H
#define PLAT_VERSION_H

#include "version.h"

#define PLATFORM_NAME "Yosemite 3"
#define PROJECT_NAME "Vernal Falls"
#define PROJECT_STAGE MP

/*
 * 0x07 Vernal Falls
 */
#define BOARD_ID 0x07
#define DEVICE_ID 0x00
#define DEVICE_REVISION 0x80

#define FIRMWARE_REVISION_1 GET_FW_VERSION1(BOARD_ID, PROJECT_STAGE)
#define FIRMWARE_REVISION_2 0x03

#define IPMI_VERSION 0x02
#define ADDITIONAL_DEVICE_SUPPORT 0xBF
#define PRODUCT_ID 0x0000
#define AUXILIARY_FW_REVISION 0x00000000
#define IANA_ID 0x009C9C // same as TI BIC

#define BIC_FW_YEAR_MSB 0x20
#define BIC_FW_YEAR_LSB 0x22
#define BIC_FW_WEEK 0x34
#define BIC_FW_VER 0x01
#define BIC_FW_platform_0 0x76 // char: v
#define BIC_FW_platform_1 0x66 // char: f
#define BIC_FW_platform_2 0x00 // char: '\0'

#endif
