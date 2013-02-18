/* Copyright (c) 2012-2013, Will Tisdale <willtisdale@gmail.com>. All rights reserved.
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

/* Control flags */
static bool hotplug_disabled = false;
static bool hotplug_paused = false;
//static bool boostpulse_active = false;
static bool earlysuspend_active = false;

static unsigned int enable_all_load_threshold __read_mostly = 375;
static unsigned int enable_load_threshold __read_mostly = 275;
static unsigned int disable_load_threshold __read_mostly = 125;
static bool quad_core_mode __read_mostly = false;
static bool hotplug_routines __read_mostly = true;
static unsigned int sampling_rate __read_mostly = 10;

module_param(enable_all_load_threshold, int, 0775);
module_param(enable_load_threshold, int, 0775);
module_param(disable_load_threshold, int, 0775);
module_param(quad_core_mode, bool, 0755);
module_param(hotplug_routines, bool, 0755);
module_param(sampling_rate, int, 0755);

struct delayed_work hotplug_decision_work;
struct work_struct hotplug_online_single_work;
struct delayed_work hotplug_offline_work;
//struct work_struct hotplug_boost_online_work;
struct work_struct hotplug_online_all_work;
struct work_struct hotplug_offline_all_work;

unsigned int count;

static void hotplug_decision_work_fn(struct work_struct *work)
{
	unsigned int disable_load, enable_load, available_cpus;
	unsigned int online_cpus;
	int avg_running;

	online_cpus = num_online_cpus();
	available_cpus = 4;
	disable_load = disable_load_threshold * online_cpus;
	enable_load = enable_load_threshold * online_cpus;

	/* 
	 * This is a custom function from Codeaurora to calculate the average of the runnable threads
	 * and it doesn't seem to be very expensive so its worth a try. sched_get_nr_running_avg running 
	 * on this kernel is modified from original code, the iowait calculations were removed because 
	 * for the purpose of this driver we don't use that value and may cause extra overhead. 
	 */
	sched_get_nr_running_avg(&avg_running);
	
	if (!hotplug_disabled) {
		if (hotplug_paused) {
			schedule_delayed_work_on(0, &hotplug_decision_work, 10);
			
			hotplug_paused = false;
			count = 0;
			
			return;
		} else if (avg_running > enable_all_load_threshold) {
			
			/* 
			 * Paused flag is set to true here because after all cores are online 
			 * we wait 1 sample time before hotplugging again based on the load
			 * changes.
			 * 
			 * TODO: instead of setting a paused flag maybe make a user exported
			 * sample timer to force the hotplugging to be paused X times the sample
			 * timer value
			 */
			hotplug_paused = true;
			
			if (work_pending(&hotplug_offline_all_work))
				cancel_work_sync(&hotplug_offline_all_work);
				
			if (online_cpus < 4)
				schedule_work_on(0, &hotplug_online_all_work);
			else
				schedule_delayed_work_on(0, &hotplug_decision_work, 10);
				
			count = 0;
			
			return;
		} else if ((avg_running >= enable_load) && (online_cpus < available_cpus)) {
			if (work_pending(&hotplug_offline_all_work))
				cancel_work_sync(&hotplug_offline_all_work);
			schedule_work_on(0, &hotplug_online_single_work);
			
			count = 0;
			
			return;
		} else if (avg_running < disable_load && online_cpus > 2) {
			//if (boostpulse_active) {
			//	boostpulse_active = false;
			//} else if (online_cpus > 2) {
				
			/* 
			 * This count serves to filter any spurious lower load as we don't want the driver
			 * to offline a core during a intense task if for some reason it reports a low
			 * load in one sample time. This can be called a sampling rate.
			 */ 
			if (count++ == sampling_rate) {
				schedule_work_on(0, &hotplug_offline_all_work);
				
				count = 0;
			}
			//}
		}
	}

	schedule_delayed_work_on(0, &hotplug_decision_work, 10);
}

static void online_cpu_nr(int cpu)
{
	int ret;
		
	ret = cpu_up(cpu);
	pr_info("auto_hotplug: CPU%d online.\n", cpu);
	if (ret)
		pr_info("Error %d online core %d\n", ret, cpu);
}

static void offline_cpu_nr(int cpu)
{
	int ret;
		
	ret = cpu_down(cpu);
	pr_info("auto_hotplug: CPU%d down.\n", cpu);
	if (ret)
		pr_info("Error %d offline core %d\n", ret, cpu);
}

static void __cpuinit hotplug_online_all_work_fn(struct work_struct *work)
{
	if (hotplug_routines) {
		if (!cpu_online(1))
			online_cpu_nr(1);			
		if (!cpu_online(2))
			online_cpu_nr(2);
		if (!cpu_online(3)) 
			online_cpu_nr(3);
		
		schedule_delayed_work_on(0, &hotplug_decision_work, 10);
		return;
	} else {
		if (!cpu_online(1))
			online_cpu_nr(1);
	
		if (quad_core_mode) {
			if (!cpu_online(2))
				online_cpu_nr(2);
			if (!cpu_online(3))
				online_cpu_nr(3);
		}
	}
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
			offline_cpu_nr(cpu);
		}
	}
}

static void __cpuinit hotplug_online_single_work_fn(struct work_struct *work)
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
	schedule_delayed_work_on(0, &hotplug_decision_work, 10);
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
	earlysuspend_active = true;
	
	if (hotplug_routines) {
		cancel_work_sync(&hotplug_offline_all_work);
    	cancel_delayed_work_sync(&hotplug_decision_work);
	}
	
    if (num_online_cpus() > 1) {
    	pr_info("auto_hotplug: Offlining CPUs for early suspend\n");
        schedule_work_on(0, &hotplug_offline_all_work);
	}
}

static void __cpuinit auto_hotplug_late_resume(struct early_suspend *handler)
{
	earlysuspend_active = false;
	
	if (hotplug_routines) {
		if (!cpu_online(1))
			online_cpu_nr(1);
		schedule_delayed_work_on(0, &hotplug_decision_work, 10);
	} else
		schedule_work_on(0, &hotplug_online_all_work);
}

static struct early_suspend auto_hotplug_suspend = {
	.suspend = auto_hotplug_early_suspend,
	.resume = auto_hotplug_late_resume,
};
#endif /* CONFIG_HAS_EARLYSUSPEND */

int __init auto_hotplug_init(void)
{
	pr_info("auto_hotplug: v1.0\n");
	pr_info("Author: _thalamus\n");
	pr_info("Modified by: Francisco Franco\n");
	pr_info("auto_hotplug: %d CPUs detected\n", 4);

	INIT_DELAYED_WORK(&hotplug_decision_work, hotplug_decision_work_fn);
	INIT_WORK(&hotplug_online_all_work, hotplug_online_all_work_fn);
	INIT_WORK(&hotplug_offline_all_work, hotplug_offline_all_work_fn);
	INIT_WORK(&hotplug_online_single_work, hotplug_online_single_work_fn);
	INIT_DELAYED_WORK_DEFERRABLE(&hotplug_offline_work, hotplug_offline_single_work_fn);
	
	/*
	 * The usual 20 seconds wait before starting the hotplug work
	 */
	if (hotplug_routines)
		schedule_delayed_work_on(0, &hotplug_decision_work, HZ*20);

#ifdef CONFIG_HAS_EARLYSUSPEND
	register_early_suspend(&auto_hotplug_suspend);
#endif

	return 0;
}
late_initcall(auto_hotplug_init);
