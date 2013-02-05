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

static bool online_all_cpus __read_mostly = false;
module_param(online_all_cpus, bool, 0755);

struct work_struct hotplug_online_all_work;
struct work_struct hotplug_offline_all_work;

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
	online_cpu_nr(1);
	pr_info("auto_hotplug: CPU%d online.\n", 1);
	
	if (online_all_cpus) {
		online_cpu_nr(2);
		online_cpu_nr(3);
		pr_info("auto_hotplug: CPU%d online.\n", 1);
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

#ifdef CONFIG_HAS_EARLYSUSPEND
static void auto_hotplug_early_suspend(struct early_suspend *handler)
{
    if (num_online_cpus() > 1) {
    	pr_info("auto_hotplug: Offlining CPUs for early suspend\n");
        schedule_work_on(0, &hotplug_offline_all_work);
	}
}

static void auto_hotplug_late_resume(struct early_suspend *handler)
{
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

#ifdef CONFIG_HAS_EARLYSUSPEND
	register_early_suspend(&auto_hotplug_suspend);
#endif
	return 0;
}
late_initcall(auto_hotplug_init);