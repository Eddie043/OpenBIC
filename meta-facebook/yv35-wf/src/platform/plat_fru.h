#ifndef PLAT_FRU_H
#define PLAT_FRU_H
#include "plat_i2c.h"

#define EXB_FRU_PORT I2C_BUS3
#define EXB_FRU_ADDR (0xA8 >> 1)

enum {
	EXB_FRU_ID,
	//OTHER_FRU_ID,
};

#endif