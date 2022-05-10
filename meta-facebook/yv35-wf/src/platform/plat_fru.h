#ifndef PLAT_FRU_H
#define PLAT_FRU_H

#define EXB_FRU_PORT 0x02 //I2C[3] I2C_BUS3
#define EXB_FRU_ADDR (0xA8 >> 1)

enum {
	EXB_FRU_ID,
	//OTHER_FRU_ID,
};

#endif