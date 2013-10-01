/*
 * drivers/cpufreq/cpufreq_interactive.c
 *
 * Copyright (C) 2010 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Author: Mike Chan (mike@android.com)
 *
 */

#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/cpufreq.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/tick.h>
#include <linux/time.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/kernel_stat.h>
#include <linux/input.h>
#include <asm/cputime.h>
#include <linux/hotplug.h>

static atomic_t active_count = ATOMIC_INIT(0);

struct cpufreq_interactive_cpuinfo {
	struct timer_list cpu_timer;
	int timer_idlecancel;
	u64 time_in_idle;
	u64 idle_exit_time;
	u64 timer_run_time;
	int idling;
	u64 target_set_time;
	u64 target_set_time_in_idle;
	struct cpufreq_policy *policy;
	struct cpufreq_frequency_table *freq_table;
	unsigned int target_freq;
	unsigned int floor_freq;
	u64 floor_validate_time;
	u64 hispeed_validate_time;
	int governor_enabled;
	unsigned int prev_iowait_time;
};

static DEFINE_PER_CPU(struct cpufreq_interactive_cpuinfo, cpuinfo);

/* Workqueues handle frequency scaling */
static struct task_struct *up_task;
static struct workqueue_struct *down_wq;
static struct work_struct freq_scale_down_work;
static cpumask_t up_cpumask;
static spinlock_t up_cpumask_lock;
static cpumask_t down_cpumask;
static spinlock_t down_cpumask_lock;
static struct mutex set_speed_lock;

/* Hi speed to bump to from lo speed when load burst (default max) */
#define DEFAULT_HISPEED_FREQ 1026000
static u64 hispeed_freq;

/* Bump the CPU to hispeed_freq if its load is >= 50% */
#define HISPEED_FREQ_LOAD 50

/* If the CPU load is >= 85% it goes to max frequency */
#define DEFAULT_UP_THRESHOLD 85
static unsigned long up_threshold;

/*
 * The minimum amount of time to spend at a frequency before we can ramp down.
 */
#define DEFAULT_MIN_SAMPLE_TIME (80 * USEC_PER_MSEC)
static unsigned long min_sample_time;

/*
 * The sample rate of the timer used to increase frequency
 */
#define DEFAULT_TIMER_RATE (35 * USEC_PER_MSEC)
static unsigned long timer_rate;

/*
 * Wait this long before raising speed above hispeed, by default a single
 * timer interval.
 */
#define DEFAULT_ABOVE_HISPEED_DELAY DEFAULT_TIMER_RATE
static unsigned long above_hispeed_delay_val;

/*
 * The CPU will be boosted to this frequency when the screen is
 * touched. input_boost needs to be enabled.
 */
#define DEFAULT_INPUT_BOOST_FREQ 1242000
static int input_boost_freq;

/*
 * Duration of the touch boost
 */
#define DEFAULT_INPUT_BOOST_FREQ_DURATION 1000
static int input_boost_freq_duration;

/*
 * dynamic tunables scaling flag linked to the
 * hotplug driver
 */
static bool dynamic_scaling = true;

/*
 * Helper to get the maximum set frequency which takes into consideration if the
 * device is being thermal throttled or not to ensure that the loads are
 * properly calculated
 */
unsigned int get_cur_max(unsigned int cpu);

bool interactive_selected = false;

static int cpufreq_governor_interactive(struct cpufreq_policy *policy,
                                        unsigned int event);

#ifndef CONFIG_CPU_FREQ_DEFAULT_GOV_INTERACTIVE
static
#endif
struct cpufreq_governor cpufreq_gov_interactive = {
	.name = "interactive",
	.governor = cpufreq_governor_interactive,
	.max_transition_latency = 10000000,
	.owner = THIS_MODULE,
};

static inline cputime64_t get_cpu_idle_time_jiffy(unsigned int cpu,
                                                  cputime64_t *wall)
{
  	u64 idle_time;
  	u64 cur_wall_time;
  	u64 busy_time;
    
  	cur_wall_time = jiffies64_to_cputime64(get_jiffies_64());
    
  	busy_time  = kcpustat_cpu(cpu).cpustat[CPUTIME_USER];
  	busy_time += kcpustat_cpu(cpu).cpustat[CPUTIME_SYSTEM];
  	busy_time += kcpustat_cpu(cpu).cpustat[CPUTIME_IRQ];
  	busy_time += kcpustat_cpu(cpu).cpustat[CPUTIME_SOFTIRQ];
  	busy_time += kcpustat_cpu(cpu).cpustat[CPUTIME_STEAL];
  	busy_time += kcpustat_cpu(cpu).cpustat[CPUTIME_NICE];
    
  	idle_time = cur_wall_time - busy_time;
    
  	if (wall)
    	*wall = jiffies_to_usecs(cur_wall_time);
    
  	return jiffies_to_usecs(idle_time);
}

static inline cputime64_t get_cpu_iowait_time(unsigned int cpu, cputime64_t *wall)
{
	u64 iowait_time = get_cpu_iowait_time_us(cpu, wall);
    
	if (iowait_time == -1ULL)
		return 0;
    
	return iowait_time;
}

static inline cputime64_t get_cpu_idle_time(unsigned int cpu,
                                            cputime64_t *wall)
{
  	u64 idle_time = get_cpu_idle_time_us(cpu, wall) -
    get_cpu_iowait_time(cpu, wall);
    
  	if (idle_time == -1ULL)
    	idle_time = get_cpu_idle_time_jiffy(cpu, wall);
    
  	return idle_time;
}

static void cpufreq_interactive_timer(unsigned long data)
{
	unsigned int delta_idle;
	unsigned int delta_time;
	int cpu_load;
	u64 time_in_idle;
	u64 idle_exit_time;
	struct cpufreq_interactive_cpuinfo *pcpu =
    &per_cpu(cpuinfo, data);
	u64 now_idle;
	unsigned int new_freq;
	unsigned int index;
	unsigned long flags;
	unsigned int cur_max;
	unsigned int max_freq;
    
	smp_rmb();
    
	if (!pcpu->governor_enabled)
		return;
    
	if (cpu_is_offline(data))
		return;
    
	/*
	 * Once pcpu->timer_run_time is updated to >= pcpu->idle_exit_time,
	 * this lets idle exit know the current idle time sample has
	 * been processed, and idle exit can generate a new sample and
	 * re-arm the timer.  This prevents a concurrent idle
	 * exit on that CPU from writing a new set of info at the same time
	 * the timer function runs (the timer function can't use that info
	 * until more time passes).
	 */
	time_in_idle = pcpu->time_in_idle;
	idle_exit_time = pcpu->idle_exit_time;
	now_idle = get_cpu_idle_time(data, &pcpu->timer_run_time);
	smp_wmb();
    
	/* If we raced with cancelling a timer, skip. */
	if (!idle_exit_time)
		return;
    
	delta_idle = (unsigned int)(now_idle - time_in_idle);
	delta_time = (unsigned int)(pcpu->timer_run_time - idle_exit_time);
    
	if (WARN_ON_ONCE(!delta_time))
		goto rearm;
    
    cpu_load = delta_idle > delta_time ? 
                         0 : 100 * (delta_time - delta_idle) / delta_time;
    
	cur_max = get_cur_max(pcpu->policy->cpu);

	max_freq = cur_max >= pcpu->policy->max ? pcpu->policy->max : cur_max;

	/* Lets divide by up_threshold so that the device uses more freqs */
	new_freq = max_freq * cpu_load / up_threshold;

	if (cpu_load >= up_threshold)
		new_freq = max_freq;
    
	if (new_freq <= hispeed_freq)
		pcpu->hispeed_validate_time = pcpu->timer_run_time;
    
	if (cpufreq_frequency_table_target(pcpu->policy, pcpu->freq_table,
                                       new_freq, CPUFREQ_RELATION_H,
                                       &index)) {
		pr_warn_once("timer %d: cpufreq_frequency_table_target error\n",
                     (int) data);
		goto rearm;
	}
    
	new_freq = pcpu->freq_table[index].frequency;
    
	/* 
	 * we want cpu0 to be the only core blocked for freq changes while
     	 * we are touching the screen for UI interaction 
	 */
	if (is_touching && pcpu->policy->cpu < 2)
	{
		if (ktime_to_ms(ktime_get()) - 
				freq_boosted_time >= input_boost_freq_duration)
			is_touching = false;
		else if (new_freq < input_boost_freq || 
					pcpu->policy->cur < input_boost_freq)
			new_freq = input_boost_freq;
	}
    
	/*
	 * Do not scale below floor_freq unless we have been at or above the
	 * floor frequency for the minimum sample time since last validated.
	 */
	if (new_freq < pcpu->floor_freq) {
		if (pcpu->timer_run_time - pcpu->floor_validate_time
		    < min_sample_time) {
			goto rearm;
		}
	}
    
	pcpu->floor_freq = new_freq;
	pcpu->floor_validate_time = pcpu->timer_run_time;
    
	if (pcpu->target_freq == new_freq)
    	goto rearm_if_notmax;
    
	pcpu->target_set_time_in_idle = now_idle;
	pcpu->target_set_time = pcpu->timer_run_time;
    
	if (new_freq < pcpu->target_freq) {
		pcpu->target_freq = new_freq;
		spin_lock_irqsave(&down_cpumask_lock, flags);
		cpumask_set_cpu(data, &down_cpumask);
		spin_unlock_irqrestore(&down_cpumask_lock, flags);
		queue_work(down_wq, &freq_scale_down_work);
  	} else {
		pcpu->target_freq = new_freq;
     	spin_lock_irqsave(&up_cpumask_lock, flags);
     	cpumask_set_cpu(data, &up_cpumask);
     	spin_unlock_irqrestore(&up_cpumask_lock, flags);
     	wake_up_process(up_task);
	}
    
rearm_if_notmax:
	if (pcpu->target_freq == max_freq)
		return;
    
rearm:
	if (!timer_pending(&pcpu->cpu_timer)) {
		/*
		 * If already at min: if that CPU is idle, don't set timer.
		 * Else cancel the timer if that CPU goes idle.  We don't
		 * need to re-evaluate speed until the next idle exit.
		 */
		if (pcpu->target_freq == pcpu->policy->min) {
			smp_rmb();
            
			if (pcpu->idling)
				return;
            
			pcpu->timer_idlecancel = 1;
		}
        
		pcpu->time_in_idle = get_cpu_idle_time(
                                               data, &pcpu->idle_exit_time);
		mod_timer_pinned(&pcpu->cpu_timer,
                         jiffies + usecs_to_jiffies(timer_rate));
	}
    
	return;
}

static void cpufreq_interactive_idle_start(void)
{
	int cpu = smp_processor_id();
	struct cpufreq_interactive_cpuinfo *pcpu =
    &per_cpu(cpuinfo, cpu);
	int pending;
    
	if (!pcpu->governor_enabled)
		return;
    
	if (cpu_is_offline(cpu)) {
		del_timer(&pcpu->cpu_timer);
		return;
	}
    
	pcpu->idling = 1;
	smp_wmb();
	pending = timer_pending(&pcpu->cpu_timer);
    
	if (pcpu->target_freq != pcpu->policy->min) {
#ifdef CONFIG_SMP
		/*
		 * Entering idle while not at lowest speed.  On some
		 * platforms this can hold the other CPU(s) at that speed
		 * even though the CPU is idle. Set a timer to re-evaluate
		 * speed so this idle CPU doesn't hold the other CPUs above
		 * min indefinitely.  This should probably be a quirk of
		 * the CPUFreq driver.
		 */
		if (!pending) {
			pcpu->time_in_idle = get_cpu_idle_time(
                                                   smp_processor_id(), &pcpu->idle_exit_time);
			pcpu->timer_idlecancel = 0;
			mod_timer_pinned(&pcpu->cpu_timer,
                             jiffies + usecs_to_jiffies(timer_rate));
		}
#endif
	} else {
		/*
		 * If at min speed and entering idle after load has
		 * already been evaluated, and a timer has been set just in
		 * case the CPU suddenly goes busy, cancel that timer.  The
		 * CPU didn't go busy; we'll recheck things upon idle exit.
		 */
		if (pending && pcpu->timer_idlecancel) {
			del_timer(&pcpu->cpu_timer);
			/*
			 * Ensure last timer run time is after current idle
			 * sample start time, so next idle exit will always
			 * start a new idle sampling period.
			 */
			pcpu->idle_exit_time = 0;
			pcpu->timer_idlecancel = 0;
		}
	}
    
}

static void cpufreq_interactive_idle_end(void)
{
	struct cpufreq_interactive_cpuinfo *pcpu =
    &per_cpu(cpuinfo, smp_processor_id());
    
	pcpu->idling = 0;
	smp_wmb();
    
	/*
	 * Arm the timer for 1-2 ticks later if not already, and if the timer
	 * function has already processed the previous load sampling
	 * interval.  (If the timer is not pending but has not processed
	 * the previous interval, it is probably racing with us on another
	 * CPU.  Let it compute load based on the previous sample and then
	 * re-arm the timer for another interval when it's done, rather
	 * than updating the interval start time to be "now", which doesn't
	 * give the timer function enough time to make a decision on this
	 * run.)
	 */
	if (timer_pending(&pcpu->cpu_timer) == 0 &&
	    pcpu->timer_run_time >= pcpu->idle_exit_time &&
	    pcpu->governor_enabled) {
		pcpu->time_in_idle =
        get_cpu_idle_time(smp_processor_id(),
                          &pcpu->idle_exit_time);
		pcpu->timer_idlecancel = 0;
		mod_timer_pinned(&pcpu->cpu_timer,
                         jiffies + usecs_to_jiffies(timer_rate));
	}
    
}

static int cpufreq_interactive_up_task(void *data)
{
	unsigned int cpu;
	cpumask_t tmp_mask;
	unsigned long flags;
	struct cpufreq_interactive_cpuinfo *pcpu;
    
	while (1) {
		set_current_state(TASK_INTERRUPTIBLE);
		spin_lock_irqsave(&up_cpumask_lock, flags);
        
		if (cpumask_empty(&up_cpumask)) {
			spin_unlock_irqrestore(&up_cpumask_lock, flags);
			schedule();
            
			if (kthread_should_stop())
				break;
            
			spin_lock_irqsave(&up_cpumask_lock, flags);
		}
        
		set_current_state(TASK_RUNNING);
		tmp_mask = up_cpumask;
		cpumask_clear(&up_cpumask);
		spin_unlock_irqrestore(&up_cpumask_lock, flags);
        
		for_each_cpu(cpu, &tmp_mask) {
			unsigned int j;
			unsigned int max_freq = 0;
            
			pcpu = &per_cpu(cpuinfo, cpu);
            smp_rmb();

			if (!pcpu->governor_enabled)
				continue;
            
			mutex_lock(&set_speed_lock);
            
			for_each_cpu(j, pcpu->policy->cpus) {
				struct cpufreq_interactive_cpuinfo *pjcpu =
                &per_cpu(cpuinfo, j);
                
				if (pjcpu->target_freq > max_freq)
					max_freq = pjcpu->target_freq;
			}
            
			if (max_freq != pcpu->policy->cur)
				__cpufreq_driver_target(pcpu->policy,
                                        max_freq,
                                        CPUFREQ_RELATION_H);
			mutex_unlock(&set_speed_lock);
		}
	}
    
	return 0;
}

static void cpufreq_interactive_freq_down(struct work_struct *work)
{
	unsigned int cpu;
	cpumask_t tmp_mask;
	unsigned long flags;
	struct cpufreq_interactive_cpuinfo *pcpu;
    
	spin_lock_irqsave(&down_cpumask_lock, flags);
	tmp_mask = down_cpumask;
	cpumask_clear(&down_cpumask);
	spin_unlock_irqrestore(&down_cpumask_lock, flags);
    
	for_each_cpu(cpu, &tmp_mask) {
		unsigned int j;
		unsigned int max_freq = 0;
        
		pcpu = &per_cpu(cpuinfo, cpu);
		smp_rmb();
        
		if (!pcpu->governor_enabled)
			continue;
        
		mutex_lock(&set_speed_lock);
        
		for_each_cpu(j, pcpu->policy->cpus) {
			struct cpufreq_interactive_cpuinfo *pjcpu =
            &per_cpu(cpuinfo, j);
            
			if (pjcpu->target_freq > max_freq)
				max_freq = pjcpu->target_freq;
		}
        
		if (max_freq != pcpu->policy->cur)
			__cpufreq_driver_target(pcpu->policy, max_freq,
                                    CPUFREQ_RELATION_H);
        
		mutex_unlock(&set_speed_lock);
	}
}

static ssize_t show_hispeed_freq(struct kobject *kobj,
                                 struct attribute *attr, char *buf)
{
	return sprintf(buf, "%llu\n", hispeed_freq);
}

static ssize_t store_hispeed_freq(struct kobject *kobj,
                                  struct attribute *attr, const char *buf,
                                  size_t count)
{
	int ret;
	u64 val;
    
	ret = strict_strtoull(buf, 0, &val);
	if (ret < 0)
		return ret;
	hispeed_freq = val;
	return count;
}

static struct global_attr hispeed_freq_attr = __ATTR(hispeed_freq, 0644,
                                                     show_hispeed_freq, store_hispeed_freq);

static ssize_t show_up_threshold(struct kobject *kobj,
                                 struct attribute *attr, char *buf)
{
	return sprintf(buf, "%lu\n", up_threshold);
}

static ssize_t store_up_threshold(struct kobject *kobj,
                                  struct attribute *attr, const char *buf, size_t count)
{
	int ret;
	unsigned long val;
    
	ret = strict_strtoul(buf, 0, &val);
	if (ret < 0)
		return ret;
	up_threshold = val;
	return count;
}

static struct global_attr up_threshold_attr = __ATTR(up_threshold, 0644,
                                                     show_up_threshold, store_up_threshold);

static ssize_t show_min_sample_time(struct kobject *kobj,
                                    struct attribute *attr, char *buf)
{
	return sprintf(buf, "%lu\n", min_sample_time);
}

static ssize_t store_min_sample_time(struct kobject *kobj,
                                     struct attribute *attr, const char *buf, size_t count)
{
	int ret;
	unsigned long val;
    
	ret = strict_strtoul(buf, 0, &val);
	if (ret < 0)
		return ret;
	min_sample_time = val;
	return count;
}

static struct global_attr min_sample_time_attr = __ATTR(min_sample_time, 0644,
                                                        show_min_sample_time, store_min_sample_time);

static ssize_t show_above_hispeed_delay(struct kobject *kobj,
                                        struct attribute *attr, char *buf)
{
	return sprintf(buf, "%lu\n", above_hispeed_delay_val);
}

static ssize_t store_above_hispeed_delay(struct kobject *kobj,
                                         struct attribute *attr,
                                         const char *buf, size_t count)
{
	int ret;
	unsigned long val;
    
	ret = strict_strtoul(buf, 0, &val);
	if (ret < 0)
		return ret;
	above_hispeed_delay_val = val;
	return count;
}

define_one_global_rw(above_hispeed_delay);

static ssize_t show_timer_rate(struct kobject *kobj,
                               struct attribute *attr, char *buf)
{
	return sprintf(buf, "%lu\n", timer_rate);
}

static ssize_t store_timer_rate(struct kobject *kobj,
                                struct attribute *attr, const char *buf, size_t count)
{
	int ret;
	unsigned long val;
    
	ret = strict_strtoul(buf, 0, &val);
	if (ret < 0)
		return ret;
	timer_rate = val;
	return count;
}

static struct global_attr timer_rate_attr = __ATTR(timer_rate, 0644,
                                                   show_timer_rate, store_timer_rate);

static ssize_t show_input_boost_freq(struct kobject *kobj, struct attribute *attr,
                                     char *buf)
{
	return sprintf(buf, "%d\n", input_boost_freq);
}

static ssize_t store_input_boost_freq(struct kobject *kobj, struct attribute *attr,
                                      const char *buf, size_t count)
{
	int ret;
	unsigned long val;
    
	ret = strict_strtoul(buf, 0, &val);
	if (ret < 0)
		return ret;
    
	input_boost_freq = val;
    
	return count;
}

static struct global_attr input_boost_freq_attr = __ATTR(input_boost_freq, 0644,
								show_input_boost_freq, store_input_boost_freq);

static ssize_t show_input_boost_freq_duration(struct kobject *kobj, struct attribute *attr,
                                     char *buf)
{
	return sprintf(buf, "%d\n", input_boost_freq_duration);
}

static ssize_t store_input_boost_freq_duration(struct kobject *kobj, struct attribute *attr,
                                      const char *buf, size_t count)
{
	int ret;
	unsigned long val;
    
	ret = strict_strtoul(buf, 0, &val);
	if (ret < 0)
		return ret;
    
	input_boost_freq_duration = val;
    
	return count;
}

static struct global_attr input_boost_freq_duration_attr = 
			__ATTR(input_boost_freq_duration, 0644,
			show_input_boost_freq_duration, store_input_boost_freq_duration);

static ssize_t show_dynamic_scaling(struct kobject *kobj,
                                    struct attribute *attr, char *buf)
{
  	return sprintf(buf, "%u\n", dynamic_scaling);
}

static ssize_t store_dynamic_scaling(struct kobject *kobj,
                                     struct attribute *attr, const char *buf, size_t count)
{
  	int ret;
	unsigned long val;
    
	ret = kstrtoul(buf, 0, &val);
	
	if (ret < 0)
		return ret;
  	
  	dynamic_scaling = val;
  	
  	return count;
}

static struct global_attr dynamic_scaling_attr = __ATTR(dynamic_scaling, 0644,
                                                        show_dynamic_scaling, store_dynamic_scaling);

static struct attribute *interactive_attributes[] = {
	&hispeed_freq_attr.attr,
	&above_hispeed_delay.attr,
	&min_sample_time_attr.attr,
	&timer_rate_attr.attr,
	&input_boost_freq_attr.attr,
	&input_boost_freq_duration_attr.attr,
	&dynamic_scaling_attr.attr,
	&up_threshold_attr.attr,
	NULL,
};

static struct attribute_group interactive_attr_group = {
	.attrs = interactive_attributes,
	.name = "interactive",
};

void scale_above_hispeed_delay(unsigned int new_above_hispeed_delay)
{
	if (dynamic_scaling &&
		above_hispeed_delay_val != new_above_hispeed_delay)
		above_hispeed_delay_val = new_above_hispeed_delay;
}

void scale_timer_rate(unsigned int new_timer_rate)
{
	if (dynamic_scaling && timer_rate != new_timer_rate)
		timer_rate = new_timer_rate;
}

void scale_min_sample_time(unsigned int new_min_sample_time)
{
	if (dynamic_scaling && min_sample_time != new_min_sample_time)
		min_sample_time = new_min_sample_time;
}

unsigned int get_input_boost_freq()
{
	return input_boost_freq;
}

unsigned int get_min_sample_time()
{
	return min_sample_time;
}

bool get_dynamic_scaling()
{
	return dynamic_scaling;
}

unsigned int get_hispeed_freq()
{
	return hispeed_freq;
}

static int cpufreq_governor_interactive(struct cpufreq_policy *policy,
                                        unsigned int event)
{
	int rc;
	unsigned int j;
	struct cpufreq_interactive_cpuinfo *pcpu;
	struct cpufreq_frequency_table *freq_table;
    
	switch (event) {
        case CPUFREQ_GOV_START:
            freq_table =
			cpufreq_frequency_get_table(policy->cpu);
            
            for_each_cpu(j, policy->cpus) {
                pcpu = &per_cpu(cpuinfo, j);
                pcpu->policy = policy;
                pcpu->target_freq = policy->cur;
                pcpu->freq_table = freq_table;
                pcpu->target_set_time_in_idle =
				get_cpu_idle_time(j,
                                  &pcpu->target_set_time);
                pcpu->floor_freq = pcpu->target_freq;
                pcpu->floor_validate_time =
				pcpu->target_set_time;
                pcpu->hispeed_validate_time =
				pcpu->target_set_time;
                pcpu->governor_enabled = 1;
                pcpu->idle_exit_time = pcpu->target_set_time;
                mod_timer_pinned(&pcpu->cpu_timer,
                                 jiffies + usecs_to_jiffies(timer_rate));
				smp_wmb();
            }
            
            /*
             * Do not register the idle hook and create sysfs
             * entries if we have already done so.
             */
            if (atomic_inc_return(&active_count) > 1)
                return 0;
            
			interactive_selected = true;

            rc = sysfs_create_group(cpufreq_global_kobject,
                                    &interactive_attr_group);
            if (rc)
                return rc;
            
            break;
            
        case CPUFREQ_GOV_STOP:
            for_each_cpu(j, policy->cpus) {
                pcpu = &per_cpu(cpuinfo, j);
                pcpu->governor_enabled = 0;
				smp_wmb();
                del_timer_sync(&pcpu->cpu_timer);
                
                /*
                 * Reset idle exit time since we may cancel the timer
                 * before it can run after the last idle exit time,
                 * to avoid tripping the check in idle exit for a timer
                 * that is trying to run.
                 */
                pcpu->idle_exit_time = 0;
            }
            
            flush_work(&freq_scale_down_work);
            if (atomic_dec_return(&active_count) > 0)
                return 0;
            
			interactive_selected = false;

            sysfs_remove_group(cpufreq_global_kobject,
                               &interactive_attr_group);
            
            break;
            
        case CPUFREQ_GOV_LIMITS:
            if (policy->max < policy->cur)
                __cpufreq_driver_target(policy,
                                        policy->max, CPUFREQ_RELATION_H);
            else if (policy->min > policy->cur)
                __cpufreq_driver_target(policy,
                                        policy->min, CPUFREQ_RELATION_L);
            break;
	}
	return 0;
}

static int cpufreq_interactive_idle_notifier(struct notifier_block *nb,
                                             unsigned long val,
                                             void *data)
{
	switch (val) {
        case IDLE_START:
            cpufreq_interactive_idle_start();
            break;
        case IDLE_END:
            cpufreq_interactive_idle_end();
            break;
	}
    
	return 0;
}

static struct notifier_block cpufreq_interactive_idle_nb = {
	.notifier_call = cpufreq_interactive_idle_notifier,
};

static int __init cpufreq_interactive_init(void)
{
	unsigned int i;
	struct cpufreq_interactive_cpuinfo *pcpu;
	struct sched_param param = { .sched_priority = MAX_RT_PRIO-1 };
    
	up_threshold = DEFAULT_UP_THRESHOLD;
	min_sample_time = DEFAULT_MIN_SAMPLE_TIME;
	above_hispeed_delay_val = DEFAULT_ABOVE_HISPEED_DELAY;
	timer_rate = DEFAULT_TIMER_RATE;
	hispeed_freq = DEFAULT_HISPEED_FREQ;
	input_boost_freq = DEFAULT_INPUT_BOOST_FREQ;
	input_boost_freq_duration = DEFAULT_INPUT_BOOST_FREQ_DURATION;
    
	/* Initalize per-cpu timers */
	for_each_possible_cpu(i) {
		pcpu = &per_cpu(cpuinfo, i);
		init_timer(&pcpu->cpu_timer);
		pcpu->cpu_timer.function = cpufreq_interactive_timer;
		pcpu->cpu_timer.data = i;
	}
    
	up_task = kthread_create(cpufreq_interactive_up_task, NULL,
                             "kinteractiveup");
	if (IS_ERR(up_task))
		return PTR_ERR(up_task);
    
	sched_setscheduler_nocheck(up_task, SCHED_FIFO, &param);
	get_task_struct(up_task);
    
	/* No rescuer thread, bind to CPU queuing the work for possibly
     warm cache (probably doesn't matter much). */
	down_wq = alloc_workqueue("knteractive_down", 0, 1);
    
	if (!down_wq)
		goto err_freeuptask;
    
	INIT_WORK(&freq_scale_down_work,
              cpufreq_interactive_freq_down);
    
	spin_lock_init(&up_cpumask_lock);
	spin_lock_init(&down_cpumask_lock);
	mutex_init(&set_speed_lock);
    
	/* Kick the kthread to idle */
	wake_up_process(up_task);
    
	idle_notifier_register(&cpufreq_interactive_idle_nb);
	return cpufreq_register_governor(&cpufreq_gov_interactive);
    
err_freeuptask:
	put_task_struct(up_task);
	return -ENOMEM;
}

#ifdef CONFIG_CPU_FREQ_DEFAULT_GOV_INTERACTIVE
fs_initcall(cpufreq_interactive_init);
#else
module_init(cpufreq_interactive_init);
#endif

static void __exit cpufreq_interactive_exit(void)
{
	cpufreq_unregister_governor(&cpufreq_gov_interactive);
	kthread_stop(up_task);
	put_task_struct(up_task);
	destroy_workqueue(down_wq);
}

module_exit(cpufreq_interactive_exit);

MODULE_AUTHOR("Mike Chan <mike@android.com>");
MODULE_DESCRIPTION("'cpufreq_interactive' - A cpufreq governor for "
                   "Latency sensitive workloads");
MODULE_LICENSE("GPL");
