#include "hal_gpio.h"
#include "plat_gpio.h"
#include "plat_isr.h"
#include "power_status.h"
#include "plat_power_seq.h"

void pal_set_sys_status()
{
	set_CL_DC_status(FM_POWER_EN);
	set_DC_status(PWRGD_CARD_PWROK);
	control_power_sequence();
}

#define DEF_PROJ_GPIO_PRIORITY 61

DEVICE_DEFINE(PRE_DEF_PROJ_GPIO, "PRE_DEF_PROJ_GPIO_NAME", &gpio_init, NULL, NULL, NULL,
	      POST_KERNEL, DEF_PROJ_GPIO_PRIORITY, NULL);