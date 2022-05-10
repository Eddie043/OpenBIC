#include "plat_sdr_table.h"

#include <stdio.h>
#include <string.h>

#include "sdr.h"
#include "plat_ipmb.h"
#include "plat_sensor_table.h"

SDR_Full_sensor plat_sdr_table[] = {};

uint8_t load_sdr_table(void)
{
	memcpy(full_sdr_table, plat_sdr_table, sizeof(plat_sdr_table));
	return (sizeof(plat_sdr_table) / sizeof(plat_sdr_table[0]));
};