/*
 * Copyright (c) 2003, 2007-11 Matteo Frigo
 * Copyright (c) 2003, 2007-11 Massachusetts Institute of Technology
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */


#include "ifftw.h"

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

#ifndef WITH_SLOW_TIMER
#  include "cycle.h"
#endif

/* MRJ TODO guard this so it only builds when I want my special printouts */
#include <stdio.h>
#include "exynos_5410_gpio.h"
static unsigned int gpio_regval;
static int gpio_regval_init = 0;
/* MRJ TODO this stuff really needs to be in the library somewhere */
#define EXYNOS_5410_GPIO_REG_BASE_ADDR 0x13400000
// For a given bank of GPIOs, the registers are laid out in memory as: CON DATA UPD DRIVESTR
#define EXYNOS_GPIO_CON_REG_OFFSET 0 //CON (data direction) register
#define EXYNOS_GPIO_DATA_REG_OFFSET 4 // Data register
#define EXYNOS_GPIO_UPD_REG_OFFSET 8 //UPD (pullup/pulldown) register
#define EXYNOS_GPIO_DRIVE_STRENGTH_REG_OFFSET 12 //Drive strength register

#ifndef FFTW_TIME_LIMIT
#define FFTW_TIME_LIMIT 2.0  /* don't run for more than two seconds */
#endif

/* the following code is disabled for now, because it seems to
   require that we #include <windows.h> in ifftw.h to 
   typedef LARGE_INTEGER crude_time, and this pulls in the whole
   Windows universe and leads to namespace conflicts (unless
   we did some hack like assuming sizeof(LARGE_INTEGER) == sizeof(long long).
   gettimeofday is provided by MinGW, which we use to cross-compile
   FFTW for Windows, and this seems to work well enough */
#if 0 && (defined(__WIN32__) || defined(_WIN32) || defined(_WIN64))
crude_time X(get_crude_time)(void)
{
     crude_time tv;
     QueryPerformanceCounter(&tv);
     return tv;
}

static double elapsed_since(crude_time t0)
{
     crude_time t1, freq;
     QueryPerformanceCounter(&t1);
     QueryPerformanceFrequency(&freq);
     return (((double) (t1.QuadPart - t0.QuadPart))) /
	  ((double) freq.QuadPart);
}

#  define TIME_MIN_SEC 1.0e-2

#elif defined(HAVE_GETTIMEOFDAY)
crude_time X(get_crude_time)(void)
{
     crude_time tv;
     gettimeofday(&tv, 0);
     return tv;
}

#define elapsed_sec(t1,t0) ((double)(t1.tv_sec - t0.tv_sec) +		\
			    (double)(t1.tv_usec - t0.tv_usec) * 1.0E-6)

static double elapsed_since(crude_time t0)
{
     crude_time t1;
     gettimeofday(&t1, 0);
     return elapsed_sec(t1, t0);
}

#  define TIME_MIN_SEC 1.0e-3

#else /* !HAVE_GETTIMEOFDAY */

/* Note that the only system where we are likely to need to fall back
   on the clock() function is Windows, for which CLOCKS_PER_SEC is 1000
   and thus the clock wraps once every 50 days.  This should hopefully
   be longer than the time required to create any single plan! */
crude_time X(get_crude_time)(void) { return clock(); }

#define elapsed_sec(t1,t0) ((double) ((t1) - (t0)) / CLOCKS_PER_SEC)

static double elapsed_since(crude_time t0)
{
     return elapsed_sec(clock(), t0);
}

#  define TIME_MIN_SEC 2.0e-1 /* from fftw2 */

#endif /* !HAVE_GETTIMEOFDAY */

double X(elapsed_since)(const planner *plnr, const problem *p, crude_time t0)
{
     double t = elapsed_since(t0);
     if (plnr->cost_hook)
	  t = plnr->cost_hook(p, t, COST_MAX);
     return t;
}

#ifdef WITH_SLOW_TIMER
/* excruciatingly slow; only use this if there is no choice! */
typedef crude_time ticks;
#  define getticks X(get_crude_time)
#  define elapsed(t1,t0) elapsed_sec(t1,t0)
#  define TIME_MIN TIME_MIN_SEC
#  define TIME_REPEAT 4 /* from fftw2 */
#  define HAVE_TICK_COUNTER
#endif

#ifdef HAVE_TICK_COUNTER

#  ifndef TIME_MIN
#    define TIME_MIN 100.0
#  endif

#  ifndef TIME_REPEAT
#    define TIME_REPEAT 8
#  endif

  static double measure(plan *pln, const problem *p, int iter)
  {
       ticks t0, t1;
       int i;
       /* MRJ Write 1 to GPIO 27 */
       exynos_5410_gpio_write_raw_reg(0x0C60 + EXYNOS_GPIO_DATA_REG_OFFSET, gpio_regval | (1U << 1));
       t0 = getticks();
       for (i = 0; i < iter; ++i) 
	    pln->adt->solve(pln, p);
       t1 = getticks();
       /* MRJ Write 0 to GPIO 27 */
       exynos_5410_gpio_write_raw_reg(0x0C60 + EXYNOS_GPIO_DATA_REG_OFFSET, gpio_regval | (0U << 1));
       return elapsed(t1, t0);
  }

  double X(measure_execution_time)(const planner *plnr, 
				   plan *pln, const problem *p)
  {
    int iter;
    int repeat;

    /* MRJ This will run the first time this function is called to put the GPIO in a consistent state */
    if(!gpio_regval_init) {
      odroid_xu_gpio_setup_all_output();
      gpio_regval = exynos_5410_gpio_read_raw_reg(0x0C60 + EXYNOS_GPIO_DATA_REG_OFFSET);
      /* Write a 0 to pin 27 to make sure we start in a consistent state.
         Then we can write a 1 to begin each codelet and a 0 to end it */
      gpio_regval &= ~(1U << 1); /* Zero out bit 1 */
      exynos_5410_gpio_write_raw_reg(0x0C60 + EXYNOS_GPIO_DATA_REG_OFFSET, gpio_regval); /* Write out the new value */
      gpio_regval_init = 1;
    }

    /* MRJ print out problem and plan for beginning of line */
    D("%P, ", p);
    printer *pr = X(mkprinter_file)(stderr);
    pln->adt->print(pln, pr);
    X(printer_destroy)(pr);

    X(plan_awake)(pln, AWAKE_ZERO);
    p->adt->zero(p);

    start_over:
      for (iter = 1; iter; iter *= 2) {
        double tmin = 0;
        int first = 1;
        crude_time begin = X(get_crude_time)();

        /* repeat the measurement TIME_REPEAT times */
        for (repeat = 0; repeat < TIME_REPEAT; ++repeat) {
          double t = measure(pln, p, iter);
          if (plnr->cost_hook)
            t = plnr->cost_hook(p, t, COST_MAX);
          if (t < 0) {
            fprintf(stderr, ", %d, %d", repeat+1, iter);
            goto start_over;
          }

          if (first || t < tmin)
            tmin = t;
  		    first = 0;

          /* do not run for too long */
  		    if (X(elapsed_since)(plnr, p, begin) > FFTW_TIME_LIMIT) {
            ++repeat; /* MRJ increment by one so we can print the correct number of times we executed this */
  		      break;
          }
  	    }

        fprintf(stderr, ", %d, %d", repeat, iter);
  	    if (tmin >= TIME_MIN) {
  		    X(plan_awake)(pln, SLEEPY);
          /* MRJ Want to print the number of times we ran the plan (repeat) to know how to postprocess the tag/power data */
  		    fprintf(stderr, ", %f\n", tmin / (double)iter);
          return tmin / (double) iter;
  	    }
      }
      goto start_over; /* may happen if timer is screwed up */
  }

#else /* no cycle counter */

  double X(measure_execution_time)(const planner *plnr, 
				   plan *pln, const problem *p)
  {
       UNUSED(plnr);
       UNUSED(p);
       UNUSED(pln);
       return -1.0;
  }

#endif
