/*
    KSysGuard, the KDE System Guard
   
	Copyright (c) 1999, 2000 Chris Schlaeger <cs@kde.org>
    
    This program is free software; you can redistribute it and/or
    modify it under the terms of version 2 of the GNU General Public
    License as published by the Free Software Foundation.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

	$Id$
*/

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "stat.h"
#include "Command.h"

typedef struct
{
	/* A CPU can be loaded with user processes, reniced processes and
	 * system processes. Unused processing time is called idle load.
	 * These variable store the percentage of each load type. */
	int userLoad;
	int niceLoad;
	int sysLoad;
	int idleLoad;

	/* To calculate the loads we need to remember the tick values for each
	 * load type. */
	unsigned long userTicks;
	unsigned long niceTicks;
	unsigned long sysTicks;
	unsigned long idleTicks;
} CPULoadInfo;

typedef struct
{
	unsigned long delta;
	unsigned long old;
} DiskLoadSample;

typedef struct
{
	/* 5 types of samples are taken:
       total, rio, wio, rBlk, wBlk */
	DiskLoadSample s[5];
} DiskLoadInfo;

typedef struct DiskIOInfo
{
	int major;
	int minor;
	int alive;
	DiskLoadSample total;
	DiskLoadSample rio;
	DiskLoadSample wio;
	DiskLoadSample rblk;
	DiskLoadSample wblk;
	struct DiskIOInfo* next;
} DiskIOInfo;

#define STATBUFSIZE 2048
static char StatBuf[STATBUFSIZE];
static int Dirty = 0;

/* We have observed deviations of up to 5% in the accuracy of the timer
 * interrupts. So we try to measure the interrupt interval and use this
 * value to calculate timing dependant values. */
static float timeInterval = 0;
static struct timeval lastSampling;
static struct timeval currSampling;

static CPULoadInfo CPULoad;
static CPULoadInfo* SMPLoad = 0;
static unsigned CPUCount = 0;
static DiskLoadInfo* DiskLoad = 0;
static unsigned DiskCount = 0;
static DiskIOInfo* DiskIO = 0;
static unsigned long PageIn = 0;
static unsigned long OldPageIn = 0;
static unsigned long PageOut = 0;
static unsigned long OldPageOut = 0;
static unsigned long Ctxt = 0;
static unsigned long OldCtxt = 0;
static unsigned int NumOfInts = 0;
static unsigned long* OldIntr = 0;
static unsigned long* Intr = 0;

static int initStatDisk(char* tag, char* buf, char* label, char* shortLabel,
						int index, cmdExecutor ex, cmdExecutor iq);
static void updateCPULoad(const char* line, CPULoadInfo* load);
static int processDisk(char* tag, char* buf, char* label, int index);
static void processStat(void);
static int processDiskIO(const char* buf);
static void cleanupDiskList(void);

static int
initStatDisk(char* tag, char* buf, char* label, char* shortLabel, int index,
			 cmdExecutor ex, cmdExecutor iq)
{
	char sensorName[128];

	gettimeofday(&lastSampling, 0);

	if (strcmp(label, tag) == 0)
	{
		int i;
		buf = buf + strlen(label) + 1;
		
		for (i = 0; i < DiskCount; ++i)
		{
			sscanf(buf, "%lu", &DiskLoad[i].s[index].old);
			while (*buf && isblank(*buf++))
				;
			while (*buf && isdigit(*buf++))
				;
			sprintf(sensorName, "disk/disk%d/%s", i, shortLabel);
			registerMonitor(sensorName, "integer", ex, iq);

		}
		return (1);
	}

	return (0);
}

static void
updateCPULoad(const char* line, CPULoadInfo* load)
{
	unsigned long currUserTicks, currSysTicks, currNiceTicks, currIdleTicks;
	unsigned long totalTicks;

	sscanf(line, "%*s %lu %lu %lu %lu", &currUserTicks, &currNiceTicks,
		   &currSysTicks, &currIdleTicks);

	totalTicks = ((currUserTicks - load->userTicks) +
				  (currSysTicks - load->sysTicks) +
				  (currNiceTicks - load->niceTicks) +
				  (currIdleTicks - load->idleTicks));

	if (totalTicks > 10)
	{
		load->userLoad = (100 * (currUserTicks - load->userTicks))
			/ totalTicks;
		load->sysLoad = (100 * (currSysTicks - load->sysTicks))
			/ totalTicks;
		load->niceLoad = (100 * (currNiceTicks - load->niceTicks))
			/ totalTicks;
		load->idleLoad = (100 - (load->userLoad + load->sysLoad
								 + load->niceLoad));
	}
	else
		load->userLoad = load->sysLoad = load->niceLoad = load->idleLoad = 0;

	load->userTicks = currUserTicks;
	load->sysTicks = currSysTicks;
	load->niceTicks = currNiceTicks;
	load->idleTicks = currIdleTicks;
}

static int
processDisk(char* tag, char* buf, char* label, int index)
{
	if (strcmp(label, tag) == 0)
	{
		unsigned long val;
		int i;
		buf = buf + strlen(label) + 1;
		
		for (i = 0; i < DiskCount; ++i)
		{
			sscanf(buf, "%lu", &val);
			while (*buf && isblank(*buf++))
				;
			while (*buf && isdigit(*buf++))
				;
			DiskLoad[i].s[index].delta = val - DiskLoad[i].s[index].old;
			DiskLoad[i].s[index].old = val;
		}
		return (1);
	}

	return (0);
}

static int
processDiskIO(const char* buf)
{
	/* Process disk_io lines as provided by 2.4.x kernels.
	 * disk_io: (3,0):(524392,341664,14532074,182728,11468624) */
	int major, minor;
	unsigned long total, rblk, rio, wblk, wio;
	DiskIOInfo* ptr = DiskIO;
	DiskIOInfo* last = 0;
	char sensorName[128];

	if (sscanf(buf, "disk_io: (%d,%d):(%lu,%lu,%lu,%lu,%lu)", &major, &minor,
			   &total, &rio, &rblk, &wio, &wblk) != 7)
		return (-1);

	while (ptr)
	{
		if (ptr->major == major && ptr->minor == minor)
		{
			/* The IO device has already been registered. */
			ptr->total.delta = total - ptr->total.old;
			ptr->total.old = total;
			ptr->rio.delta = rio - ptr->rio.old;
			ptr->rio.old = rio;
			ptr->wio.delta = wio - ptr->wio.old;
			ptr->wio.old = wio;
			ptr->rblk.delta = rblk - ptr->rblk.old;
			ptr->rblk.old = rblk;
			ptr->wblk.delta = wblk - ptr->wblk.old;
			ptr->wblk.old = wblk;
			ptr->alive = 1;
			return (0);
		}
		last = ptr;
		ptr = ptr->next;
	}

	/* The IO device has not been registered yet. We need to add it. */
	ptr = (DiskIOInfo*) malloc(sizeof(DiskIOInfo));
	ptr->major = major;
	ptr->minor = minor;
	ptr->total.delta = 0;
	ptr->total.old = total;
	ptr->rio.delta = 0;
	ptr->rio.old = rio;
	ptr->wio.delta = 0;
	ptr->wio.old = wio;
	ptr->rblk.delta = 0;
	ptr->rblk.old = rblk;
	ptr->wblk.delta = 0;
	ptr->wblk.old = wblk;
	ptr->alive = 1;
	ptr->next = 0;
	if (last)
	{
		/* Append new entry at end of list. */
		last->next = ptr;
	}
	else
	{
		/* List is empty, so we insert the fist element into the list. */
		DiskIO = ptr;
	}
	sprintf(sensorName, "disk/%d:%d/total", major, minor);
	registerMonitor(sensorName, "integer", printDiskIO, printDiskIOInfo);
	sprintf(sensorName, "disk/%d:%d/rio", major, minor);
	registerMonitor(sensorName, "integer", printDiskIO, printDiskIOInfo);
	sprintf(sensorName, "disk/%d:%d/wio", major, minor);
	registerMonitor(sensorName, "integer", printDiskIO, printDiskIOInfo);
	sprintf(sensorName, "disk/%d:%d/rblk", major, minor);
	registerMonitor(sensorName, "integer", printDiskIO, printDiskIOInfo);
	sprintf(sensorName, "disk/%d:%d/wblk", major, minor);
	registerMonitor(sensorName, "integer", printDiskIO, printDiskIOInfo);

	return (0);
}

static void
cleanupDiskList(void)
{
	DiskIOInfo* ptr = DiskIO;
	DiskIOInfo* last = 0;

	while (ptr)
	{
		if (ptr->alive == 0)
		{
			DiskIOInfo* newPtr;
			char sensorName[128];

			/* Disk device has disappeared. We have to remove it from
			 * the list and unregister the monitors. */
			sprintf(sensorName, "disk/%d:%d/total", ptr->major, ptr->minor);
			removeMonitor(sensorName);
			sprintf(sensorName, "disk/%d:%d/rio", ptr->major, ptr->minor);
			removeMonitor(sensorName);
			sprintf(sensorName, "disk/%d:%d/wio", ptr->major, ptr->minor);
			removeMonitor(sensorName);
			sprintf(sensorName, "disk/%d:%d/rblk", ptr->major, ptr->minor);
			removeMonitor(sensorName);
			sprintf(sensorName, "disk/%d:%d/wblk", ptr->major, ptr->minor);
			removeMonitor(sensorName);
			if (last)
			{
				last->next = ptr->next;
				newPtr = ptr->next;
			}
			else
			{
				DiskIO = ptr->next;
				newPtr = DiskIO;
				last = 0;
			}
			free (ptr);
			ptr = newPtr;
		}
		else
		{
			ptr->alive = 0;
			last = ptr;
			ptr = ptr->next;
		}
	}
}

static void
processStat(void)
{
	char format[32];
	char tagFormat[16];
	char buf[1024];
	char tag[32];
	char* statBufP = StatBuf;

	sprintf(format, "%%%d[^\n]\n", (int) sizeof(buf) - 1);
	sprintf(tagFormat, "%%%ds", (int) sizeof(tag) - 1);

	while (sscanf(statBufP, format, buf) == 1)
	{
		buf[sizeof(buf) - 1] = '\0';
		statBufP += strlen(buf) + 1;	/* move statBufP to next line */
		sscanf(buf, tagFormat, tag);

		if (strcmp("cpu", tag) == 0)
		{
			/* Total CPU load */
			updateCPULoad(buf, &CPULoad);
		}
		else if (strncmp("cpu", tag, 3) == 0)
		{
			/* Load for each SMP CPU */
			int id;
			sscanf(tag + 3, "%d", &id);
			updateCPULoad(buf, &SMPLoad[id]);
		}
		else if (processDisk(tag, buf, "disk", 0))
			;
		else if (processDisk(tag, buf, "disk_rio", 1))
			;
		else if (processDisk(tag, buf, "disk_wio", 2))
			;
		else if (processDisk(tag, buf, "disk_rblk", 3))
			;
		else if (processDisk(tag, buf, "disk_wblk", 4))
			;
		else if (strcmp("disk_io:", tag) == 0)
			processDiskIO(buf);
		else if (strcmp("page", tag) == 0)
		{
			unsigned long v1, v2;
			sscanf(buf + 5, "%lu %lu", &v1, &v2);
			PageIn = v1 - OldPageIn;
			OldPageIn = v1;
			PageOut = v2 - OldPageOut;
			OldPageOut = v2;
		}
		else if (strcmp("intr", tag) == 0)
		{
			int i = 0;
			char* p = buf + 5;

			for (i = 0; i < NumOfInts; i++)
			{
				unsigned long val;
				
				sscanf(p, "%lu", &val);
				Intr[i] = val - OldIntr[i];
				OldIntr[i] = val;
				while (*p && *p != ' ')
					p++;
				while (*p && *p == ' ')
					p++;
			}
		}
		else if (strcmp("ctxt", tag) == 0)
		{
			unsigned long val;
			sscanf(buf + 5, "%lu", &val);
			Ctxt = val - OldCtxt;
			OldCtxt = val;
		}
	}

	/* save exact time inverval between this and the last read of /proc/stat */
	timeInterval = currSampling.tv_sec - lastSampling.tv_sec +
		(currSampling.tv_usec - lastSampling.tv_usec) / 1000000.0;
	lastSampling = currSampling;

	cleanupDiskList();

	Dirty = 0;
}

/*
================================ public part =================================
*/

void
initStat(void)
{
	/* The CPU load is calculated from the values in /proc/stat. The cpu
	 * entry contains 4 counters. These counters count the number of ticks
	 * the system has spend on user processes, system processes, nice
	 * processes and idle time.
	 *
	 * SMP systems will have cpu1 to cpuN lines right after the cpu info. The
	 * format is identical to cpu and reports the information for each cpu.
	 * Linux kernels <= 2.0 do not provide this information!
	 *
	 * The /proc/stat file looks like this:
	 *
	 * cpu  1586 4 808 36274
	 * disk 7797 0 0 0
	 * disk_rio 6889 0 0 0
	 * disk_wio 908 0 0 0
	 * disk_rblk 13775 0 0 0
	 * disk_wblk 1816 0 0 0
	 * page 27575 1330
	 * swap 1 0
	 * intr 50444 38672 2557 0 0 0 0 2 0 2 0 0 3 1429 1 7778 0
	 * ctxt 54155
	 * btime 917379184
	 * processes 347 
	 *
	 * Linux kernel >= 2.4.0 have one or more disk_io: lines instead of
	 * the disk_* lines.
	 */

	char format[32];
	char buf[1024];
	FILE* stat = 0;

	if ((stat = fopen("/proc/stat", "r")) == NULL)
	{
		fprintf(currentClient, "ERROR: Cannot open file \'/proc/stat\'!\n"
				"The kernel needs to be compiled with support\n"
				"for /proc filesystem enabled!");
		return;
	}

	/* Use unbuffered input for /proc/stat file. */
    setvbuf(stat, NULL, _IONBF, 0);

	sprintf(format, "%%%d[^\n]\n", (int) sizeof(buf) - 1);

	while (fscanf(stat, format, buf) == 1)
	{
		char tag[32];
		char tagFormat[16];
		
		buf[sizeof(buf) - 1] = '\0';
		sprintf(tagFormat, "%%%ds", (int) sizeof(tag) - 1);
		sscanf(buf, tagFormat, tag);

		if (strcmp("cpu", tag) == 0)
		{
			/* Total CPU load */
			registerMonitor("cpu/user", "integer", printCPUUser,
							printCPUUserInfo);
			registerMonitor("cpu/nice", "integer", printCPUNice,
							printCPUNiceInfo);
			registerMonitor("cpu/sys", "integer", printCPUSys,
							printCPUSysInfo);
			registerMonitor("cpu/idle", "integer", printCPUIdle,
							printCPUIdleInfo);
		}
		else if (strncmp("cpu", tag, 3) == 0)
		{
			char cmdName[24];
			/* Load for each SMP CPU */
			int id;

			sscanf(tag + 3, "%d", &id);
			CPUCount++;
			sprintf(cmdName, "cpu%d/user", id);
			registerMonitor(cmdName, "integer", printCPUxUser,
							printCPUxUserInfo);
			sprintf(cmdName, "cpu%d/nice", id);
			registerMonitor(cmdName, "integer", printCPUxNice,
							printCPUxNiceInfo);
			sprintf(cmdName, "cpu%d/sys", id);
			registerMonitor(cmdName, "integer", printCPUxSys,
							printCPUxSysInfo);
			sprintf(cmdName, "cpu%d/idle", id);
			registerMonitor(cmdName, "integer", printCPUxIdle,
							printCPUxIdleInfo);
		}
		else if (strcmp("disk", tag) == 0)
		{
			unsigned long val;
			char* b = buf + 5;

			/* Count the number of registered disks */
			for (DiskCount = 0; *b && sscanf(b, "%lu", &val) == 1; DiskCount++)
			{
				while (*b && isblank(*b++))
					;
				while (*b && isdigit(*b++))
					;
			}
			if (DiskCount > 0)
				DiskLoad = (DiskLoadInfo*) malloc(sizeof(DiskLoadInfo)
												  * DiskCount);
			initStatDisk(tag, buf, "disk", "disk", 0, printDiskTotal,
						 printDiskTotalInfo);
		}
		else if (initStatDisk(tag, buf, "disk_rio", "rio", 1, printDiskRIO,
							  printDiskRIOInfo))
			;
		else if (initStatDisk(tag, buf, "disk_wio", "wio", 2, printDiskWIO,
							  printDiskWIOInfo))
			;
		else if (initStatDisk(tag, buf, "disk_rblk", "rblk", 3, printDiskRBlk,
							  printDiskRBlkInfo))
			;
		else if (initStatDisk(tag, buf, "disk_wblk", "wblk", 4, printDiskWBlk,
							  printDiskWBlkInfo))
			;
		else if (strcmp("disk_io:", tag) == 0)
			processDiskIO(buf);
		else if (strcmp("page", tag) == 0)
		{
			sscanf(buf + 5, "%lu %lu", &OldPageIn, &OldPageOut);
			registerMonitor("cpu/pageIn", "integer", printPageIn,
							printPageInInfo);
			registerMonitor("cpu/pageOut", "integer", printPageOut,
							printPageOutInfo);
		}
		else if (strcmp("intr", tag) == 0)
		{
			int i;
			char cmdName[32];
			char* p = buf + 5;
			/* Count the number of listed values in the intr line. */
			NumOfInts = 0;
			while (*p)
				if (*p++ == ' ')
					NumOfInts++;
			/* It looks like anything above 24 is always 0. So let's just
			 * ignore this for the time beeing. */
			if (NumOfInts > 25)
				NumOfInts = 25;
			OldIntr = (unsigned long*) malloc(NumOfInts 
											  * sizeof(unsigned long));
			Intr = (unsigned long*) malloc(NumOfInts * sizeof(unsigned long));
			i = 0;
			p = buf + 5;
			for (i = 0; p && i < NumOfInts; i++)
			{
				sscanf(p, "%lu", &OldIntr[i]);
				while (*p && *p != ' ')
					p++;
				while (*p && *p == ' ')
					p++;
				sprintf(cmdName, "cpu/interrupts/int%02d", i);
				registerMonitor(cmdName, "integer", printInterruptx,
								printInterruptxInfo);
			}
		}
		else if (strcmp("ctxt", tag) == 0)
		{
			sscanf(buf + 5, "%lu", &OldCtxt);
			registerMonitor("cpu/context", "integer", printCtxt,
							printCtxtInfo);
		}
	}
	fclose(stat);

	/* Call processStat to eliminate initial peek values. */
	processStat();

	if (CPUCount > 0)
		SMPLoad = (CPULoadInfo*) malloc(sizeof(CPULoadInfo) * CPUCount);
}

void
exitStat(void)
{
	if (DiskLoad)
		free(DiskLoad);

	if (SMPLoad)
	{
		free(SMPLoad);
		SMPLoad = 0;
	}
}

int
updateStat(void)
{
	/* ATTENTION: This function is called from a signal handler! Rules for
	 * signal handlers must be obeyed! */
	size_t n;
	int fd;

	if ((fd = open("/proc/stat", O_RDONLY)) < 0)
	{
		print_error("Cannot open file \'/proc/stat\'!\n"
			   "The kernel needs to be compiled with support\n"
			   "for /proc filesystem enabled!\n");
		return (-1);
	}
	if ((n = read(fd, StatBuf, STATBUFSIZE - 1)) == STATBUFSIZE - 1)
	{
		log_error("Internal buffer too small to read \'/proc/stat\'");
		return (-1);
	}
	gettimeofday(&currSampling, 0);
	close(fd);
	StatBuf[n] = '\0';

	Dirty = 1;

	return (0);
}

void
printCPUUser(const char* cmd)
{
	if (Dirty)
		processStat();
	fprintf(currentClient, "%d\n", CPULoad.userLoad);
}

void 
printCPUUserInfo(const char* cmd)
{
	fprintf(currentClient, "CPU User Load\t0\t100\t%%\n");
}

void
printCPUNice(const char* cmd)
{
	if (Dirty)
		processStat();
	fprintf(currentClient, "%d\n", CPULoad.niceLoad);
}

void 
printCPUNiceInfo(const char* cmd)
{
	fprintf(currentClient, "CPU Nice Load\t0\t100\t%%\n");
}

void
printCPUSys(const char* cmd)
{
	if (Dirty)
		processStat();
	fprintf(currentClient, "%d\n", CPULoad.sysLoad);
}

void 
printCPUSysInfo(const char* cmd)
{
	fprintf(currentClient, "CPU System Load\t0\t100\t%%\n");
}

void
printCPUIdle(const char* cmd)
{
	if (Dirty)
		processStat();
	fprintf(currentClient, "%d\n", CPULoad.idleLoad);
}

void 
printCPUIdleInfo(const char* cmd)
{
	fprintf(currentClient, "CPU Idle Load\t0\t100\t%%\n");
}

void
printCPUxUser(const char* cmd)
{
	int id;

	if (Dirty)
		processStat();
	sscanf(cmd + 3, "%d", &id);
	fprintf(currentClient, "%d\n", SMPLoad[id].userLoad);
}

void 
printCPUxUserInfo(const char* cmd)
{
	int id;

	sscanf(cmd + 3, "%d", &id);
	fprintf(currentClient, "CPU%d User Load\t0\t100\t%%\n", id);
}

void
printCPUxNice(const char* cmd)
{
	int id;

	if (Dirty)
		processStat();
	sscanf(cmd + 3, "%d", &id);
	fprintf(currentClient, "%d\n", SMPLoad[id].niceLoad);
}

void 
printCPUxNiceInfo(const char* cmd)
{
	int id;

	sscanf(cmd + 3, "%d", &id);
	fprintf(currentClient, "CPU%d Nice Load\t0\t100\t%%\n", id);
}

void
printCPUxSys(const char* cmd)
{
	int id;

	if (Dirty)
		processStat();
	sscanf(cmd + 3, "%d", &id);
	fprintf(currentClient, "%d\n", SMPLoad[id].sysLoad);
}

void 
printCPUxSysInfo(const char* cmd)
{
	int id;

	sscanf(cmd + 3, "%d", &id);
	fprintf(currentClient, "CPU%d System Load\t0\t100\t%%\n", id);
}

void
printCPUxIdle(const char* cmd)
{
	int id;

	if (Dirty)
		processStat();
	sscanf(cmd + 3, "%d", &id);
	fprintf(currentClient, "%d\n", SMPLoad[id].idleLoad);
}

void 
printCPUxIdleInfo(const char* cmd)
{
	int id;

	sscanf(cmd + 3, "%d", &id);
	fprintf(currentClient, "CPU%d Idle Load\t0\t100\t%%\n", id);
}

void
printDiskTotal(const char* cmd)
{
	int id;

	if (Dirty)
		processStat();
	sscanf(cmd + 9, "%d", &id);
	fprintf(currentClient, "%lu\n", (unsigned long) (DiskLoad[id].s[0].delta / timeInterval));
}

void
printDiskTotalInfo(const char* cmd)
{
	int id;

	sscanf(cmd + 9, "%d", &id);
	fprintf(currentClient, "Disk%d Total Load\t0\t0\tkBytes/s\n", id);
}

void
printDiskRIO(const char* cmd)
{
	int id;

	if (Dirty)
		processStat();
	sscanf(cmd + 9, "%d", &id);
	fprintf(currentClient, "%lu\n", (unsigned long ) (DiskLoad[id].s[1].delta / timeInterval));
}

void
printDiskRIOInfo(const char* cmd)
{
	int id;

	sscanf(cmd + 9, "%d", &id);
	fprintf(currentClient, "Disk%d Read\t0\t0\tkBytes/s\n", id);
}

void
printDiskWIO(const char* cmd)
{
	int id;

	if (Dirty)
		processStat();
	sscanf(cmd + 9, "%d", &id);
	fprintf(currentClient, "%lu\n", (unsigned long) (DiskLoad[id].s[2].delta / timeInterval));
}

void
printDiskWIOInfo(const char* cmd)
{
	int id;

	sscanf(cmd + 9, "%d", &id);
	fprintf(currentClient, "Disk%d Write\t0\t0\tkBytes/s\n", id);
}

void
printDiskRBlk(const char* cmd)
{
	int id;

	if (Dirty)
		processStat();
	sscanf(cmd + 9, "%d", &id);
	/* a block is 512 bytes or 1/2 kBytes */
	fprintf(currentClient, "%lu\n", (unsigned long)
		   (DiskLoad[id].s[3].delta / timeInterval * 2));
}

void
printDiskRBlkInfo(const char* cmd)
{
	int id;

	sscanf(cmd + 9, "%d", &id);
	fprintf(currentClient, "Disk%d Read Data\t0\t0\tkBytes/s\n", id);
}

void
printDiskWBlk(const char* cmd)
{
	int id;

	if (Dirty)
		processStat();
	sscanf(cmd + 9, "%d", &id);
	/* a block is 512 bytes or 1/2 kBytes */
	fprintf(currentClient, "%lu\n", (unsigned long)
		   (DiskLoad[id].s[4].delta / timeInterval * 2));
}

void
printDiskWBlkInfo(const char* cmd)
{
	int id;

	sscanf(cmd + 9, "%d", &id);
	fprintf(currentClient, "Disk%d Write Data\t0\t0\tkBytes/s\n", id);
}

void
printPageIn(const char* cmd)
{
	if (Dirty)
		processStat();
	fprintf(currentClient, "%lu\n", (unsigned long) (PageIn / timeInterval));
}

void
printPageInInfo(const char* cmd)
{
	fprintf(currentClient, "Paged in Pages\t0\t0\t1/s\n");
}

void
printPageOut(const char* cmd)
{
	if (Dirty)
		processStat();
	fprintf(currentClient, "%lu\n", (unsigned long) (PageOut / timeInterval));
}

void
printPageOutInfo(const char* cmd)
{
	fprintf(currentClient, "Paged out Pages\t0\t0\t1/s\n");
}

void
printInterruptx(const char* cmd)
{
	int id;

	if (Dirty)
		processStat();
	sscanf(cmd + strlen("cpu/interrupts/int"), "%d", &id);
	fprintf(currentClient, "%lu\n", (unsigned long) (Intr[id] / timeInterval));
}

void
printInterruptxInfo(const char* cmd)
{
	int id;

	sscanf(cmd + strlen("cpu/interrupt/int"), "%d", &id);
	fprintf(currentClient, "Interrupt %d\t0\t0\t1/s\n", id);
}

void
printCtxt(const char* cmd)
{
	if (Dirty)
		processStat();
	fprintf(currentClient, "%lu\n", (unsigned long) (Ctxt / timeInterval));
}

void
printCtxtInfo(const char* cmd)
{
	fprintf(currentClient, "Context switches\t0\t0\t1/s\n");
}

void
printDiskIO(const char* cmd)
{
	int major, minor;
	char name[16];
	DiskIOInfo* ptr = DiskIO;

	sscanf(cmd, "disk/%d:%d/%s", &major, &minor, name);

	while (ptr && ptr->major != major && ptr->minor != minor)
		ptr = ptr->next;

	if (!ptr)
	{
		log_error("Disk device disappeared");
		return;
	}
	if (strcmp(name, "total") == 0)
		fprintf(currentClient, "%lu\n", (unsigned long) (ptr->total.delta / timeInterval));
	else if (strcmp(name, "rio") == 0)
		fprintf(currentClient, "%lu\n", (unsigned long) (ptr->rio.delta / timeInterval));
	else if (strcmp(name, "wio") == 0)
		fprintf(currentClient, "%lu\n", (unsigned long) (ptr->wio.delta / timeInterval));
	else if (strcmp(name, "rblk") == 0)
		fprintf(currentClient, "%lu\n", (unsigned long)
			   (ptr->rblk.delta / (timeInterval * 2)));
	else if (strcmp(name, "wblk") == 0)
		fprintf(currentClient, "%lu\n", (unsigned long)
			   (ptr->wblk.delta / (timeInterval * 2)));
	else {
		fprintf(currentClient, "0\n");

		log_error("Unknown disk device property \'%s\'", name);
	}
}

void
printDiskIOInfo(const char* cmd)
{
	int major, minor;
	char name[16];
	DiskIOInfo* ptr = DiskIO;

	sscanf(cmd, "disk/%d:%d/%s", &major, &minor, name);

	while (ptr && ptr->major != major && ptr->minor != minor)
		ptr = ptr->next;

	if (!ptr)
	{
		/* Disk device has disappeared. Print a dummy answer. */
		fprintf(currentClient, "Dummy\t0\t0\t\n");
		return;
	}
	/* remove trailing '?' */
	name[strlen(name) - 1] = '\0';

	if (strcmp(name, "total") == 0)
		fprintf(currentClient, "Total accesses device %d, %d\t0\t0\t1/s\n", major, minor);
	else if (strcmp(name, "rio") == 0)
		fprintf(currentClient, "Read data device %d, %d\t0\t0\t1/s\n", major, minor);
	else if (strcmp(name, "wio") == 0)
		fprintf(currentClient, "Write data device %d, %d\t0\t0\t1/s\n", major, minor);
	else if (strcmp(name, "rblk") == 0)
		fprintf(currentClient, "Read accesses device %d, %d\t0\t0\tkBytes/s\n", major, minor);
	else if (strcmp(name, "wblk") == 0)
		fprintf(currentClient, "Write accesses device %d, %d\t0\t0\tkBytes/s\n", major, minor);
	else {
		fprintf(currentClient, "Dummy\t0\t0\t\n");
		log_error("Request for unknown device property \'%s\'",	name);
	}
}
