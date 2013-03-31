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
#include <linux/slab.h>

//threshold is 2 seconds
#define SEC_THRESHOLD 2000

unsigned int __read_mostly first_level = 80;
unsigned int __read_mostly second_level = 50;
unsigned int __read_mostly third_level = 30;

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

static struct workqueue_struct *wq;

static struct delayed_work decide_hotplug;

static void __cpuinit decide_hotplug_func(struct work_struct *work)
{
    unsigned long now;
    unsigned int load;
    unsigned long temp_diff;
    unsigned long sampling_timer;
    static int cpu = 0;

    //load polled in this sampling time
    load = report_load_at_max_freq();
    
    //time of this sampling time
    now = ktime_to_ms(ktime_get());
    
    //scale all thresholds and timers with the online cpus in the struct
    first_level = 70 * stats.online_cpus;
    second_level = 40 * stats.online_cpus;
    third_level = 20 * stats.online_cpus;
    temp_diff = SEC_THRESHOLD/stats.online_cpus;
    sampling_timer = HZ/stats.online_cpus;
    
    
    //every core online if it hits this threshold
    if (load >= first_level && stats.online_cpus < stats.total_cpus)
    {
        if ((now - stats.time_stamp) >= temp_diff)
        {	
            for_each_possible_cpu(cpu)
            {
                if (cpu)
                {
                    if (!cpu_online(cpu))
                    {
                        cpu_up(cpu);
                        pr_info("Hotplug: cpu%d is up\n", cpu);
                    }
                }
            }
            
            /*
             * new current time for comparison in the next load check
             * we don't want too many hot[in]plugs in small time span
             */
            stats.time_stamp = now;
        }
        queue_delayed_work_on(0, wq, &decide_hotplug, sampling_timer);
        return;
    }
    
    //load is medium-high so plug only one core
    else if (load >= second_level && stats.online_cpus < stats.total_cpus)
    {
        if ((now - stats.time_stamp) >= temp_diff)
        {
            for_each_possible_cpu(cpu)
            {
                if (cpu)
                {
                    if (!cpu_online(cpu))
                    {
                        cpu_up(cpu);
                        pr_info("Hotplug: cpu%d is up\n", cpu);
                        break;
                    }
                }
            }

            stats.time_stamp = now;
        }
        queue_delayed_work_on(0, wq, &decide_hotplug, sampling_timer);
        return;
    }
    
    //low load obliterate the cpus to death
    else if (load <= third_level && stats.online_cpus > 1)
    {
        if ((now - stats.time_stamp) >= temp_diff)
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

    queue_delayed_work_on(0, wq, &decide_hotplug, sampling_timer);
}

static void mako_hotplug_early_suspend(struct early_suspend *handler)
{
    static int cpu = 0;
	
    //cancel the hotplug work when the screen is off
    cancel_delayed_work_sync(&decide_hotplug);
    pr_info("Early Suspend stopping Hotplug work...");
    
    if (num_online_cpus() > 1)
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
    
    stats.online_cpus = num_online_cpus();
}

static void __cpuinit mako_hotplug_late_resume(struct early_suspend *handler)
{
    struct cpufreq_policy *policy = (struct cpufreq_policy *) policy;
    
    //bump the frequency a notch for the next timer_rate period
    policy->cur = 1512000; //1512MHz
    
    //lets free the pointer (how much does this cost in ms?)
    kfree(policy);
    
    pr_info("Late Resume starting Hotplug work...\n");
    
    queue_delayed_work_on(0, wq, &decide_hotplug, HZ);
}

static struct early_suspend mako_hotplug_suspend =
{
	.suspend = mako_hotplug_early_suspend,
	.resume = mako_hotplug_late_resume,
};

int __init start_dancing_init(void)
{
	pr_info("Mako Hotplug driver started.\n");
    
    wq = create_singlethread_workqueue("mako_hotplug_workqueue");
    
    INIT_DELAYED_WORK(&decide_hotplug, decide_hotplug_func);
          
    stats.time_stamp = 0;
    stats.online_cpus = num_online_cpus();
    stats.total_cpus = num_present_cpus();

    queue_delayed_work_on(0, wq, &decide_hotplug, HZ*25);
    
    register_early_suspend(&mako_hotplug_suspend);
    
    return 0;
}
late_initcall(start_dancing_init);
