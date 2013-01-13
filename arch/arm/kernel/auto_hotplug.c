/* Copyright (c) 2012, Will Tisdale <willtisdale@gmail.com>. All rights reserved.
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
 * whilst hotplugging.
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
 * Enable debug output to dump the average
 * calculations and ring buffer array values
 * WARNING: Enabling this causes a ton of overhead
 *
 * FIXME: Turn it into debugfs stats (somehow)
 * because currently it is a sack of shit.
 */
#define DEBUG 0

/*
 * SAMPLING_PERIODS * SAMPLING_RATE is the minimum
 * load history which will be averaged
 */
#define SAMPLING_PERIODS 	20
#define INDEX_MAX_VALUE		(SAMPLING_PERIODS - 1)
/*
 * SAMPLING_RATE is scaled based on num_online_cpus()
 */
#define SAMPLING_RATE	100

/* Control flags */
unsigned char flags;
#define HOTPLUG_DISABLED	(1 << 0)
#define HOTPLUG_PAUSED		(1 << 1)
#define BOOSTPULSE_ACTIVE	(1 << 2)
#define EARLYSUSPEND_ACTIVE	(1 << 3)

/*
 * Load defines:
 * ENABLE_ALL is a high watermark to rapidly online all CPUs
 *
 * ENABLE is the load which is required to enable 1 extra CPU
 * DISABLE is the load at which a CPU is disabled
 * These two are scaled based on num_online_cpus()
 */

static unsigned int enable_all_load_threshold __read_mostly = 375;
static unsigned int enable_load_threshold __read_mostly = 275;
static unsigned int disable_load_threshold __read_mostly = 125;

module_param(enable_all_load_threshold, int, 0775);
module_param(enable_load_threshold, int, 0775);
module_param(disable_load_threshold, int, 0775);

struct delayed_work hotplug_decision_work;
struct delayed_work hotplug_unpause_work;
struct work_struct hotplug_online_all_work;
struct work_struct hotplug_online_single_work;
struct delayed_work hotplug_offline_work;
struct work_struct hotplug_offline_all_work;
struct work_struct hotplug_boost_online_work;

static unsigned int history[SAMPLING_PERIODS];
static unsigned int index;

static void hotplug_decision_work_fn(struct work_struct *work)
{
	unsigned int running, disable_load, enable_load, avg_running = 0;
	unsigned int online_cpus, available_cpus, i, j;
#if DEBUG
	unsigned int k;
#endif

	online_cpus = num_online_cpus();
	available_cpus = 4;
	disable_load = disable_load_threshold * online_cpus;
	enable_load = enable_load_threshold * online_cpus;

	running = nr_running() * 100;
	history[index] = running;

#if DEBUG
	pr_info("online_cpus is: %d\n", online_cpus);
	pr_info("enable_load is: %d\n", enable_load);
	pr_info("disable_load is: %d\n", disable_load);
	pr_info("index is: %d\n", index);
	pr_info("running is: %d\n", running);
#endif

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

#if DEBUG
	pr_info("array contents: ");
	for (k = 0; k < SAMPLING_PERIODS; k++) {
		 pr_info("%d: %d\t",k, history[k]);
	}
	pr_info("\n");
	pr_info("avg_running before division: %d\n", avg_running);
#endif

	avg_running = avg_running / SAMPLING_PERIODS;

#if DEBUG
	pr_info("average_running is: %d\n", avg_running);
#endif

	if (!(flags & HOTPLUG_DISABLED)) {
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
			flags |= HOTPLUG_PAUSED;
			if (delayed_work_pending(&hotplug_offline_work))
				cancel_delayed_work(&hotplug_offline_work);
			schedule_work(&hotplug_online_all_work);
			return;
		} else if (flags & HOTPLUG_PAUSED) {
			schedule_delayed_work_on(0, &hotplug_decision_work, SAMPLING_RATE);
			return;
		} else if ((avg_running >= enable_load) && (online_cpus < available_cpus)) {
			//pr_info("auto_hotplug: Onlining single CPU, avg running: %d\n", avg_running);
			if (delayed_work_pending(&hotplug_offline_work))
				cancel_delayed_work(&hotplug_offline_work);
			schedule_work(&hotplug_online_single_work);
			return;
		} else if (avg_running < disable_load && online_cpus > 1) {
			/* Only queue a cpu_down() if there isn't one already pending */
			if(flags & BOOSTPULSE_ACTIVE) {
				flags &= ~BOOSTPULSE_ACTIVE;
				//pr_info("auto_hotplug: Clearing boostpulse flags\n");
			} else if (!(delayed_work_pending(&hotplug_offline_work)) && !(flags & BOOSTPULSE_ACTIVE)) {
				//pr_info("auto_hotplug: Offlining CPU, avg running: %d\n", avg_running);
				schedule_delayed_work_on(0, &hotplug_offline_work, HZ);
			}
		}
	}
	
#if DEBUG
	pr_info("sampling_rate is: %d\n", jiffies_to_msecs(SAMPLING_RATE));
#endif
	schedule_delayed_work_on(0, &hotplug_decision_work, SAMPLING_RATE);
}

static void hotplug_online_all_work_fn(struct work_struct *work)
{
	int cpu;
	for_each_possible_cpu(cpu) {
		if (likely(!cpu_online(cpu))) {
			cpu_up(cpu);
			pr_info("auto_hotplug: CPU%d up.\n", cpu);
		}
	}
	/*
	 * Pause for 2 seconds before even considering offlining a CPU
	 */
	schedule_delayed_work(&hotplug_unpause_work, HZ * 2);
	schedule_delayed_work_on(0, &hotplug_decision_work, SAMPLING_RATE);
}

static void hotplug_offline_all_work_fn(struct work_struct *work)
{
	int cpu;
	for_each_possible_cpu(cpu) {
		if (likely(cpu_online(cpu) && (cpu))) {
			cpu_down(cpu);
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
				cpu_up(cpu);
				pr_info("auto_hotplug: CPU%d up.\n", cpu);
				break;
			}
		}
	}
	schedule_delayed_work_on(0, &hotplug_decision_work, SAMPLING_RATE);
}

static void hotplug_offline_work_fn(struct work_struct *work)
{
	int cpu;
	for_each_online_cpu(cpu) {
		if (cpu) {
			cpu_down(cpu);
			pr_info("auto_hotplug: CPU%d down.\n", cpu);
			break;
		}
	}
	schedule_delayed_work_on(0, &hotplug_decision_work, SAMPLING_RATE);
}

static void hotplug_unpause_work_fn(struct work_struct *work)
{
	//pr_info("auto_hotplug: Clearing pause flag\n");
	flags &= ~HOTPLUG_PAUSED;
}

inline void hotplug_boostpulse(void)
{
	if (unlikely(flags & (EARLYSUSPEND_ACTIVE
		| HOTPLUG_DISABLED)))
		return;

	if (!(flags & BOOSTPULSE_ACTIVE)) {
		flags |= BOOSTPULSE_ACTIVE;
		/*
		 * If there are less than 2 CPUs online, then online
		 * an additional CPU, otherwise check for any pending
		 * offlines, cancel them and pause for 2 seconds.
		 * Either way, we don't allow any cpu_down()
		 * whilst the user is interacting with the device.
		 */
		if (likely(num_online_cpus() < 2)) {
			//pr_info("User is interacting with the device, make sure 2 CPU's are active.\n");
			cancel_delayed_work_sync(&hotplug_offline_work);
			flags |= HOTPLUG_PAUSED;
			schedule_work(&hotplug_online_single_work);
			schedule_delayed_work(&hotplug_unpause_work, HZ * 2);
		} else {
			if (delayed_work_pending(&hotplug_offline_work)) {
				cancel_delayed_work(&hotplug_offline_work);
				flags |= HOTPLUG_PAUSED;
				//pr_info("Device is boostpulsed, skip any type of work.\n");
				schedule_delayed_work(&hotplug_unpause_work, HZ * 2);
				schedule_delayed_work_on(0, &hotplug_decision_work, SAMPLING_RATE);
			}
		}
	}
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void auto_hotplug_early_suspend(struct early_suspend *handler)
{
	//pr_info("auto_hotplug: early suspend handler\n");
	flags |= EARLYSUSPEND_ACTIVE;
 
	/* Cancel all scheduled delayed work to avoid races */
    cancel_delayed_work_sync(&hotplug_offline_work);
    cancel_delayed_work_sync(&hotplug_decision_work);
    if (num_online_cpus() > 1) {
    	pr_info("auto_hotplug: Offlining CPUs for early suspend\n");
        schedule_work_on(0, &hotplug_offline_all_work);
	}
}

static void auto_hotplug_late_resume(struct early_suspend *handler)
{
	//pr_info("auto_hotplug: late resume handler\n");
	flags &= ~EARLYSUSPEND_ACTIVE;
 
	schedule_delayed_work_on(0, &hotplug_decision_work, SAMPLING_RATE);
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

	INIT_DELAYED_WORK(&hotplug_decision_work, hotplug_decision_work_fn);
	INIT_DELAYED_WORK_DEFERRABLE(&hotplug_unpause_work, hotplug_unpause_work_fn);
	INIT_WORK(&hotplug_online_all_work, hotplug_online_all_work_fn);
	INIT_WORK(&hotplug_online_single_work, hotplug_online_single_work_fn);
	INIT_WORK(&hotplug_offline_all_work, hotplug_offline_all_work_fn);
	INIT_DELAYED_WORK_DEFERRABLE(&hotplug_offline_work, hotplug_offline_work_fn);

	/*
	 * Give the system time to boot before fiddling with hotplugging.
	 */
	flags |= HOTPLUG_PAUSED;
	schedule_delayed_work_on(0, &hotplug_decision_work, HZ * 5);
	schedule_delayed_work(&hotplug_unpause_work, HZ * 6);

#ifdef CONFIG_HAS_EARLYSUSPEND
	register_early_suspend(&auto_hotplug_suspend);
#endif
	return 0;
}
late_initcall(auto_hotplug_init);