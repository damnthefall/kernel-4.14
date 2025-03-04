/*
 * Copyright (C) 2017 MediaTek Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kobject.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/err.h>
#include <linux/syscalls.h>
#include <linux/platform_device.h>
#include "mt-plat/mtk_thermal_monitor.h"
#include <linux/uidgid.h>
#include <linux/notifier.h>
#include <linux/fb.h>
#include "mach/mtk_thermal.h"
#if (CONFIG_MTK_GAUGE_VERSION == 30)
#include <mt-plat/charger_type.h>
#include <mt-plat/mtk_charger.h>
#include <mt-plat/mtk_battery.h>
#else
#include <tmp_battery.h>
#include <charging.h>
#endif
#include <linux/power_supply.h>
/* ************************************ */
/* Weak functions */
/* ************************************ */
	int __attribute__ ((weak))
get_bat_charging_current_level(void)
{
	pr_notice("E_WF: %s doesn't exist\n", __func__);
	return 500;
}

	enum charger_type __attribute__ ((weak))
mt_get_charger_type(void)
{
	pr_notice("E_WF: %s doesn't exist\n", __func__);
	return STANDARD_HOST;
}

#if (CONFIG_MTK_GAUGE_VERSION == 30)
	int __attribute__ ((weak))
charger_manager_set_charging_current_limit(
struct charger_consumer *consumer, int idx, int charging_current_uA)
{
	pr_notice("E_WF: %s doesn't exist\n", __func__);
	return 0;
}
	int __attribute__ ((weak))
charger_manager_set_input_current_limit(
struct charger_consumer *consumer, int idx, int input_current_uA)
{
	pr_notice("E_WF: %s doesn't exist\n", __func__);
	return 0;
}

	int __attribute__ ((weak))
mtk_chr_get_tchr_x(int *min_temp, int *max_temp)
{
	pr_notice("E_WF: %s doesn't exist\n", __func__);
	return 0;
}
/* mtk_chr_get_tchr() */

	int __attribute__ ((weak))
charger_manager_get_current_charging_type(struct charger_consumer *consumer)
{
	pr_notice("E_WF: %s doesn't exist\n", __func__);
	return -1;
}
#endif
/* ************************************ */

#define mtk_cooler_bcct_2nd_dprintk_always(fmt, args...) \
	pr_notice("[Thermal/TC/bcct_2nd]" fmt, ##args)

#define mtk_cooler_bcct_2nd_dprintk(fmt, args...) \
	do { \
		if (cl_bcct_2nd_klog_on == 1) \
			pr_debug("[Thermal/TC/bcct_2nd]" fmt, ##args); \
	}  while (0)

#define MAX_NUM_INSTANCE_MTK_COOLER_BCCT_2ND  3

#define MTK_CL_BCCT_2ND_GET_LIMIT(limit, state) \
{(limit) = (short) (((unsigned long) (state))>>16); }

#define MTK_CL_BCCT_2ND_SET_LIMIT(limit, state) \
{(state) = ((((unsigned long) (state))&0xFFFF) | ((short) limit<<16)); }

#define MTK_CL_BCCT_2ND_GET_CURR_STATE(curr_state, state) \
{(curr_state) = (((unsigned long) (state))&0xFFFF); }

#define MTK_CL_BCCT_2ND_SET_CURR_STATE(curr_state, state) \
	do { \
		if (0 == (curr_state)) \
			state &= ~0x1; \
		else \
			state |= 0x1; \
	} while (0)

static kuid_t uid = KUIDT_INIT(0);
static kgid_t gid = KGIDT_INIT(1000);

#define MIN(_a_, _b_) ((_a_) > (_b_) ? (_b_) : (_a_))
#define MAX(_a_, _b_) ((_a_) > (_b_) ? (_a_) : (_b_))

/* Battery & Charger Status*/
#ifdef BATTERY_INFO
static int bat_info_mintchr; /* charger min temp */
static int bat_info_maxtchr; /* charger max temp */
#endif
#if (CONFIG_MTK_GAUGE_VERSION == 30)
static struct charger_consumer *pthermal_consumer;
#endif

/* Charger Limiter
 * Charger Limiter provides API to limit charger IC input current and
 * battery charging current. It arbitrates the limitation from users and sets
 * limitation to charger driver via two API functions:
 *	set_chr_input_current_limit()
 *	set_bat_charging_current_limit()
 */
int x_chrlmt_chr_input_curr_limit = -1; /**< -1 is unlimit, unit is mA. */
int x_chrlmt_bat_chr_curr_limit = -1; /**< -1 is unlimit, unit is mA. */
static bool x_chrlmt_is_lcmoff; /**0 is lcm on, 1 is lcm off */
static int x_chrlmt_lcmoff_policy_enable; /**0: No lcmoff abcct_2nd */

static struct power_supply *cp_psy = NULL;

struct x_chrlmt_handle {
	int chr_input_curr_limit;
	int bat_chr_curr_limit;
};

static struct workqueue_struct *bcct_2nd_chrlmt_queue;
static struct work_struct      bcct_2nd_chrlmt_work;

/* temp solution, use list instead */
#define CHR_LMT_MAX_USER_COUNT	(4)

static struct x_chrlmt_handle
		*x_chrlmt_registered_users[CHR_LMT_MAX_USER_COUNT] = { 0 };

static int x_chrlmt_register(struct x_chrlmt_handle *handle)
{
	int i;

	if (!handle)
		return -1;

	handle->chr_input_curr_limit = -1;
	handle->bat_chr_curr_limit = -1;

	/* find an empty entry */
	for (i = CHR_LMT_MAX_USER_COUNT; --i >= 0; )
		if (!x_chrlmt_registered_users[i]) {
			x_chrlmt_registered_users[i] = handle;
			return 0;
		}

	return -1;
}

static int x_chrlmt_unregister(struct x_chrlmt_handle *handle)
{
	return -1;
}

static void x_chrlmt_set_limit_handler(struct work_struct *work)
{
	int rc = 0;
	union power_supply_propval prop = {0, };

	if (!cp_psy) {
		cp_psy = power_supply_get_by_name("mmi_chrg_manager");
		if (!cp_psy) {
			mtk_cooler_bcct_2nd_dprintk_always("get cp psy failed\n",
								__func__);
			return;
		}
	}

	prop.intval = (x_chrlmt_bat_chr_curr_limit != -1) ?
				x_chrlmt_bat_chr_curr_limit * 1000 : -1;
	rc = power_supply_set_property(cp_psy,
				POWER_SUPPLY_PROP_SYSTEM_TEMP_LEVEL, &prop);
	if (rc) {
		mtk_cooler_bcct_2nd_dprintk_always("set cp psy failed\n",
								__func__);
	}

	mtk_cooler_bcct_2nd_dprintk_always("%s %d %d\n", __func__,
						x_chrlmt_chr_input_curr_limit,
						x_chrlmt_bat_chr_curr_limit);
}

static int x_chrlmt_set_limit(
struct x_chrlmt_handle *handle, int chr_input_curr_limit,
int bat_char_curr_limit)
{
	int i;
	int min_char_input_curr_limit = 0xFFFFFF;
	int min_bat_char_curr_limit = 0xFFFFFF;

	if (!handle)
		return -1;

	handle->chr_input_curr_limit = chr_input_curr_limit;
	handle->bat_chr_curr_limit = bat_char_curr_limit;

	for (i = CHR_LMT_MAX_USER_COUNT; --i >= 0; )
		if (x_chrlmt_registered_users[i]) {
			if (x_chrlmt_registered_users[i]->chr_input_curr_limit
			> -1)
				min_char_input_curr_limit =
				MIN(x_chrlmt_registered_users[i]
					->chr_input_curr_limit
						, min_char_input_curr_limit);

			if (x_chrlmt_registered_users[i]->bat_chr_curr_limit
			> -1)
				min_bat_char_curr_limit =
				MIN(x_chrlmt_registered_users[i]
					->bat_chr_curr_limit
						, min_bat_char_curr_limit);
		}

	if (min_char_input_curr_limit == 0xFFFFFF)
		min_char_input_curr_limit = -1;
	if (min_bat_char_curr_limit == 0xFFFFFF)
		min_bat_char_curr_limit = -1;

      if (!cp_psy) {
		cp_psy = power_supply_get_by_name("mmi_chrg_manager");
		if (!cp_psy) {
			mtk_cooler_bcct_2nd_dprintk_always("get cp psy failed\n",
								__func__);
			return 0;
		}
	} else if ((min_char_input_curr_limit != x_chrlmt_chr_input_curr_limit)
	|| (min_bat_char_curr_limit != x_chrlmt_bat_chr_curr_limit)) {
		x_chrlmt_chr_input_curr_limit = min_char_input_curr_limit;
		x_chrlmt_bat_chr_curr_limit = min_bat_char_curr_limit;

		if (bcct_2nd_chrlmt_queue)
			queue_work(bcct_2nd_chrlmt_queue,
						&bcct_2nd_chrlmt_work);

		mtk_cooler_bcct_2nd_dprintk_always("%s %p %d %d\n", __func__
					, handle, x_chrlmt_chr_input_curr_limit,
					x_chrlmt_bat_chr_curr_limit);
	}

	return 0;
}

static int cl_bcct_2nd_klog_on;
static struct thermal_cooling_device
		*cl_bcct_2nd_dev[MAX_NUM_INSTANCE_MTK_COOLER_BCCT_2ND] = { 0 };

static unsigned long cl_bcct_2nd_state[MAX_NUM_INSTANCE_MTK_COOLER_BCCT_2ND]
									= { 0 };

static struct x_chrlmt_handle cl_bcct_2nd_chrlmt_handle;

static int cl_bcct_2nd_cur_limit = 65535;

static void mtk_cl_bcct_2nd_set_bcct_limit(void)
{
	/* TODO: optimize */
	int i = 0;
	int min_limit = 65535;

	for (; i < MAX_NUM_INSTANCE_MTK_COOLER_BCCT_2ND; i++) {
		unsigned long curr_state;

		MTK_CL_BCCT_2ND_GET_CURR_STATE(curr_state,
						cl_bcct_2nd_state[i]);

		if (curr_state == 1) {

			int limit;

			MTK_CL_BCCT_2ND_GET_LIMIT(limit, cl_bcct_2nd_state[i]);
			if ((min_limit > limit) && (limit > 0))
				min_limit = limit;
		}
	}

	if (min_limit != cl_bcct_2nd_cur_limit) {
		cl_bcct_2nd_cur_limit = min_limit;

		if (cl_bcct_2nd_cur_limit >= 65535) {
			x_chrlmt_set_limit(&cl_bcct_2nd_chrlmt_handle, -1, -1);
			mtk_cooler_bcct_2nd_dprintk("%s limit=-1\n", __func__);
		} else {
			x_chrlmt_set_limit(&cl_bcct_2nd_chrlmt_handle, -1,
							cl_bcct_2nd_cur_limit);

			mtk_cooler_bcct_2nd_dprintk("%s limit=%d\n", __func__
					, cl_bcct_2nd_cur_limit);
		}

		mtk_cooler_bcct_2nd_dprintk("%s real limit=%d\n", __func__
				, get_bat_charging_current_level() / 100);

	}
}

static int mtk_cl_bcct_2nd_get_max_state(
struct thermal_cooling_device *cdev, unsigned long *state)
{
	*state = 1;
	mtk_cooler_bcct_2nd_dprintk("%s %s %lu\n", __func__,
						cdev->type, *state);
	return 0;
}

static int mtk_cl_bcct_2nd_get_cur_state(
struct thermal_cooling_device *cdev, unsigned long *state)
{
	MTK_CL_BCCT_2ND_GET_CURR_STATE(*state,
				*((unsigned long *)cdev->devdata));

	mtk_cooler_bcct_2nd_dprintk("%s %s %lu\n", __func__,
				cdev->type, *state);

	mtk_cooler_bcct_2nd_dprintk("%s %s limit=%d\n", __func__,
					cdev->type,
					get_bat_charging_current_level() / 100);
	return 0;
}

static int mtk_cl_bcct_2nd_set_cur_state(
struct thermal_cooling_device *cdev, unsigned long state)
{
	/*Only active while lcm not off */
	if (x_chrlmt_is_lcmoff)
		state = 0;

	mtk_cooler_bcct_2nd_dprintk("%s %s %lu\n", __func__, cdev->type, state);
	MTK_CL_BCCT_2ND_SET_CURR_STATE(state,
				*((unsigned long *)cdev->devdata));

	mtk_cl_bcct_2nd_set_bcct_limit();
	mtk_cooler_bcct_2nd_dprintk("%s %s limit=%d\n", __func__, cdev->type,
			get_bat_charging_current_level() / 100);

	return 0;
}

/* bind fan callbacks to fan device */
static struct thermal_cooling_device_ops mtk_cl_bcct_2nd_ops = {
	.get_max_state = mtk_cl_bcct_2nd_get_max_state,
	.get_cur_state = mtk_cl_bcct_2nd_get_cur_state,
	.set_cur_state = mtk_cl_bcct_2nd_set_cur_state,
};

static int mtk_cooler_bcct_2nd_register_ltf(void)
{
	int i;

	mtk_cooler_bcct_2nd_dprintk("%s\n", __func__);

	x_chrlmt_register(&cl_bcct_2nd_chrlmt_handle);

	for (i = MAX_NUM_INSTANCE_MTK_COOLER_BCCT_2ND; i-- > 0;) {
		char temp[20] = { 0 };

		sprintf(temp, "mtk-cl-bcct%02d_2nd", i);
		/* put bcct_2nd state to cooler devdata */
		cl_bcct_2nd_dev[i] = mtk_thermal_cooling_device_register(
					temp, (void *)&cl_bcct_2nd_state[i],
					&mtk_cl_bcct_2nd_ops);
	}

	return 0;
}

static void mtk_cooler_bcct_2nd_unregister_ltf(void)
{
	int i;

	mtk_cooler_bcct_2nd_dprintk("%s\n", __func__);

	for (i = MAX_NUM_INSTANCE_MTK_COOLER_BCCT_2ND; i-- > 0;) {
		if (cl_bcct_2nd_dev[i]) {
			mtk_thermal_cooling_device_unregister(
			cl_bcct_2nd_dev[i]);
			cl_bcct_2nd_dev[i] = NULL;
			cl_bcct_2nd_state[i] = 0;
		}
	}

	x_chrlmt_unregister(&cl_bcct_2nd_chrlmt_handle);
}

static struct thermal_cooling_device *cl_abcct_2nd_dev;
static unsigned long cl_abcct_2nd_state;
static struct x_chrlmt_handle abcct_2nd_chrlmt_handle;
static long abcct_2nd_prev_temp;
static long abcct_2nd_curr_temp;
static long abcct_2nd_target_temp = 48000;
static long abcct_2nd_kp = 1000;
static long abcct_2nd_ki = 3000;
static long abcct_2nd_kd = 10000;
static int abcct_2nd_max_bat_chr_curr_limit = 3000;
static int abcct_2nd_min_bat_chr_curr_limit = 200;
static int abcct_2nd_cur_bat_chr_curr_limit;
static long abcct_2nd_iterm;

#ifdef BATTERY_INFO
static void bat_chg_info_update(void)
{
#if (CONFIG_MTK_GAUGE_VERSION == 30)
	if (cl_bcct_2nd_klog_on == 1) {
	/*
	 * int ret = mtk_chr_get_tchr_x(&bat_info_mintchr, &bat_info_maxtchr);
	 * if (ret)
	 * mtk_cooler_bcct_dprintk("mtk_chr_get_tchr_x: %d %d err: %d\n",
	 *			bat_info_mintchr, bat_info_maxtchr, ret);
	 */
	}
#endif
}
#endif

static int mtk_cl_abcct_2nd_get_max_state(
struct thermal_cooling_device *cdev, unsigned long *state)
{
	*state = 1;
	mtk_cooler_bcct_2nd_dprintk("%s %s %lu\n", __func__,
						cdev->type, *state);
	return 0;
}

static int mtk_cl_abcct_2nd_get_cur_state(
struct thermal_cooling_device *cdev, unsigned long *state)
{
	*state = cl_abcct_2nd_state;
	mtk_cooler_bcct_2nd_dprintk("%s %s %lu\n", __func__,
						cdev->type, *state);
	return 0;
}

static int mtk_cl_abcct_2nd_set_cur_state(
struct thermal_cooling_device *cdev, unsigned long state)
{
#ifdef BATTERY_INFO
	static ktime_t lasttime;
#endif

	cl_abcct_2nd_state = state;
	/*Only active while lcm not off */
	if (x_chrlmt_is_lcmoff)
		cl_abcct_2nd_state = 0;

#ifdef BATTERY_INFO
	if (ktime_to_ms(ktime_sub(ktime_get(), lasttime)) > 5000) {
		bat_chg_info_update();
		lasttime = ktime_get();
	}
#endif

	mtk_cooler_bcct_2nd_dprintk("%s %s %lu\n", __func__,
						cdev->type, cl_abcct_2nd_state);
	return 0;
}

/* bind fan callbacks to fan device */
static struct thermal_cooling_device_ops mtk_cl_abcct_2nd_ops = {
	.get_max_state = mtk_cl_abcct_2nd_get_max_state,
	.get_cur_state = mtk_cl_abcct_2nd_get_cur_state,
	.set_cur_state = mtk_cl_abcct_2nd_set_cur_state,
};

static int mtk_cl_abcct_2nd_set_cur_temp(
struct thermal_cooling_device *cdev, unsigned long temp)
{
	long delta, pterm, dterm;
	int limit;

	/* based on temp and state to do ATM */
	abcct_2nd_prev_temp = abcct_2nd_curr_temp;
	abcct_2nd_curr_temp = (long) temp;

	if (cl_abcct_2nd_state == 0) {
		abcct_2nd_iterm = 0;
		abcct_2nd_cur_bat_chr_curr_limit =
					abcct_2nd_max_bat_chr_curr_limit;

		x_chrlmt_set_limit(&abcct_2nd_chrlmt_handle, -1, -1);
		return 0;
	}

	pterm = abcct_2nd_target_temp - abcct_2nd_curr_temp;

	abcct_2nd_iterm += pterm;
	if (((abcct_2nd_curr_temp < abcct_2nd_target_temp)
		&& (abcct_2nd_iterm < 0))
	|| ((abcct_2nd_curr_temp > abcct_2nd_target_temp)
		&& (abcct_2nd_iterm > 0)))
		abcct_2nd_iterm = 0;

	if (((abcct_2nd_curr_temp < abcct_2nd_target_temp)
		&& (abcct_2nd_curr_temp < abcct_2nd_prev_temp))
	|| ((abcct_2nd_curr_temp > abcct_2nd_target_temp)
		&& (abcct_2nd_curr_temp > abcct_2nd_prev_temp)))
		dterm = abcct_2nd_prev_temp - abcct_2nd_curr_temp;
	else
		dterm = 0;

	delta = pterm/abcct_2nd_kp + abcct_2nd_iterm/abcct_2nd_ki
		+ dterm/abcct_2nd_kd;

	/* Align limit to 500mA to avoid redundant calls to x_chrlmt. */
	if (delta > 0 && delta < 500)
		delta = 500;
	else if (delta > -500 && delta < 0)
		delta = -500;

	limit = abcct_2nd_cur_bat_chr_curr_limit + (int) delta;
	/* Align limit to 50mA to avoid redundant calls to x_chrlmt. */
	limit = (limit / 500) * 500;
	limit = MIN(abcct_2nd_max_bat_chr_curr_limit, limit);
	limit = MAX(abcct_2nd_min_bat_chr_curr_limit, limit);
	abcct_2nd_cur_bat_chr_curr_limit = limit;

	mtk_cooler_bcct_2nd_dprintk("%s %ld %ld %ld %ld %ld %d\n"
			, __func__, abcct_2nd_curr_temp, pterm, abcct_2nd_iterm,
			dterm, delta, abcct_2nd_cur_bat_chr_curr_limit);

	x_chrlmt_set_limit(&abcct_2nd_chrlmt_handle, -1,
					abcct_2nd_cur_bat_chr_curr_limit);

	return 0;
}

static struct thermal_cooling_device_ops_extra mtk_cl_abcct_2nd_ops_ext = {
	.set_cur_temp = mtk_cl_abcct_2nd_set_cur_temp
};

static struct thermal_cooling_device *cl_abcct_2nd_lcmoff_dev;
static unsigned long cl_abcct_2nd_lcmoff_state;
static struct x_chrlmt_handle abcct_2nd_lcmoff_chrlmt_handle;
static long abcct_2nd_lcmoff_prev_temp;
static long abcct_2nd_lcmoff_curr_temp;
static long abcct_2nd_lcmoff_target_temp = 48000;
static long abcct_2nd_lcmoff_kp = 1000;
static long abcct_2nd_lcmoff_ki = 3000;
static long abcct_2nd_lcmoff_kd = 10000;
static int abcct_2nd_lcmoff_max_bat_chr_curr_limit = 3000;
static int abcct_2nd_lcmoff_min_bat_chr_curr_limit = 200;
static int abcct_2nd_lcmoff_cur_bat_chr_curr_limit;
static long abcct_2nd_lcmoff_iterm;

static int mtk_cl_abcct_2nd_lcmoff_get_max_state(
struct thermal_cooling_device *cdev, unsigned long *state)
{
	*state = 1;
	mtk_cooler_bcct_2nd_dprintk("%s %s %lu\n", __func__,
						cdev->type, *state);
	return 0;
}

static int mtk_cl_abcct_2nd_lcmoff_get_cur_state(
struct thermal_cooling_device *cdev, unsigned long *state)
{
	*state = cl_abcct_2nd_lcmoff_state;
	mtk_cooler_bcct_2nd_dprintk("%s %s %lu\n", __func__,
						cdev->type, *state);
	return 0;
}

static int mtk_cl_abcct_2nd_lcmoff_set_cur_state(
struct thermal_cooling_device *cdev, unsigned long state)
{
	cl_abcct_2nd_lcmoff_state = state;

	/*Only active while lcm off */
	if (!x_chrlmt_is_lcmoff)
		cl_abcct_2nd_lcmoff_state = 0;
	mtk_cooler_bcct_2nd_dprintk("%s %s %lu\n", __func__, cdev->type,
						cl_abcct_2nd_lcmoff_state);
	return 0;
}

/* bind fan callbacks to fan device */
static struct thermal_cooling_device_ops mtk_cl_abcct_2nd_lcmoff_ops = {
	.get_max_state = mtk_cl_abcct_2nd_lcmoff_get_max_state,
	.get_cur_state = mtk_cl_abcct_2nd_lcmoff_get_cur_state,
	.set_cur_state = mtk_cl_abcct_2nd_lcmoff_set_cur_state,
};

static int mtk_cl_abcct_2nd_lcmoff_set_cur_temp(
struct thermal_cooling_device *cdev, unsigned long temp)
{
	long delta, pterm, dterm;
	int limit;

	/* based on temp and state to do ATM */
	abcct_2nd_lcmoff_prev_temp = abcct_2nd_lcmoff_curr_temp;
	abcct_2nd_lcmoff_curr_temp = (long) temp;

	if (cl_abcct_2nd_lcmoff_state == 0) {
		abcct_2nd_lcmoff_iterm = 0;
		abcct_2nd_lcmoff_cur_bat_chr_curr_limit =
					abcct_2nd_lcmoff_max_bat_chr_curr_limit;

		x_chrlmt_set_limit(&abcct_2nd_lcmoff_chrlmt_handle, -1, -1);
		return 0;
	}

	pterm = abcct_2nd_lcmoff_target_temp - abcct_2nd_lcmoff_curr_temp;

	abcct_2nd_lcmoff_iterm += pterm;
	if (((abcct_2nd_lcmoff_curr_temp < abcct_2nd_target_temp)
		&& (abcct_2nd_lcmoff_iterm < 0))
	|| ((abcct_2nd_lcmoff_curr_temp > abcct_2nd_target_temp)
		&& (abcct_2nd_lcmoff_iterm > 0)))
		abcct_2nd_lcmoff_iterm = 0;

	if (((abcct_2nd_lcmoff_curr_temp < abcct_2nd_target_temp)
		&& (abcct_2nd_lcmoff_curr_temp < abcct_2nd_lcmoff_prev_temp))
	|| ((abcct_2nd_lcmoff_curr_temp > abcct_2nd_target_temp)
		&& (abcct_2nd_lcmoff_curr_temp > abcct_2nd_lcmoff_prev_temp)))
		dterm = abcct_2nd_lcmoff_prev_temp - abcct_2nd_lcmoff_curr_temp;
	else
		dterm = 0;

	delta = pterm/abcct_2nd_lcmoff_kp +
		abcct_2nd_lcmoff_iterm/abcct_2nd_lcmoff_ki +
		dterm/abcct_2nd_lcmoff_kd;

	/* Align limit to 50mA to avoid redundant calls to x_chrlmt. */
	if (delta > 0 && delta < 50)
		delta = 50;
	else if (delta > -50 && delta < 0)
		delta = -50;

	limit = abcct_2nd_lcmoff_cur_bat_chr_curr_limit + (int) delta;
	/* Align limit to 50mA to avoid redundant calls to x_chrlmt. */
	limit = (limit / 50) * 50;
	limit = MIN(abcct_2nd_lcmoff_max_bat_chr_curr_limit, limit);
	limit = MAX(abcct_2nd_lcmoff_min_bat_chr_curr_limit, limit);
	abcct_2nd_lcmoff_cur_bat_chr_curr_limit = limit;

	mtk_cooler_bcct_2nd_dprintk("%s %ld %ld %ld %ld %ld %d\n"
			, __func__, abcct_2nd_lcmoff_curr_temp, pterm,
			abcct_2nd_lcmoff_iterm, dterm, delta, limit);

	x_chrlmt_set_limit(&abcct_2nd_lcmoff_chrlmt_handle, -1, limit);

	return 0;
}

static struct thermal_cooling_device_ops_extra
mtk_cl_abcct_2nd_lcmoff_ops_ext = {
	.set_cur_temp = mtk_cl_abcct_2nd_lcmoff_set_cur_temp
};

static int mtk_cooler_abcct_2nd_register_ltf(void)
{
	mtk_cooler_bcct_2nd_dprintk("%s\n", __func__);

	x_chrlmt_register(&abcct_2nd_chrlmt_handle);

	if (!cl_abcct_2nd_dev)
		cl_abcct_2nd_dev =
			mtk_thermal_cooling_device_register_wrapper_extra(
			"abcct_2nd", (void *)NULL, &mtk_cl_abcct_2nd_ops,
			&mtk_cl_abcct_2nd_ops_ext);

	return 0;
}

static void mtk_cooler_abcct_2nd_unregister_ltf(void)
{
	mtk_cooler_bcct_2nd_dprintk("%s\n", __func__);

	if (cl_abcct_2nd_dev) {
		mtk_thermal_cooling_device_unregister(cl_abcct_2nd_dev);
		cl_abcct_2nd_dev = NULL;
		cl_abcct_2nd_state = 0;
	}

	x_chrlmt_unregister(&abcct_2nd_chrlmt_handle);
}

static int mtk_cooler_abcct_2nd_lcmoff_register_ltf(void)
{
	mtk_cooler_bcct_2nd_dprintk("%s\n", __func__);

	x_chrlmt_register(&abcct_2nd_lcmoff_chrlmt_handle);

	if (!cl_abcct_2nd_lcmoff_dev)
		cl_abcct_2nd_lcmoff_dev =
			mtk_thermal_cooling_device_register_wrapper_extra(
			"abcct_2nd_lcmoff", (void *)NULL,
			&mtk_cl_abcct_2nd_lcmoff_ops,
			&mtk_cl_abcct_2nd_lcmoff_ops_ext);

	return 0;
}

static void mtk_cooler_abcct_2nd_lcmoff_unregister_ltf(void)
{
	mtk_cooler_bcct_2nd_dprintk("%s\n", __func__);

	if (cl_abcct_2nd_lcmoff_dev) {
		mtk_thermal_cooling_device_unregister(cl_abcct_2nd_lcmoff_dev);
		cl_abcct_2nd_lcmoff_dev = NULL;
		cl_abcct_2nd_lcmoff_state = 0;
	}

	x_chrlmt_unregister(&abcct_2nd_lcmoff_chrlmt_handle);
}

static ssize_t _cl_bcct_2nd_write(
struct file *filp, const char __user *buf, size_t len, loff_t *data)
{
	/* int ret = 0; */
	char tmp[128] = { 0 };
	int klog_on, limit0, limit1, limit2;

	len = (len < (128 - 1)) ? len : (128 - 1);
	/* write data to the buffer */
	if (copy_from_user(tmp, buf, len))
		return -EFAULT;

/**
 * sscanf format <klog_on> <mtk-cl-bcct_2nd00 limit> <mtk-cl-bcct_2nd01 limit>
 * <klog_on> can only be 0 or 1
 * <mtk-cl-bcct_2nd00 limit> can only be positive integer
 *				or -1 to denote no limit
 */

	if (data == NULL) {
		mtk_cooler_bcct_2nd_dprintk("%s null data\n", __func__);
		return -EINVAL;
	}
	/* WARNING: Modify here if
	 * MTK_THERMAL_MONITOR_COOLER_MAX_EXTRA_CONDITIONS
	 * is changed to other than 3
	 */
#if (MAX_NUM_INSTANCE_MTK_COOLER_BCCT_2ND == 3)
	MTK_CL_BCCT_2ND_SET_LIMIT(-1, cl_bcct_2nd_state[0]);
	MTK_CL_BCCT_2ND_SET_LIMIT(-1, cl_bcct_2nd_state[1]);
	MTK_CL_BCCT_2ND_SET_LIMIT(-1, cl_bcct_2nd_state[2]);

	if (sscanf(tmp, "%d %d %d %d", &klog_on, &limit0, &limit1, &limit2)
	>= 1) {
		if (klog_on == 0 || klog_on == 1)
			cl_bcct_2nd_klog_on = klog_on;

		if (limit0 >= -1)
			MTK_CL_BCCT_2ND_SET_LIMIT(limit0, cl_bcct_2nd_state[0]);
		if (limit1 >= -1)
			MTK_CL_BCCT_2ND_SET_LIMIT(limit1, cl_bcct_2nd_state[1]);
		if (limit2 >= -1)
			MTK_CL_BCCT_2ND_SET_LIMIT(limit2, cl_bcct_2nd_state[2]);

		return len;
	}
#else
#error	\
"Change correspondent part when changing MAX_NUM_INSTANCE_MTK_COOLER_BCCT_2ND!"
#endif
	mtk_cooler_bcct_2nd_dprintk("%s bad argument\n", __func__);
	return -EINVAL;
}

static int _cl_bcct_2nd_read(struct seq_file *m, void *v)
{
	/**
	 * The format to print out:
	 *  kernel_log <0 or 1>
	 *  <mtk-cl-bcct_2nd<ID>> <bcc limit>
	 *  ..
	 */

	mtk_cooler_bcct_2nd_dprintk("%s\n", __func__);

	{
		int i = 0;

		seq_printf(m, "%d\n", cl_bcct_2nd_cur_limit);
		seq_printf(m, "klog %d\n", cl_bcct_2nd_klog_on);
		seq_printf(m, "curr_limit %d\n", cl_bcct_2nd_cur_limit);

		for (; i < MAX_NUM_INSTANCE_MTK_COOLER_BCCT_2ND; i++) {
			int limit;
			unsigned int curr_state;

			MTK_CL_BCCT_2ND_GET_LIMIT(limit, cl_bcct_2nd_state[i]);
			MTK_CL_BCCT_2ND_GET_CURR_STATE(curr_state,
							cl_bcct_2nd_state[i]);

			seq_printf(m, "mtk-cl-bcct_2nd%02d %d mA, state %d\n",
							i, limit, curr_state);
		}
	}

	return 0;
}

static int _cl_bcct_2nd_open(struct inode *inode, struct file *file)
{
	return single_open(file, _cl_bcct_2nd_read, PDE_DATA(inode));
}

static const struct file_operations _cl_bcct_2nd_fops = {
	.owner = THIS_MODULE,
	.open = _cl_bcct_2nd_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.write = _cl_bcct_2nd_write,
	.release = single_release,
};

static ssize_t _cl_abcct_2nd_write(
struct file *filp, const char __user *buf, size_t len, loff_t *data)
{
	/* int ret = 0; */
	char tmp[128] = { 0 };
	long _abcct_2nd_target_temp, _abcct_2nd_kp,
		_abcct_2nd_ki, _abcct_2nd_kd;

	int _max_cur, _min_cur;
	int scan_count = 0;

	len = (len < (128 - 1)) ? len : (128 - 1);
	/* write data to the buffer */
	if (copy_from_user(tmp, buf, len))
		return -EFAULT;

	if (data == NULL)  {
		mtk_cooler_bcct_2nd_dprintk("%s null data\n", __func__);
		return -EINVAL;
	}

	scan_count = sscanf(tmp, "%ld %ld %ld %ld %d %d"
			, &_abcct_2nd_target_temp, &_abcct_2nd_kp
			, &_abcct_2nd_ki, &_abcct_2nd_kd
			, &_max_cur, &_min_cur);

	if (scan_count >= 6) {
		abcct_2nd_target_temp = _abcct_2nd_target_temp;
		abcct_2nd_kp = _abcct_2nd_kp;
		abcct_2nd_ki = _abcct_2nd_ki;
		abcct_2nd_kd = _abcct_2nd_kd;
		abcct_2nd_max_bat_chr_curr_limit = _max_cur;
		abcct_2nd_min_bat_chr_curr_limit = _min_cur;
		abcct_2nd_cur_bat_chr_curr_limit =
					abcct_2nd_max_bat_chr_curr_limit;

		abcct_2nd_iterm = 0;

		return len;
	}

	mtk_cooler_bcct_2nd_dprintk("%s bad argument\n", __func__);
	return -EINVAL;
}

static int _cl_abcct_2nd_read(struct seq_file *m, void *v)
{
	mtk_cooler_bcct_2nd_dprintk("%s\n", __func__);

	seq_printf(m, "%d\n", abcct_2nd_cur_bat_chr_curr_limit);
	seq_printf(m, "abcct_2nd_cur_bat_chr_curr_limit %d\n",
					abcct_2nd_cur_bat_chr_curr_limit);

	seq_printf(m, "abcct_2nd_target_temp %ld\n", abcct_2nd_target_temp);
	seq_printf(m, "abcct_2nd_kp %ld\n", abcct_2nd_kp);
	seq_printf(m, "abcct_2nd_ki %ld\n", abcct_2nd_ki);
	seq_printf(m, "abcct_2nd_kd %ld\n", abcct_2nd_kd);
	seq_printf(m, "abcct_2nd_max_bat_chr_curr_limit %d\n",
					abcct_2nd_max_bat_chr_curr_limit);

	seq_printf(m, "abcct_2nd_min_bat_chr_curr_limit %d\n",
					abcct_2nd_min_bat_chr_curr_limit);

	return 0;
}

static int _cl_abcct_2nd_open(struct inode *inode, struct file *file)
{
	return single_open(file, _cl_abcct_2nd_read, PDE_DATA(inode));
}

static const struct file_operations _cl_abcct_2nd_fops = {
	.owner = THIS_MODULE,
	.open = _cl_abcct_2nd_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.write = _cl_abcct_2nd_write,
	.release = single_release,
};

static ssize_t _cl_abcct_2nd_lcmoff_write(
struct file *filp, const char __user *buf, size_t len, loff_t *data)
{
	/* int ret = 0; */
	char tmp[128] = { 0 };
	int _lcmoff_policy_enable;
	long _abcct_2nd_lcmoff_target_temp, _abcct_2nd_lcmoff_kp,
		_abcct_2nd_lcmoff_ki, _abcct_2nd_lcmoff_kd;

	int _max_cur, _min_cur;
	int scan_count = 0;

	len = (len < (128 - 1)) ? len : (128 - 1);
	/* write data to the buffer */
	if (copy_from_user(tmp, buf, len))
		return -EFAULT;

	if (data == NULL) {
		mtk_cooler_bcct_2nd_dprintk("%s null data\n", __func__);
		return -EINVAL;
	}

	scan_count =  sscanf(tmp, "%d %ld %ld %ld %ld %d %d"
			, &_lcmoff_policy_enable
			, &_abcct_2nd_lcmoff_target_temp, &_abcct_2nd_lcmoff_kp
			, &_abcct_2nd_lcmoff_ki, &_abcct_2nd_lcmoff_kd
			, &_max_cur, &_min_cur);

	if (scan_count >= 7) {
		x_chrlmt_lcmoff_policy_enable = _lcmoff_policy_enable;
		abcct_2nd_lcmoff_target_temp = _abcct_2nd_lcmoff_target_temp;
		abcct_2nd_lcmoff_kp = _abcct_2nd_lcmoff_kp;
		abcct_2nd_lcmoff_ki = _abcct_2nd_lcmoff_ki;
		abcct_2nd_lcmoff_kd = _abcct_2nd_lcmoff_kd;
		abcct_2nd_lcmoff_max_bat_chr_curr_limit = _max_cur;
		abcct_2nd_lcmoff_min_bat_chr_curr_limit = _min_cur;
		abcct_2nd_lcmoff_cur_bat_chr_curr_limit =
					abcct_2nd_lcmoff_max_bat_chr_curr_limit;

		abcct_2nd_lcmoff_iterm = 0;

		return len;
	}

	mtk_cooler_bcct_2nd_dprintk("%s bad argument\n", __func__);
	return -EINVAL;
}

static int _cl_abcct_2nd_lcmoff_read(struct seq_file *m, void *v)
{
	mtk_cooler_bcct_2nd_dprintk("%s\n", __func__);

	seq_printf(m, "x_chrlmt_lcmoff_policy_enable %d\n",
					x_chrlmt_lcmoff_policy_enable);

	seq_printf(m, "%d\n", abcct_2nd_lcmoff_cur_bat_chr_curr_limit);
	seq_printf(m, "abcct_2nd_lcmoff_cur_bat_chr_curr_limit %d\n",
				abcct_2nd_lcmoff_cur_bat_chr_curr_limit);

	seq_printf(m, "abcct_2nd_lcmoff_target_temp %ld\n",
				abcct_2nd_lcmoff_target_temp);

	seq_printf(m, "abcct_2nd_lcmoff_kp %ld\n", abcct_2nd_lcmoff_kp);
	seq_printf(m, "abcct_2nd_lcmoff_ki %ld\n", abcct_2nd_lcmoff_ki);
	seq_printf(m, "abcct_2nd_lcmoff_kd %ld\n", abcct_2nd_lcmoff_kd);
	seq_printf(m, "abcct_2nd_lcmoff_max_bat_chr_curr_limit %d\n",
				abcct_2nd_lcmoff_max_bat_chr_curr_limit);

	seq_printf(m, "abcct_2nd_lcmoff_min_bat_chr_curr_limit %d\n",
				abcct_2nd_lcmoff_min_bat_chr_curr_limit);

	return 0;
}

static int _cl_abcct_2nd_lcmoff_open(struct inode *inode, struct file *file)
{
	return single_open(file, _cl_abcct_2nd_lcmoff_read, PDE_DATA(inode));
}

static const struct file_operations _cl_abcct_2nd_lcmoff_fops = {
	.owner = THIS_MODULE,
	.open = _cl_abcct_2nd_lcmoff_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.write = _cl_abcct_2nd_lcmoff_write,
	.release = single_release,
};

static void bcct_2nd_lcmoff_switch(int onoff)
{
	mtk_cooler_bcct_2nd_dprintk("%s: onoff = %d\n", __func__, onoff);

	/* onoff = 0: LCM OFF */
	/* others: LCM ON */
	if (onoff) {
		/* deactivate lcmoff policy */
		x_chrlmt_is_lcmoff = 0;
	} else {
		/* activate lcmoff policy */
		x_chrlmt_is_lcmoff = 1;
	}
}

static int bcct_2nd_lcmoff_fb_notifier_callback(
struct notifier_block *self, unsigned long event, void *data)
{
	struct fb_event *evdata = data;
	int blank;

	/* skip if it's not a blank event */
	if ((event != FB_EVENT_BLANK) || (data == NULL))
		return 0;

	/* skip if policy is not enable */
	if (!x_chrlmt_lcmoff_policy_enable)
		return 0;

	blank = *(int *)evdata->data;
	mtk_cooler_bcct_2nd_dprintk("%s: blank = %d, event = %lu\n",
							__func__, blank, event);


	switch (blank) {
	/* LCM ON */
	case FB_BLANK_UNBLANK:
		bcct_2nd_lcmoff_switch(1);
		break;
	/* LCM OFF */
	case FB_BLANK_POWERDOWN:
		bcct_2nd_lcmoff_switch(0);
		break;
	default:
		break;
	}

	return 0;
}

static struct notifier_block bcct_2nd_lcmoff_fb_notifier = {
	.notifier_call = bcct_2nd_lcmoff_fb_notifier_callback,
};

static int _cl_x_chrlmt_read(struct seq_file *m, void *v)
{
	mtk_cooler_bcct_2nd_dprintk("%s\n", __func__);

	seq_printf(m, "%d,%d\n", x_chrlmt_chr_input_curr_limit,
						x_chrlmt_bat_chr_curr_limit);

	seq_printf(m, "x_chrlmt_chr_input_curr_limit %d\n",
						x_chrlmt_chr_input_curr_limit);

	seq_printf(m, "x_chrlmt_bat_chr_curr_limit %d\n",
						x_chrlmt_bat_chr_curr_limit);

	seq_printf(m, "abcct_2nd_cur_bat_chr_curr_limit %d\n",
					abcct_2nd_cur_bat_chr_curr_limit);

	seq_printf(m, "cl_bcct_2nd_cur_limit %d\n", cl_bcct_2nd_cur_limit);

	return 0;
}

static int _cl_x_chrlmt_open(struct inode *inode, struct file *file)
{
	return single_open(file, _cl_x_chrlmt_read, PDE_DATA(inode));
}

static const struct file_operations _cl_x_chrlmt_fops = {
	.owner = THIS_MODULE,
	.open = _cl_x_chrlmt_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

#ifdef BATTERY_INFO
static int _cl_battery_status_read(struct seq_file *m, void *v)
{
	mtk_cooler_bcct_2nd_dprintk("%s\n", __func__);

	seq_printf(m, "%d,%d\n", bat_info_mintchr, bat_info_maxtchr);

	return 0;
}

static int _cl_battery_status_open(struct inode *inode, struct file *file)
{
	return single_open(file, _cl_battery_status_read, PDE_DATA(inode));
}

static const struct file_operations _cl_battery_status_fops = {
	.owner = THIS_MODULE,
	.open = _cl_battery_status_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};
#endif

int mtk_cooler_is_abcct_2nd_unlimit(void)
{
	return (cl_abcct_2nd_state == 0 &&  cl_abcct_2nd_lcmoff_state == 0) ?
									1 : 0;
}
EXPORT_SYMBOL(mtk_cooler_is_abcct_2nd_unlimit);

#if (CONFIG_MTK_GAUGE_VERSION == 30)
static int mtkcooler_bcct_2nd_pdrv_probe(struct platform_device *pdev)
{
	mtk_cooler_bcct_2nd_dprintk_always("%s\n", __func__);
	pthermal_consumer = charger_manager_get_by_name(&pdev->dev, "charger1");

	return 0;
}

static int mtkcooler_bcct_2nd_pdrv_remove(struct platform_device *pdev)
{
	return 0;
}

struct platform_device mtk_cooler_bcct_2nd_device = {
	.name = "mtk-cooler-bcct_2nd",
	.id = -1,
};

static struct platform_driver mtk_cooler_bcct_2nd_driver = {
	.probe = mtkcooler_bcct_2nd_pdrv_probe,
	.remove = mtkcooler_bcct_2nd_pdrv_remove,
	.driver = {
		.name = "mtk-cooler-bcct_2nd",
		.owner  = THIS_MODULE,
	},
};
static int __init mtkcooler_bcct_2nd_late_init(void)
{
	int ret = 0;

	mtk_cooler_bcct_2nd_dprintk_always("%s\n", __func__);

	/* register platform device/driver */
	ret = platform_device_register(&mtk_cooler_bcct_2nd_device);
	if (ret) {
		mtk_cooler_bcct_2nd_dprintk_always(
				"fail to register device @ %s()\n", __func__);
		goto fail;
	}

	ret = platform_driver_register(&mtk_cooler_bcct_2nd_driver);
	if (ret) {
		mtk_cooler_bcct_2nd_dprintk_always(
				"fail to register driver @ %s()\n", __func__);
		goto reg_platform_driver_fail;
	}

	return ret;

reg_platform_driver_fail:
	platform_device_unregister(&mtk_cooler_bcct_2nd_device);

fail:
	return ret;
}
#endif

static int __init mtk_cooler_bcct_2nd_init(void)
{
	int err = 0;
	int i;

	for (i = MAX_NUM_INSTANCE_MTK_COOLER_BCCT_2ND; i-- > 0;) {
		cl_bcct_2nd_dev[i] = NULL;
		cl_bcct_2nd_state[i] = 0;
	}

	/* cl_bcct_2nd_dev = NULL; */

	mtk_cooler_bcct_2nd_dprintk("%s\n", __func__);

	err = mtk_cooler_bcct_2nd_register_ltf();
	if (err)
		goto err_unreg;

	err = mtk_cooler_abcct_2nd_register_ltf();
	if (err)
		goto err_unreg;

	err = mtk_cooler_abcct_2nd_lcmoff_register_ltf();
	if (err)
		goto err_unreg;

	if (fb_register_client(&bcct_2nd_lcmoff_fb_notifier)) {
		mtk_cooler_bcct_2nd_dprintk_always(
				"%s: register FB client failed!\n", __func__);
		err = -EINVAL;
		goto err_unreg;
	}

	/* create a proc file */
	{
		struct proc_dir_entry *entry = NULL;
		struct proc_dir_entry *dir_entry = NULL;

		dir_entry = mtk_thermal_get_proc_drv_therm_dir_entry();
		if (!dir_entry) {
			mtk_cooler_bcct_2nd_dprintk(
				"[%s]: mkdir /proc/driver/thermal failed\n",
					__func__);
		}

		entry = proc_create("clbcct_2nd", 0664, dir_entry,
							&_cl_bcct_2nd_fops);

		if (!entry)
			mtk_cooler_bcct_2nd_dprintk_always(
				"%s clbcct_2nd creation failed\n", __func__);
		else
			proc_set_user(entry, uid, gid);

		entry = proc_create("clabcct_2nd", 0664, dir_entry,
							&_cl_abcct_2nd_fops);

		if (!entry)
			mtk_cooler_bcct_2nd_dprintk_always(
				"%s clabcct_2nd creation failed\n", __func__);
		else
			proc_set_user(entry, uid, gid);

		entry = proc_create("clabcct_2nd_lcmoff", 0664,
					dir_entry, &_cl_abcct_2nd_lcmoff_fops);

		if (!entry)
			mtk_cooler_bcct_2nd_dprintk_always(
				"%s clabcct_2nd_lcmoff creation failed\n",
				__func__);
		else
			proc_set_user(entry, uid, gid);

		entry = proc_create("bcct_2ndlmt", 0444, NULL,
							&_cl_x_chrlmt_fops);

#ifdef BATTERY_INFO
		entry = proc_create("battery_status_x", 0444, NULL,
						&_cl_battery_status_fops);
#endif
	}

	bcct_2nd_chrlmt_queue = alloc_workqueue("bcct_2nd_chrlmt_work",
			WQ_UNBOUND | WQ_MEM_RECLAIM | WQ_HIGHPRI, 1);
	INIT_WORK(&bcct_2nd_chrlmt_work, x_chrlmt_set_limit_handler);

	return 0;

err_unreg:
	mtk_cooler_bcct_2nd_unregister_ltf();
	return err;
}

static void __exit mtk_cooler_bcct_2nd_exit(void)
{
	mtk_cooler_bcct_2nd_dprintk("%s\n", __func__);

	if (bcct_2nd_chrlmt_queue) {
		cancel_work_sync(&bcct_2nd_chrlmt_work);
		flush_workqueue(bcct_2nd_chrlmt_queue);
		destroy_workqueue(bcct_2nd_chrlmt_queue);
		bcct_2nd_chrlmt_queue = NULL;
	}

	/* remove the proc file */
	remove_proc_entry("driver/thermal/clbcct_2nd", NULL);
	remove_proc_entry("driver/thermal/clabcct_2nd", NULL);
	remove_proc_entry("driver/thermal/clabcct_2nd_lcmoff", NULL);

	mtk_cooler_bcct_2nd_unregister_ltf();
	mtk_cooler_abcct_2nd_unregister_ltf();
	mtk_cooler_abcct_2nd_lcmoff_unregister_ltf();

	fb_unregister_client(&bcct_2nd_lcmoff_fb_notifier);

#if (CONFIG_MTK_GAUGE_VERSION == 30)
	platform_driver_unregister(&mtk_cooler_bcct_2nd_driver);
	platform_device_unregister(&mtk_cooler_bcct_2nd_device);
#endif
}

module_init(mtk_cooler_bcct_2nd_init);
module_exit(mtk_cooler_bcct_2nd_exit);
#if (CONFIG_MTK_GAUGE_VERSION == 30)
late_initcall(mtkcooler_bcct_2nd_late_init);
#endif
