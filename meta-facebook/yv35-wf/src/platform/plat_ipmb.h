#ifndef PLAT_IPMB_H
#define PLAT_IPMB_H

#include "plat_i2c.h"

#define IPMB_RF_BIC_BUS I2C_BUS9

#define BMC_I2C_ADDRESS 0x10
#define SELF_I2C_ADDRESS 0x20
#define CXL_I2C_ADDRESS 0x20 // TBU
#define RF_BIC_I2C_ADDRESS 0x20

enum {
	RF_BIC_IPMB_IDX,
	MAX_IPMB_IDX,
};

#endif
