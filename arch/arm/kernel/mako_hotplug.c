/*
 *  Copyright (c) 2013, Francisco Franco <franciscofranco.1990@gmail.com>. All rights reserved.
 *
 *  Simple no bullshit hot[in]plug driver for SMP
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/cpu.h>
#include <linux/workqueue.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/earlysuspend.h>
#include <linux/cpufreq.h>

//threshold is 2 seconds
#define SEC_THRESHOLD 2000

unsigned int __read_mostly first_level = 70;
unsigned int __read_mostly second_level = 40;
unsigned int __read_mostly third_level = 20;

//functions comes from msm_rq_stats
unsigned int report_load_at_max_freq(void);

/*
 * TODO probably populate the struct with more relevant data
 */
struct cpu_stats
{
    /*
     * variable to be accessed to filter spurious load spikes
     */
    unsigned long time_stamp;
    
    /*
     * number of online cpus
     */
    unsigned int online_cpus;
    
    /*
     * total number of cores in this SoC
     */
    unsigned int total_cpus;
};

static struct cpu_stats stats;

static struct delayed_work decide_hotplug;

static void __cpuinit decide_hotplug_func(struct work_struct *work)
{
    unsigned long now;
    unsigned int load;
    int cpu;

    //load polled in this sampling time
    load = report_load_at_max_freq();
    
    //time of this sampling time
    now = ktime_to_ms(ktime_get());
    
    //every core online if it hits this threshold
    if (load >= first_level && stats.online_cpus < stats.total_cpus)
    {
        if ((now - stats.time_stamp) >= SEC_THRESHOLD/stats.online_cpus)
        {	
            for_each_possible_cpu(cpu)
            {
                if (!cpu_online(cpu))
                {
                    cpu_up(cpu);
                    pr_info("Hotplug: cpu%d is up\n", cpu);
                }
            }
            
            /*
             * new current time for comparison in the next load check
             * we don't want too many hot[in]plugs in small time span
             */
            stats.time_stamp = now;
        }
        schedule_delayed_work_on(0, &decide_hotplug, HZ/stats.online_cpus);
        return;
    }
    
    //load is medium-high so plug only one core
    else if (load >= second_level && stats.online_cpus < stats.total_cpus)
    {
        if ((now - stats.time_stamp) >= SEC_THRESHOLD/stats.online_cpus)
        {
            for_each_possible_cpu(cpu)
            {
                if (!cpu_online(cpu))
                {
                    cpu_up(cpu);
                    pr_info("Hotplug: cpu%d is up\n", cpu);
                    break;
                }
            }

            stats.time_stamp = now;
        }
        schedule_delayed_work_on(0, &decide_hotplug, HZ/stats.online_cpus);
        return;
    }
    
    //low load obliterate the cpus to death
    else if (load <= third_level && stats.online_cpus > 1)
    {
        if ((now - stats.time_stamp) >= SEC_THRESHOLD/stats.online_cpus)
        {
            for_each_possible_cpu(cpu)
            {
                if (cpu)
                {
                    if (cpu_online(cpu))
                    {
                        cpu_down(cpu);
                        pr_info("Hotplug: cpu%d is down\n", cpu);
                    }
                }
            }
            
            stats.time_stamp = now;
        }
    }
    
    stats.online_cpus = num_online_cpus();
    
    //threshold levels scale with the number of online cpus
    first_level = 70 * stats.online_cpus;
    second_level = 40 * stats.online_cpus;
    third_level = 20 * stats.online_cpus;

    schedule_delayed_work_on(0, &decide_hotplug, HZ/stats.online_cpus);
}

static void mako_hotplug_early_suspend(struct early_suspend *handler)
{
    int cpu;
    
    //cancel the hotplug work when the screen is off
    cancel_delayed_work_sync(&decide_hotplug);
	
    if (stats.online_cpus > 1)
    {
        for_each_possible_cpu(cpu)
        {
            if (cpu)
            {
                if (cpu_online(cpu))
                {
                    cpu_down(cpu);
                    pr_info("Early Suspend Hotplug: cpu%d is down\n", cpu);
                }
            }
        }
	}
}

static void __cpuinit mako_hotplug_late_resume(struct early_suspend *handler)
{
    //bump the frequency a notch for the next timer_rate period
    struct cpufreq_policy *policy = cpufreq_cpu_get(0); 
    policy->cur = 702000; //702MHz
    
    schedule_delayed_work_on(0, &decide_hotplug, HZ);
}

static struct early_suspend mako_hotplug_suspend =
{
	.suspend = mako_hotplug_early_suspend,
	.resume = mako_hotplug_late_resume,
};

int __init start_dancing_init(void)
{
	pr_info("Mako Hotplug driver started.\n");
    INIT_DELAYED_WORK(&decide_hotplug, decide_hotplug_func);
          
    stats.time_stamp = 0;
    stats.online_cpus = num_online_cpus();
    stats.total_cpus = num_present_cpus();

    schedule_delayed_work_on(0, &decide_hotplug, HZ*25);
    
    register_early_suspend(&mako_hotplug_suspend);
    
    return 0;
}
late_initcall(start_dancing_init);
