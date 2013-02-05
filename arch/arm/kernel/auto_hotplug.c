/* Copyright (c) 2012, Will Tisdale <willtisdale@gmail.com>. All rights reserved.
 *
 * Modified for Mako and Grouper, Francisco Franco <franciscofranco.1990@gmail.com>. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */

/*
 * Generic auto hotplug driver for ARM SoCs. Targeted at current generation
 * SoCs with dual and quad core applications processors.
 * Automatically hotplugs online and offline CPUs based on system load.
 * It is also capable of immediately onlining a core based on an external
 * event by calling void hotplug_boostpulse(void)
 *
 * Not recommended for use with OMAP4460 due to the potential for lockups
 * whilst hotplugging - locks up because the SoC requires the hardware to	
 * be hotpluged in a certain special order otherwise it will probably	
 * deadlock or simply trigger the watchdog and reboot. 
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/cpu.h>
#include <linux/workqueue.h>
#include <linux/sched.h>

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

/*
 * SAMPLING_PERIODS * SAMPLING_RATE is the minimum
 * load history which will be averaged
 */
#define SAMPLING_PERIODS 	10
#define INDEX_MAX_VALUE		(SAMPLING_PERIODS - 1)
/*
 * SAMPLING_RATE is scaled based on num_online_cpus()
 */
#define SAMPLING_RATE	100

/* Control flags */
static bool hotplug_disabled = false;
static bool hotplug_paused = false;
static bool boostpulse_active = false;
static bool earlysuspend_active = false;

static unsigned int enable_all_load_threshold __read_mostly = 425;
static unsigned int enable_load_threshold __read_mostly = 275;
static unsigned int disable_load_threshold __read_mostly = 125;
static bool quad_core_mode __read_mostly = false;
static bool hotplug_routines __read_mostly = true;

module_param(enable_all_load_threshold, int, 0775);
module_param(enable_load_threshold, int, 0775);
module_param(disable_load_threshold, int, 0775);
module_param(quad_core_mode, bool, 0755);

struct delayed_work hotplug_decision_work;
struct delayed_work hotplug_unpause_work;
struct work_struct hotplug_online_single_work;
struct delayed_work hotplug_offline_work;
struct work_struct hotplug_boost_online_work;
struct work_struct hotplug_online_all_work;
struct work_struct hotplug_offline_all_work;

static unsigned int history[SAMPLING_PERIODS];
static unsigned int index;

static void hotplug_decision_work_fn(struct work_struct *work)
{
	unsigned int running, disable_load, enable_load, avg_running = 0;
	unsigned int online_cpus, available_cpus, i, j;
	int cpu;

	online_cpus = num_online_cpus();
	available_cpus = 4;
	disable_load = disable_load_threshold * online_cpus;
	enable_load = enable_load_threshold * online_cpus;
	running = nr_running() * 100;

	for_each_online_cpu(cpu) {
		history[index] = running;
		if (unlikely(index++ == INDEX_MAX_VALUE))
			index = 0;
	}

	/*
	 * Use a circular buffer to calculate the average load
	 * over the sampling periods.
	 * This will absorb load spikes of short duration where
	 * we don't want additional cores to be onlined because
	 * the cpufreq driver should take care of those load spikes.
	 */
	for (i = 0, j = index; i < SAMPLING_PERIODS; i++, j--) {
		avg_running += history[j];
		if (unlikely(j == 0))
			j = INDEX_MAX_VALUE;
	}

	/*
	 * If we are at the end of the buffer, return to the beginning.
	 */
	if (unlikely(index++ == INDEX_MAX_VALUE))
		index = 0;

	avg_running = avg_running / SAMPLING_PERIODS;

	if (!hotplug_disabled) {
		if (avg_running > enable_all_load_threshold && online_cpus < available_cpus) {
			//pr_info("auto_hotplug: Onlining all CPUs, avg running: %d\n", avg_running);
			/*
			 * Flush any delayed offlining work from the workqueue.
			 * No point in having expensive unnecessary hotplug transitions.
			 * We still online after flushing, because load is high enough to
			 * warrant it.
			 * We set the paused flag so the sampling can continue but no more
			 * hotplug events will occur.
			 */
			hotplug_paused = true;
			if (delayed_work_pending(&hotplug_offline_work))
				cancel_delayed_work(&hotplug_offline_work);
			schedule_work(&hotplug_online_all_work);
			return;
		} else if (hotplug_paused) {
			schedule_delayed_work_on(0, &hotplug_decision_work, SAMPLING_RATE);
			return;
		} else if ((avg_running >= enable_load) && (online_cpus < available_cpus)) {
			if (delayed_work_pending(&hotplug_offline_work))
				cancel_delayed_work(&hotplug_offline_work);
			schedule_work(&hotplug_online_single_work);
			return;
		} else if (avg_running < disable_load && online_cpus > 1) {
			/* Only queue a cpu_down() if there isn't one already pending */
			if(boostpulse_active) {
				boostpulse_active = false;
			} else if (!(delayed_work_pending(&hotplug_offline_work)) && !boostpulse_active) {
				schedule_delayed_work_on(0, &hotplug_offline_work, HZ);
			}
		}
	}

	schedule_delayed_work_on(0, &hotplug_decision_work, SAMPLING_RATE);
}

static void online_cpu_nr(int cpu)
{
	int ret;
	
	ret = cpu_up(cpu);
	if (ret)
		pr_info("Error %d online core %d\n", ret, cpu);
}

static void offline_cpu_nr(int cpu)
{
	int ret;
	
	ret = cpu_down(cpu);
	if (ret)
		pr_info("Error %d offline core %d\n", ret, cpu);
}

static void hotplug_online_all_work_fn(struct work_struct *work)
{
	if (hotplug_routines) {
		online_cpu_nr(1);
		online_cpu_nr(2);
		online_cpu_nr(3);
		
		//pause for 2 seconds before continuing
		schedule_delayed_work(&hotplug_unpause_work, HZ * 2);
		schedule_delayed_work_on(0, &hotplug_decision_work, SAMPLING_RATE);
		return;
	}
	
	online_cpu_nr(1);
	pr_info("auto_hotplug: CPU%d online.\n", 1);
	
	if (quad_core_mode) {
		online_cpu_nr(2);
		pr_info("auto_hotplug: CPU%d online.\n", 2);
		online_cpu_nr(3);
		pr_info("auto_hotplug: CPU%d online.\n", 3);
	}
}

static void hotplug_offline_all_work_fn(struct work_struct *work)
{
	int cpu;
	for_each_possible_cpu(cpu) {
		if (likely(cpu_online(cpu) && (cpu))) {
			offline_cpu_nr(cpu);
			pr_info("auto_hotplug: CPU%d down.\n", cpu);
		}
	}
}

static void hotplug_online_single_work_fn(struct work_struct *work)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		if (cpu) {
			if (!cpu_online(cpu)) {
				online_cpu_nr(cpu);
				break;
			}
		}
	}
	schedule_delayed_work_on(0, &hotplug_decision_work, (HZ/2));
}

static void hotplug_offline_single_work_fn(struct work_struct *work)
{
	int cpu;
	for_each_online_cpu(cpu) {
		if (cpu) {
			offline_cpu_nr(cpu);
			break;
		}
	}
	schedule_delayed_work_on(0, &hotplug_decision_work, HZ);
}

static void hotplug_unpause_work_fn(struct work_struct *work)
{
	if (hotplug_paused)
		hotplug_paused = false;
}

inline void hotplug_boostpulse(void)
{
	if (earlysuspend_active && hotplug_disabled)
		return;

	if (!boostpulse_active) {
		boostpulse_active = true;
		/*
		 * If there are less than 2 CPUs online, then online
		 * an additional CPU, otherwise check for any pending
		 * offlines, cancel them and pause for 2 seconds.
		 * Either way, we don't allow any cpu_down()
		 * whilst the user is interacting with the device.
		 */
		if (num_online_cpus() < 2) {
			cancel_delayed_work_sync(&hotplug_offline_work);
			hotplug_paused = true;
			schedule_work(&hotplug_online_single_work);
			schedule_delayed_work(&hotplug_unpause_work, HZ * 2);
		} else {
			if (delayed_work_pending(&hotplug_offline_work)) {
				cancel_delayed_work(&hotplug_offline_work);
				hotplug_paused = true;
				schedule_delayed_work(&hotplug_unpause_work, HZ);
				schedule_delayed_work_on(0, &hotplug_decision_work, HZ);
			}
		}
	}
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void auto_hotplug_early_suspend(struct early_suspend *handler)
{
	earlysuspend_active = true;
	
	if (hotplug_routines) {
		cancel_delayed_work_sync(&hotplug_offline_work);
    	cancel_delayed_work_sync(&hotplug_decision_work);
	}
	
    if (num_online_cpus() > 1) {
    	pr_info("auto_hotplug: Offlining CPUs for early suspend\n");
        schedule_work_on(0, &hotplug_offline_all_work);
	}
}

static void auto_hotplug_late_resume(struct early_suspend *handler)
{
	earlysuspend_active = false;
	
	schedule_work_on(0, &hotplug_online_all_work);
}

static struct early_suspend auto_hotplug_suspend = {
	.suspend = auto_hotplug_early_suspend,
	.resume = auto_hotplug_late_resume,
};
#endif /* CONFIG_HAS_EARLYSUSPEND */

int __init auto_hotplug_init(void)
{
	pr_info("auto_hotplug: v0.220 by _thalamus\n");
	pr_info("auto_hotplug: %d CPUs detected\n", 4);

	INIT_WORK(&hotplug_online_all_work, hotplug_online_all_work_fn);
	INIT_WORK(&hotplug_offline_all_work, hotplug_offline_all_work_fn);
	INIT_DELAYED_WORK(&hotplug_decision_work, hotplug_decision_work_fn);
	INIT_DELAYED_WORK_DEFERRABLE(&hotplug_unpause_work, hotplug_unpause_work_fn);
	INIT_WORK(&hotplug_online_single_work, hotplug_online_single_work_fn);
	INIT_DELAYED_WORK_DEFERRABLE(&hotplug_offline_work, hotplug_offline_single_work_fn);
	
	hotplug_paused = true;

#ifdef CONFIG_HAS_EARLYSUSPEND
	register_early_suspend(&auto_hotplug_suspend);
#endif
	return 0;
}
late_initcall(auto_hotplug_init);