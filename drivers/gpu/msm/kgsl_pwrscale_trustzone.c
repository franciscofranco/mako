/* Copyright (c) 2010-2012, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include <mach/socinfo.h>
#include <mach/scm.h>
#include <linux/module.h>
#include <linux/jiffies.h>
#include <linux/hotplug.h>

#include "kgsl.h"
#include "kgsl_pwrscale.h"
#include "kgsl_device.h"

#define TZ_GOVERNOR_PERFORMANCE 0
#define TZ_GOVERNOR_ONDEMAND    1
#define TZ_GOVERNOR_INTERACTIVE	2

struct tz_priv {
	int governor;
	unsigned int no_switch_cnt;
	unsigned int skip_cnt;
	struct kgsl_power_stats bin;
};
spinlock_t tz_lock;

/* FLOOR is 5msec to capture up to 3 re-draws
 * per frame for 60fps content.
 */
#define FLOOR			5000
/* CEILING is 50msec, larger than any standard
 * frame length, but less than the idle timer.
 */
#define CEILING			50000

#define TZ_RESET_ID		0x3
#define TZ_UPDATE_ID		0x4

#if 0
#ifdef CONFIG_MSM_SCM
/* Trap into the TrustZone, and call funcs there. */
static int __secure_tz_entry(u32 cmd, u32 val, u32 id)
{
	int ret;
	spin_lock(&tz_lock);
	__iowmb();
	ret = scm_call_atomic2(SCM_SVC_IO, cmd, val, id);
	spin_unlock(&tz_lock);
	return ret;
}
#else
static int __secure_tz_entry(u32 cmd, u32 val, u32 id)
{
	return 0;
}
#endif /* CONFIG_MSM_SCM */
#endif

unsigned long window_time = 0, window_time1 = 0;
unsigned long sample_time_ms = 60;
unsigned int up_threshold = 60;
unsigned int down_threshold = 25;
unsigned int up_differential = 10;
/* extern var */
bool gpu_idle;
short idle_counter;

module_param(sample_time_ms, long, 0664);
module_param(up_threshold, int, 0664);
module_param(down_threshold, int, 0664);

struct clk_scaling_stats {
	unsigned long total_time_ms;
	unsigned long busy_time_ms;
	unsigned long threshold;	
};

static struct clk_scaling_stats gpu_stats;

static ssize_t tz_governor_show(struct kgsl_device *device,
				struct kgsl_pwrscale *pwrscale,
				char *buf)
{
	struct tz_priv *priv = pwrscale->priv;
	int ret;

	if (priv->governor == TZ_GOVERNOR_ONDEMAND)
		ret = snprintf(buf, 13, "interactive\n");
    else if (priv->governor == TZ_GOVERNOR_INTERACTIVE)
		ret = snprintf(buf, 13, "interactive\n");
	else
		ret = snprintf(buf, 13, "interactive\n");

	return ret;
}

static ssize_t tz_governor_store(struct kgsl_device *device,
				struct kgsl_pwrscale *pwrscale,
				 const char *buf, size_t count)
{
	char str[20];
	struct tz_priv *priv = pwrscale->priv;
	struct kgsl_pwrctrl *pwr = &device->pwrctrl;
	int ret;

	ret = sscanf(buf, "%20s", str);
	if (ret != 1)
		return -EINVAL;

	mutex_lock(&device->mutex);

	if (!strncmp(str, "ondemand", 8))
		priv->governor = TZ_GOVERNOR_INTERACTIVE;
    	else if (!strncmp(str, "interactive", 11))
		priv->governor = TZ_GOVERNOR_INTERACTIVE;
	else if (!strncmp(str, "performance", 11))
		priv->governor = TZ_GOVERNOR_INTERACTIVE;

	if (priv->governor == TZ_GOVERNOR_PERFORMANCE)
		kgsl_pwrctrl_pwrlevel_change(device, pwr->max_pwrlevel);

	mutex_unlock(&device->mutex);
	return count;
}

PWRSCALE_POLICY_ATTR(governor, 0644, tz_governor_show, tz_governor_store);

static struct attribute *tz_attrs[] = {
	&policy_attr_governor.attr,
	NULL
};

static struct attribute_group tz_attr_group = {
	.attrs = tz_attrs,
};

static void tz_wake(struct kgsl_device *device, struct kgsl_pwrscale *pwrscale)
{
	return;
}

#define HISTORY_SIZE 10
static unsigned int history[HISTORY_SIZE] = {0};
static unsigned int full_load = 0;
static unsigned short load_counter = 0;

static void __cpuinit tz_idle(struct kgsl_device *device, struct kgsl_pwrscale *pwrscale)
{
	struct kgsl_pwrctrl *pwr = &device->pwrctrl;
	struct tz_priv *priv = pwrscale->priv;
	struct kgsl_power_stats stats;
	unsigned long busy_time_ms = 0;
	unsigned int total = 0;
	
	if (!time_is_after_jiffies(window_time1 + msecs_to_jiffies(5)))
	{
		busy_time_ms = (u32)priv->bin.busy_time;
		
		full_load -= history[load_counter];
		history[load_counter] = (unsigned int)busy_time_ms;

		full_load += (unsigned int)busy_time_ms;

		if (unlikely(++load_counter >= HISTORY_SIZE))
			load_counter = 0;

		total = full_load / HISTORY_SIZE;

		if (pwr->active_pwrlevel == 3 && total < 4000)
		{
			if (idle_counter < 10)
				idle_counter += 1;
		}
		else if (idle_counter > 0)
		{
			idle_counter -= 2;
		}
		
		if (idle_counter >= 10)
		{
			gpu_idle = true;
		}
		else if (idle_counter <= 0)
		{
			if (gpu_idle && is_touching)
			{
				gpu_idle = false;
				touchboost_func();
			}
			else
			{
				gpu_idle = false;
			}
		}
		
		window_time1 = jiffies;
		
	/*	pr_info("---------------------------------");
		if(gpu_idle){pr_info("GPU IDLE");}
		else{pr_info("GPU BUSY");}
		pr_info("Current Load:\t\t%d",total);
		pr_info("Idle counter:\t\t%d",idle_counter);
		pr_info("---------------------------------");*/
	}
	
	/* In "performance" mode the clock speed always stays
	   the same */
	if (priv->governor == TZ_GOVERNOR_PERFORMANCE)
		return;
		
	device->ftbl->power_stats(device, &stats);
	priv->bin.total_time += stats.total_time;
	priv->bin.busy_time += stats.busy_time;

	if (time_is_after_jiffies(window_time + msecs_to_jiffies(sample_time_ms)))
		return;

	gpu_stats.total_time_ms = jiffies_to_msecs((long)jiffies - (long)window_time);
	gpu_stats.busy_time_ms = (u32)priv->bin.busy_time / USEC_PER_MSEC;

	/*
	 * Scale the up_threshold value based on the active_pwrlevel. We have
	 * 4 different levels:
	 * 3 = 128MHz
	 * 2 = 200MHz
	 * 1 = 320MHz
	 * 0 = 400MHz
	 *
	 * Making the up_threshold value lower if the active level is 2 or 3 will
	 * possibly improve smoothness while scrolling or open applications with
	 * a lot of images and what not. With a Full HD panel like Flo/Deb I could
	 * notice a few frame drops while this algorithm didn't scale past 128MHz
	 * on simple operations. This is fixed with up_threshold being scaled
	 */
	
	if (pwr->active_pwrlevel > 1)
		gpu_stats.threshold = (up_threshold / pwr->active_pwrlevel) + up_differential;
	else
		gpu_stats.threshold = up_threshold - up_differential;
		
	/*pr_info("---------------------------------");
	if(gpu_idle){pr_info("GPU IDLE");}
	else{pr_info("GPU BUSY");}
	pr_info("GPU frequency:\t%d\n", pwr->pwrlevels[pwr->active_pwrlevel].gpu_freq / 1000000);
	pr_info("(Cur Load) %ld = (Busy time) %ld * 100", gpu_stats.busy_time_ms * 100,
		gpu_stats.busy_time_ms);;
	pr_info("(Up Threshold) %ld = (Total time) %ld * (Threshold) %ld", 
		gpu_stats.total_time_ms * gpu_stats.threshold, gpu_stats.total_time_ms,
		gpu_stats.threshold);
	pr_info("(Down Threshold) %ld = (Total time) %ld * (Down Threshold) %d",
		gpu_stats.total_time_ms * down_threshold, gpu_stats.total_time_ms, 
		down_threshold);*/

	if ((gpu_stats.busy_time_ms * 100) > (gpu_stats.total_time_ms * gpu_stats.threshold))
	{
		if ((pwr->active_pwrlevel > 0) &&
			(pwr->active_pwrlevel <= (pwr->num_pwrlevels - 1)))
			kgsl_pwrctrl_pwrlevel_change(device,
					     pwr->active_pwrlevel - 1);
	}
	else if ((gpu_stats.busy_time_ms * 100) < (gpu_stats.total_time_ms * down_threshold))
	{
		if ((pwr->active_pwrlevel >= 0) &&
			(pwr->active_pwrlevel < (pwr->num_pwrlevels - 1)))
			kgsl_pwrctrl_pwrlevel_change(device,
					     pwr->active_pwrlevel + 1);
	}

	priv->bin.total_time = 0;
	priv->bin.busy_time = 0;
	window_time = jiffies;
}

static void tz_busy(struct kgsl_device *device,
	struct kgsl_pwrscale *pwrscale)
{
	device->on_time = ktime_to_us(ktime_get());
}

static void tz_sleep(struct kgsl_device *device,
	struct kgsl_pwrscale *pwrscale)
{
	struct tz_priv *priv = pwrscale->priv;

	gpu_idle = true;
	idle_counter = 10;

	/*
	 * We don't want the GPU to go to sleep if the busy_time_ms calculated on
	 * idle routine is not below down_threshold. This is just a measure of
	 * precaution
	 */
	if ((gpu_stats.busy_time_ms * 100) < 
			(gpu_stats.total_time_ms * down_threshold))
		kgsl_pwrctrl_pwrlevel_change(device, 3);

	priv->bin.total_time = 0;
	priv->bin.busy_time = 0;
	window_time = window_time1 = jiffies;
	return;
}

#ifdef CONFIG_MSM_SCM
static int tz_init(struct kgsl_device *device, struct kgsl_pwrscale *pwrscale)
{
	struct tz_priv *priv;

	gpu_stats.total_time_ms = 0;
	gpu_stats.busy_time_ms = 0;
	gpu_stats.threshold = 0;

	priv = pwrscale->priv = kzalloc(sizeof(struct tz_priv), GFP_KERNEL);
	if (pwrscale->priv == NULL)
		return -ENOMEM;

	priv->governor = TZ_GOVERNOR_INTERACTIVE;
	spin_lock_init(&tz_lock);
	kgsl_pwrscale_policy_add_files(device, pwrscale, &tz_attr_group);

	return 0;
}
#else
static int tz_init(struct kgsl_device *device, struct kgsl_pwrscale *pwrscale)
{
	return -EINVAL;
}
#endif /* CONFIG_MSM_SCM */

static void tz_close(struct kgsl_device *device, struct kgsl_pwrscale *pwrscale)
{
	kgsl_pwrscale_policy_remove_files(device, pwrscale, &tz_attr_group);
	kfree(pwrscale->priv);
	pwrscale->priv = NULL;
}

struct kgsl_pwrscale_policy kgsl_pwrscale_policy_tz = {
	.name = "trustzone",
	.init = tz_init,
	.busy = tz_busy,
	.idle = tz_idle,
	.sleep = tz_sleep,
	.wake = tz_wake,
	.close = tz_close
};
EXPORT_SYMBOL(kgsl_pwrscale_policy_tz);
