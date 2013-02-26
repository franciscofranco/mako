/* Copyright (c) 2012-2013, All rights reserved.
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

#define SAMPLES 10

static unsigned int enable_all_load_threshold __read_mostly = 325;
static unsigned int enable_load_threshold __read_mostly = 200;
static unsigned int disable_load_threshold __read_mostly = 125;
static bool quad_core_mode __read_mostly = false;
static bool hotplug_routines __read_mostly = true;
static unsigned int sampling_timer __read_mostly = 100;

module_param(enable_all_load_threshold, int, 0775);
module_param(enable_load_threshold, int, 0775);
module_param(disable_load_threshold, int, 0775);
module_param(quad_core_mode, bool, 0755);
module_param(hotplug_routines, bool, 0755);
module_param(sampling_timer, int, 0755);

struct delayed_work hotplug_decision_work;
struct work_struct hotplug_online_single_work;
struct delayed_work hotplug_offline_work;
//struct work_struct hotplug_boost_online_work;
struct work_struct no_hotplug_online_all_work;
struct work_struct hotplug_online_all_work;
struct work_struct hotplug_offline_all_work;

unsigned int timer_history[SAMPLES];
unsigned int foo;
//unsigned int count;

static void hotplug_decision_work_fn(struct work_struct *work)
{
	static unsigned int total = 0;
	unsigned int disable_load, enable_load, available_cpus;
	unsigned int online_cpus;
	int avg_running, i;

	online_cpus = num_online_cpus();
	available_cpus = num_possible_cpus();
	disable_load = disable_load_threshold * online_cpus;
	enable_load = enable_load_threshold * online_cpus;

	if (foo++ == SAMPLES)
		foo = 0;

	/* 
	 * This is a custom function from Codeaurora to calculate the average of the runnable threads
	 * and it doesn't seem to be very expensive so its worth a try. sched_get_nr_running_avg running 
	 * on this kernel is modified from original code, the iowait calculations were removed because 
	 * for the purpose of this driver we don't use that value and may cause extra overhead. 
	 *
	 * This function call has a 0ms to 1ms cost, roughly 200k nanoseconds - measured with ktime_get()
	 */

	sched_get_nr_running_avg(&avg_running);
	timer_history[foo] = avg_running;

	for (i = 0; i < ARRAY_SIZE(timer_history); i++)
		total += timer_history[i];

	total = total/SAMPLES;
	//pr_info("hotplug_ decision: total: %d\n", total);

	
	if ((total > enable_all_load_threshold) && (online_cpus < available_cpus)) {

		schedule_work(&hotplug_online_all_work);
		
		return;
	} else if ((total >= enable_load) && (online_cpus < available_cpus)) {

		schedule_work(&hotplug_online_single_work);
				
		return;
	} else if ((total < disable_load) && (online_cpus > 2)) {
		//if (boostpulse_active) {
		//	boostpulse_active = false;
		//} else if (online_cpus > 2) {
			
		/* 
		 * This count serves to filter any spurious lower load as we don't want the driver
		 * to offline a core during a intense task if for some reason it reports a low
		 * load in one sample time. This can be called a sampling rate.
		 */ 
		schedule_work(&hotplug_offline_all_work);
		//}
	}

	schedule_delayed_work(&hotplug_decision_work, msecs_to_jiffies(sampling_timer));
}

/*
 * This function is only called on late_resume if the hotplug_routines flag is false
 */
static void __cpuinit no_hotplug_online_all_work_fn(struct work_struct *work)
{
	int cpu;

	if (!hotplug_routines) {
		if (quad_core_mode) {
			for_each_possible_cpu(cpu) {
				if (cpu) {
					if (!cpu_online(cpu))
						cpu_up(cpu);
				}
			}
		}
	}
}

static void __cpuinit hotplug_online_all_work_fn(struct work_struct *work)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		if (cpu) {
			if (!cpu_online(cpu))
				cpu_up(cpu);
		}
	}

	schedule_delayed_work(&hotplug_decision_work, msecs_to_jiffies(sampling_timer));
}

static void hotplug_offline_all_work_fn(struct work_struct *work)
{
	int cpu;
	
	/* 
	 * Offlining backwards to allow cpu0 and cpu1 to be online 
	 * instead of cpu0 and cpu3 as I think it might have been 
	 * conflicting with some of the routines in this driver 
	 */
	for (cpu = 3; cpu > 1; cpu--) {
		if (likely(cpu_online(cpu) && (cpu))) {
			cpu_down(cpu);
		}
	}
}

static void __cpuinit hotplug_online_single_work_fn(struct work_struct *work)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		if (cpu) {
			if (!cpu_online(cpu)) {
				cpu_up(cpu);
				break;
			}
		}
	}
	schedule_delayed_work(&hotplug_decision_work, msecs_to_jiffies(sampling_timer));
}

#if 0
static void hotplug_offline_single_work_fn(struct work_struct *work)
{
	int cpu;
	
	/* 
	 * Offlining backwards to allow cpu0 and cpu1 to be online 
	 * instead of cpu0 and cpu3 as I think it might have been 
	 * conflicting with some of the routines in this driver 
	 */
	for (cpu = 3; cpu > 1; cpu--) {
		if (likely(cpu_online(cpu) && (cpu))) {
			offline_cpu_nr(cpu);
			break;
		}
	}
	schedule_delayed_work_on(0, &hotplug_decision_work, SAMPLING_RATE/num_online_cpus());
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
		} else {
			if (delayed_work_pending(&hotplug_offline_work)) {
				cancel_delayed_work(&hotplug_offline_work);
				hotplug_paused = true;
				schedule_delayed_work_on(0, &hotplug_decision_work, SAMPLING_RATE/num_online_cpus());
			}
		}
	}
}
#endif //if 0

#ifdef CONFIG_HAS_EARLYSUSPEND
static void auto_hotplug_early_suspend(struct early_suspend *handler)
{	
    cancel_delayed_work(&hotplug_decision_work);
	
    if (num_online_cpus() > 1) {
    	pr_info("auto_hotplug: Offlining CPUs for early suspend\n");
        schedule_work(&hotplug_offline_all_work);
	}
}

static void __cpuinit auto_hotplug_late_resume(struct early_suspend *handler)
{	
	if (hotplug_routines)
		schedule_delayed_work(&hotplug_decision_work, msecs_to_jiffies(sampling_timer));
	else
		schedule_work(&no_hotplug_online_all_work);
}

static struct early_suspend auto_hotplug_suspend = {
	.suspend = auto_hotplug_early_suspend,
	.resume = auto_hotplug_late_resume,
};
#endif /* CONFIG_HAS_EARLYSUSPEND */

int __init auto_hotplug_init(void)
{
	pr_info("auto_hotplug: v1.0\n");
	pr_info("Modified by: Francisco Franco\n");
	pr_info("auto_hotplug: %d CPUs detected\n", num_possible_cpus());

	INIT_DELAYED_WORK(&hotplug_decision_work, hotplug_decision_work_fn);
	INIT_WORK(&no_hotplug_online_all_work, no_hotplug_online_all_work_fn);
	INIT_WORK(&hotplug_online_all_work, hotplug_online_all_work_fn);
	INIT_WORK(&hotplug_offline_all_work, hotplug_offline_all_work_fn);
	INIT_WORK(&hotplug_online_single_work, hotplug_online_single_work_fn);
	INIT_DELAYED_WORK_DEFERRABLE(&hotplug_offline_work, hotplug_offline_single_work_fn);
	
	/*
	 * The usual 20 seconds wait before starting the hotplug work
	 */
	if (hotplug_routines)
		schedule_delayed_work(&hotplug_decision_work, HZ*20);

#ifdef CONFIG_HAS_EARLYSUSPEND
	register_early_suspend(&auto_hotplug_suspend);
#endif

	return 0;
}
late_initcall(auto_hotplug_init);
