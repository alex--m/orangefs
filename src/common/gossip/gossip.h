/*
 * copyright (c) 2000 Clemson University, all rights reserved.
 *
 * Written by Phil Carns.
 *
 * This program is free software; you can redistribute it and/or
 * modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Contacts:  Phil Carns  pcarns@parl.clemson.edu
 */

/*
 * April 2001
 *
 * This is a basic application logging facility.  It uses printf style
 * formatting and provides several mechanisms for output. 
 */ 

#ifndef __GOSSIP_H
#define __GOSSIP_H 

#include <syslog.h>

/********************************************************************
 * Visible interface
 */

int gossip_enable_syslog(int priority);
int gossip_enable_stderr(void);
int gossip_enable_file(const char* filename, const char* mode);
int gossip_disable(void);
int gossip_set_debug_mask(int debug_on, int mask);


#ifdef __GNUC__

/* do printf style type checking if built with gcc */
int __gossip_debug(int mask, const char* format, ...)
	__attribute__ ((format (printf, 2, 3)));
int gossip_err(const char* format, ...)
	__attribute__ ((format (printf, 1, 2)));

#ifdef GOSSIP_DISABLE_DEBUG
	#define gossip_debug(mask, format, f...)							\
		do{}while(0)															
#else

	extern int gossip_debug_on;
	extern int gossip_debug_mask;
	extern int gossip_facility;

	/* try to avoid function call overhead by checking masks in macro */
	#define gossip_debug(mask, format, f...)								\
		do{																			\
			if((gossip_debug_on) && (gossip_debug_mask & mask) &&    \
				(gossip_facility)){												\
				__gossip_debug(mask, format, ##f);							\
			}																			\
		}while(0)																	

#endif /* GOSSIP_DISABLE_DEBUG */

/* do file and line number printouts w/ the GNU preprocessor */
#define gossip_ldebug(mask, format, f...)								\
	do{																			\
		gossip_debug(mask, "%s line %d: ", __FILE__, __LINE__);	\
		gossip_debug(mask, format, ##f);									\
	} while(0)

#define gossip_lerr(format, f...)										\
	do{																			\
		gossip_err("%s line %d: ", __FILE__, __LINE__);				\
		gossip_err(format, ##f);											\
	} while(0)

#else

int __gossip_debug(int mask, const char* format, ...);
int __gossip_debug_stub(int mask, const char* format, ...);
int gossip_err(const char* format, ...);

#ifdef GOSSIP_DISABLE_DEBUG
	#define gossip_debug __gossip_debug_stub
	#define gossip_ldebug __gossip_debug_stub
#else
	#define gossip_debug __gossip_debug
	#define gossip_ldebug __gossip_debug
#endif /* GOSSIP_DISABLE_DEBUG */

#define gossip_lerr gossip_err

#endif /* __GNUC__ */

#endif /* __GOSSIP_H */

