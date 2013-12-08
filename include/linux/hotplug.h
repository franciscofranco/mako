/*
 * Copyright (c) 2013, Francisco Franco <franciscofranco.1990@gmail.com>.
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
 */

#ifndef _LINUX_HOTPLUG_H
#define _LINUX_HOTPLUG_H

extern void update_first_level(unsigned int level);
extern void update_second_level(unsigned int level);
extern void update_third_level(unsigned int level);
extern void update_fourth_level(unsigned int level);
extern void update_suspend_frequency(unsigned int freq);
extern void update_cores_on_touch(unsigned int num);

extern unsigned int get_first_level(void);
extern unsigned int get_second_level(void);
extern unsigned int get_third_level(void);
extern unsigned int get_fourth_level(void);
extern unsigned int get_suspend_frequency(void);
extern unsigned int get_cores_on_touch(void);

extern unsigned int get_input_boost_freq(void);
extern void touchboost_func(void);

extern bool gpu_idle;
extern bool dynamic_scaling;
extern short idle_counter;
extern bool is_touching;
extern u64 freq_boosted_time;
extern unsigned long time_stamp;

#endif
