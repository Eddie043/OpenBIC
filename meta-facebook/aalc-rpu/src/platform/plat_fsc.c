#include "plat_fsc.h"
#include <stdlib.h>
#include <libutil.h>
#include "sensor.h"
#include "plat_sensor_table.h"
#include <logging/log.h>

LOG_MODULE_REGISTER(plat_fsc);

struct k_thread fsc_thread;
K_KERNEL_STACK_MEMBER(fsc_thread_stack, 2048);

static uint8_t fsc_poll_flag = 1;
extern zone_cfg zone_table[];
extern uint32_t zone_table_size;

uint8_t get_fsc_enable_flag(void)
{
	return fsc_poll_flag;
}

void set_fsc_enable_flag(uint8_t flag)
{
	fsc_poll_flag = flag;
}

/* get the maximum duty of all stepwise sensor */
static uint8_t calculateStepwise(zone_cfg *zone_p, uint8_t *duty)
{
	CHECK_NULL_ARG_WITH_RETURN(zone_p, FSC_ERROR_NULL_ARG);
	CHECK_NULL_ARG_WITH_RETURN(duty, FSC_ERROR_NULL_ARG);

	if (!zone_p->sw_tbl || !zone_p->sw_tbl_num)
		return FSC_ERROR_NONE;

	LOG_INF("calculateStepwise");
	uint8_t max_duty = 0;

	for (int i = 0; i < zone_p->sw_tbl_num; i++) {
		stepwise_cfg *p = zone_p->sw_tbl + i;
		if (!p)
			continue;

		float tmp = 0.0;
		if (get_sensor_reading_to_real_val(p->sensor_num, &tmp) !=
			SENSOR_READ_4BYTE_ACUR_SUCCESS) {
			tmp = 100.0;
		}

		int16_t temp = (int16_t)tmp;
		LOG_INF("sensor_num %x, temp = %d", p->sensor_num, temp);

		// hysteresis
		if (p->pos_hyst || p->neg_hyst) {
			if (p->last_temp == FSC_TEMP_INVALID) // first time
				p->last_temp = temp;
			else if ((temp - p->last_temp) > p->pos_hyst)
                p->last_temp = temp;
            else if ((p->last_temp - temp) > p->neg_hyst)
                p->last_temp = temp;
		} else {
			p->last_temp = temp;
		}

		// find duty by temp
		uint8_t tmp_duty = 100;
		for (int j = 0; j < ARRAY_SIZE(p->step); j++) {
			LOG_INF("temp %d, duty %d", p->step[j].temp, p->step[j].duty);
			if (p->last_temp <= p->step[j].temp) {
				tmp_duty = p->step[j].duty;
				break;
			}
		}

		max_duty = MAX(max_duty, tmp_duty);
		LOG_INF("sensor_num %x, last_temp = %d, tmp_duty = %d, max_duty = %d", 
			p->sensor_num, p->last_temp, tmp_duty, max_duty);
	}

	*duty = max_duty;
	LOG_INF("*duty = %d", *duty);
	return FSC_ERROR_NONE;
}

static uint8_t calculatePID(zone_cfg *zone_p, uint8_t *duty)
{
#if 0
	CHECK_NULL_ARG_WITH_RETURN(rpm, FSC_ERROR_NULL_ARG);
	pid_cfg *table = find_pid_table(sensor_num);
	CHECK_NULL_ARG_WITH_RETURN(table, FSC_ERROR_NOT_FOUND_PID_TABLE);

	int error = table->setpoint - val;

	table->integral += error;
	int16_t iterm = table->integral * table->ki;

	if (table->i_limit_min)
		iterm = (iterm < table->i_limit_min) ? table->i_limit_min : iterm;
	if (table->i_limit_max)
		iterm = (iterm > table->i_limit_max) ? table->i_limit_max : iterm;

	int16_t output =
		table->kp * error + iterm + table->kd * (error - table->last_error); // ignore kd

	if (table->out_limit_min)
		output = (output < table->out_limit_min) ? table->out_limit_min : output;
	if (table->out_limit_max)
		output = (output > table->out_limit_max) ? table->out_limit_max : output;

	if (table->slew_pos && (output - table->last_rpm > table->slew_pos)) {
		output = table->last_rpm + table->slew_pos;
	} else if (table->slew_neg && (output - table->last_rpm < (-table->slew_neg))) {
		output = table->last_rpm - table->slew_neg;
	}

	table->last_error = error;
	table->last_rpm = output;
	*rpm = output;
#endif
	return FSC_ERROR_NONE;
}

uint8_t get_fsc_duty_cache(uint8_t zone, uint8_t *cache)
{
	CHECK_NULL_ARG_WITH_RETURN(cache, FSC_ERROR_NULL_ARG);
	if (zone >= zone_table_size)
		return FSC_ERROR_OUT_OF_RANGE;

	*cache = zone_table[zone].last_duty;
	return FSC_ERROR_NONE;
}

uint8_t get_fsc_poll_count(uint8_t zone, uint8_t *count)
{
	CHECK_NULL_ARG_WITH_RETURN(count, FSC_ERROR_NULL_ARG);
	if (zone >= zone_table_size)
		return FSC_ERROR_OUT_OF_RANGE;

	*count = zone_table[zone].fsc_poll_count;
	return FSC_ERROR_NONE;
}

/* set the zone_cfg stored data to default */
static void zone_reinit(void)
{
	for (int i = 0; i < zone_table_size; i++) {
		zone_cfg *zone_p = zone_table + i;

		// init stepwise last temp
		for (int j = 0; j < zone_p->sw_tbl_num; j++) {
			stepwise_cfg *p = zone_p->sw_tbl + i;
			if (p)
				p->last_temp = FSC_TEMP_INVALID;
		}

		// init pid
		for (uint8_t i = 0; i < zone_p->pid_tbl_num; i++) {
			pid_cfg *p = zone_p->pid_tbl;
			if (p) {
				p->integral = 0;
				p->last_error = 0;
				p->last_duty = 0;
			}
		}

		// init zone config
		zone_p->fsc_poll_count = 0;
		zone_p->last_duty = 0;
	}
}

/**
 * @brief Function to control the FSC thread.
 *
 * @param action The specified action, can be FSC_DISABLE (turn off) or FSC_ENABLE (turn on).
 */
void controlFSC(uint8_t action)
{
	fsc_poll_flag = (action == FSC_DISABLE) ? 0 : 1;
}

static void fsc_thread_handler(void *arug0, void *arug1, void *arug2)
{
	CHECK_NULL_ARG(arug0);
	CHECK_NULL_ARG(arug1);
	ARG_UNUSED(arug2);

	zone_cfg *zone_table = (zone_cfg *)arug0;
	uint32_t zone_table_size = POINTER_TO_UINT(arug1);

	LOG_INF("fsc_thread_handler zone_table %p, zone_table_size %d", zone_table, zone_table_size);

	const int fsc_poll_interval_ms = 1000;

	while (1) {
		k_msleep(fsc_poll_interval_ms);

		if (!fsc_poll_flag)
			continue;

		for (uint8_t i = 0; i < zone_table_size; i++) {
			uint8_t duty = 0;
			uint8_t tmp_duty = 0;

			zone_cfg *zone_p = zone_table + i;
			if (zone_p == NULL)
				goto fan_tbl_skip;

			if (zone_p->sw_tbl) {
				calculateStepwise(zone_p, &tmp_duty);
				duty = MAX(duty, tmp_duty);
			}

			if (zone_p->pid_tbl) {
				calculatePID(zone_p, &tmp_duty);
				duty = MAX(duty, tmp_duty);
			}

			LOG_INF("fsc zone %d, duty %d", i, duty);

			if (zone_p->set_duty)
				zone_p->set_duty(zone_p->set_duty_arg, duty);
			else
				LOG_ERR("FSC zone %d set duty function is NULL", i);

			zone_p->last_duty = duty;

fan_tbl_skip:
			// set_duty
			if (zone_p->set_duty)
				zone_p->set_duty(zone_p->set_duty_arg, duty);
			else
				LOG_ERR("FSC zone %d set duty function is NULL", i);

			if (zone_p->post_hook)
				zone_p->pre_hook(zone_p->post_hook_arg);
		}
	}
}

void fsc_init(void)
{
	zone_reinit();
	controlFSC(FSC_ENABLE);

	LOG_INF("fsc_init zone_table %p, zone_table_size %d", zone_table, zone_table_size);

	k_thread_create(&fsc_thread, fsc_thread_stack, K_THREAD_STACK_SIZEOF(fsc_thread_stack),
			fsc_thread_handler, zone_table, UINT_TO_POINTER(zone_table_size), NULL, CONFIG_MAIN_THREAD_PRIORITY, 0,
			K_NO_WAIT);
	k_thread_name_set(&fsc_thread, "fsc_thread");
}