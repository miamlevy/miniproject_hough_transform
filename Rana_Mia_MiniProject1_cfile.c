


//*****************************************************************************
//
// usb_host_msc.c - USB mass storage host application.
//
// Copyright (c) 2009-2010 Texas Instruments Incorporated.  All rights reserved.
// Software License Agreement
//
// Texas Instruments (TI) is supplying this software for use solely and
// exclusively on TI's microcontroller products. The software is owned by
// TI and/or its suppliers, and is protected under applicable copyright
// laws. You may not combine this software with "viral" open-source
// software in order to form a larger program.
//
// THIS SOFTWARE IS PROVIDED "AS IS" AND WITH ALL FAULTS.
// NO WARRANTIES, WHETHER EXPRESS, IMPLIED OR STATUTORY, INCLUDING, BUT
// NOT LIMITED TO, IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE APPLY TO THIS SOFTWARE. TI SHALL NOT, UNDER ANY
// CIRCUMSTANCES, BE LIABLE FOR SPECIAL, INCIDENTAL, OR CONSEQUENTIAL
// DAMAGES, FOR ANY REASON WHATSOEVER.
//
// This is part of OMAPL138 StarterWare Firmware Package. modified and resued
// from revision 6288 of the EK-LM3S3748 Firmware Package.
//
//*****************************************************************************

#include <string.h>
#include "ff.h"
#include "hw_types.h"
#include "gpio.h"
#include "psc.h"
#include "interrupt.h"
#include "soc_OMAPL138.h"
#include "hw_psc_OMAPL138.h"
#include "lcdkOMAPL138.h"
#include "usblib.h"
#include "usbmsc.h"
#include "usbhost.h"
#include "usbhmsc.h"
#include "uartStdio.h"
#include "uart.h"
#include "delay.h"
#include "cmdline.h"
#include "cppi41dma.h"
#include "bmp.h"
#include "m_mem.h"
#include<stdio.h>
#include <math.h>

	int width =0;
	int height = 0;
	int max_radius=0;
	int votes[13][157*2]; // votes for radius
	int theta=0;
	int x=0;
	int y=0;
	float radius = 0.0;
	int int_radius = 0;
	float angle = 0.0;
	int int_angle = 0;
	int m = 0;
	int thetas[13] = {0, 15, 30, 45, 60, 75, 90, 105, 120, 135, 150, 165, 180};
	int t[13]; // votes for theta

	int test_array[500];
	int p = 0;
	int irr = 0;
	int save_position_x[50];
	int save_position_y[50];
	float pi = 22.0/7.0;



#ifdef _TMS320C6X
#include "dspcache.h"
#else
#include "cp15.h"
#endif

#if defined(__IAR_SYSTEMS_ICC__)
#pragma data_alignment=(16*1024)
static volatile unsigned int pageTable[4*1024];
#elif defined(__TMS470__)
#pragma DATA_ALIGN(pageTable, 16*1024);
static volatile unsigned int pageTable[4*1024];
#elif defined _TMS320C6X
#else
static volatile unsigned int pageTable[4*1024]__attribute__((aligned(16*1024)));
#endif

unsigned char *bitmap;
unsigned char* image;

//*****************************************************************************
//
//! \addtogroup example_list
//! <h1>USB Mass Storage Class Host (usb_host_msc)</h1>
//!
//! This example application demonstrates reading a file system from a USB mass
//! storage class device.  It makes use of FatFs, a FAT file system driver.  It
//! provides a simple command console via the UART for issuing commands to view
//! and navigate the file system on the mass storage device.
//!
//! The first UART, which is connected to the FTDI virtual serial port on the
//! evaluation board, is configured for 115,200 bits per second, and 8-N-1
//! mode.  When the program is started a message will be printed to the
//! terminal.  Type ``help'' for command help.
//!
//! For additional details about FatFs, see the following site:
//! http://elm-chan.org/fsw/ff/00index_e.html
//
//*****************************************************************************

//*****************************************************************************
//
// The number of SysTick ticks per second.
//
//*****************************************************************************
#define TICKS_PER_SECOND 100
#define MS_PER_SYSTICK (1000 / TICKS_PER_SECOND)

//*****************************************************************************
//
// The USB controller instance
//
//*****************************************************************************
#define USB_INSTANCE    0

//*****************************************************************************
//
// Our running system tick counter and a global used to determine the time
// elapsed since last call to GetTickms().
//
//*****************************************************************************
unsigned int g_ulSysTickCount;
unsigned int g_ulLastTick;

//*****************************************************************************
//
// Defines the size of the buffers that hold the path, or temporary data from
// the memory card.  There are two buffers allocated of this size.  The buffer
// size must be large enough to hold the longest expected full path name,
// including the file name, and a trailing null character.
//
//*****************************************************************************
#define PATH_BUF_SIZE   80

//*****************************************************************************
//
// Defines the size of the buffer that holds the command line.
//
//*****************************************************************************
#define CMD_BUF_SIZE    64

//*****************************************************************************
//
// Defines the number of tryout to be tried
//
//*****************************************************************************

#define USBMSC_DRIVE_RETRY     4

//*****************************************************************************
//
// Default timeout to be used for example application
//
//*****************************************************************************

tUSBHTimeOut *USBHTimeOut         = NULL;
unsigned int deviceRetryOnTimeOut = USBMSC_DRIVE_RETRY;

//*****************************************************************************
//
// This buffer holds the full path to the current working directory.  Initially
// it is root ("/").
//
//*****************************************************************************
static char g_cCwdBuf[PATH_BUF_SIZE] = "/";

//*****************************************************************************
//
// A temporary data buffer used when manipulating file paths, or reading data
// from the memory card.
//
//*****************************************************************************
static char g_cTmpBuf[PATH_BUF_SIZE];

//*****************************************************************************
//
// The buffer that holds the command line.
//
//*****************************************************************************
static char g_cCmdBuf[CMD_BUF_SIZE];

//*****************************************************************************
//
// Current FAT fs state.
//
//*****************************************************************************
static FATFS g_sFatFs;
static DIR g_sDirObject;
static FILINFO g_sFileInfo;
static FIL g_sFileObject;

//*****************************************************************************
//
// A structure that holds a mapping between an FRESULT numerical code,
// and a string representation.  FRESULT codes are returned from the FatFs
// FAT file system driver.
//
//*****************************************************************************
typedef struct
{
	FRESULT fresult;
	char *pcResultStr;
}
tFresultString;

//*****************************************************************************
//
// A macro to make it easy to add result codes to the table.
//
//*****************************************************************************
#define FRESULT_ENTRY(f)        { (f), (#f) }

//*****************************************************************************
//
// A table that holds a mapping between the numerical FRESULT code and
// it's name as a string.  This is used for looking up error codes for
// printing to the console.
//
//*****************************************************************************
tFresultString g_sFresultStrings[] =
{
		FRESULT_ENTRY(FR_OK),
		FRESULT_ENTRY(FR_NOT_READY),
		FRESULT_ENTRY(FR_NO_FILE),
		FRESULT_ENTRY(FR_NO_PATH),
		FRESULT_ENTRY(FR_INVALID_NAME),
		FRESULT_ENTRY(FR_INVALID_DRIVE),
		FRESULT_ENTRY(FR_DENIED),
		FRESULT_ENTRY(FR_EXIST),
		FRESULT_ENTRY(FR_RW_ERROR),
		FRESULT_ENTRY(FR_WRITE_PROTECTED),
		FRESULT_ENTRY(FR_NOT_ENABLED),
		FRESULT_ENTRY(FR_NO_FILESYSTEM),
		FRESULT_ENTRY(FR_INVALID_OBJECT),
		FRESULT_ENTRY(FR_MKFS_ABORTED)
};

//*****************************************************************************
//
// A macro that holds the number of result codes.
//
//*****************************************************************************
#define NUM_FRESULT_CODES (sizeof(g_sFresultStrings) / sizeof(tFresultString))

//*****************************************************************************
//
// The size of the host controller's memory pool in bytes.
//
//*****************************************************************************
#define HCD_MEMORY_SIZE         128

//*****************************************************************************
//
// The memory pool to provide to the Host controller driver.
//
//*****************************************************************************
unsigned char g_pHCDPool[HCD_MEMORY_SIZE];

//*****************************************************************************
//
// The instance data for the MSC driver.
//
//*****************************************************************************
unsigned int g_ulMSCInstance = 0;

//*****************************************************************************
//
// Declare the USB Events driver interface.
//
//*****************************************************************************
DECLARE_EVENT_DRIVER(g_sUSBEventDriver, 0, 0, USBHCDEvents);

//*****************************************************************************
//
// The global that holds all of the host drivers in use in the application.
// In this case, only the MSC class is loaded.
//
//*****************************************************************************
static tUSBHostClassDriver const * const g_ppHostClassDrivers[] =
{
		&g_USBHostMSCClassDriver,
		&g_sUSBEventDriver
};

//*****************************************************************************
//
// This global holds the number of class drivers in the g_ppHostClassDrivers
// list.
//
//*****************************************************************************
#define NUM_CLASS_DRIVERS       (sizeof(g_ppHostClassDrivers) /               \
		sizeof(g_ppHostClassDrivers[0]))

//*****************************************************************************
//
// Hold the current state for the application.
//
//*****************************************************************************
typedef enum
{
	//
	// No device is present.
	//
	STATE_NO_DEVICE,

	//
	// Mass storage device is being enumerated.
	//
	STATE_DEVICE_ENUM,

	//
	// Mass storage device is ready.
	//
	STATE_DEVICE_READY,

	//
	// An unsupported device has been attached.
	//
	STATE_UNKNOWN_DEVICE,

	//
	// A power fault has occurred.
	//
	STATE_POWER_FAULT,

	//
	// A babble int has occurred.
	//
	STATE_BABBLE_INT,

	//
	// Device Timeout.
	//
	STATE_TIMEDOUT
}
tState;
volatile tState g_eState;
volatile tState g_eUIState;


//*****************************************************************************
//
// The current USB operating mode - Host, Device or unknown.
//
//*****************************************************************************
tUSBMode g_eCurrentUSBMode;


#ifdef DMA_MODE

endpointInfo epInfo[]=
{
		{
				USB_EP_TO_INDEX(USB_EP_1),
				CPDMA_DIR_RX,
				CPDMA_MODE_SET_TRANSPARENT,
		},

		{
				USB_EP_TO_INDEX(USB_EP_1),
				CPDMA_DIR_TX,
				CPDMA_MODE_SET_GRNDIS,
		},

		{
				USB_EP_TO_INDEX(USB_EP_2),
				CPDMA_DIR_RX,
				CPDMA_MODE_SET_TRANSPARENT,
		},

		{
				USB_EP_TO_INDEX(USB_EP_2),
				CPDMA_DIR_TX,
				CPDMA_MODE_SET_GRNDIS,
		}

};

#define NUMBER_OF_ENDPOINTS 4

#endif

//*****************************************************************************
//
// The error routine that is called if the driver library encounters an error.
//
//*****************************************************************************
#ifdef DEBUG
void
__error__(char *pcFilename, unsigned int ulLine)
{
}
#endif

//*****************************************************************************
//
// This is the handler for this SysTick interrupt.
//
//*****************************************************************************
void
SysTickIntHandler(void)
{
	//
	// Update our tick counter.
	//
	g_ulSysTickCount++;
}

//*****************************************************************************
//
// This function returns the number of ticks since the last time this function
// was called.
//
//*****************************************************************************
unsigned int
GetTickms(void)
{
	unsigned int ulRetVal;
	unsigned int ulSaved;

	ulRetVal = g_ulSysTickCount;
	ulSaved = ulRetVal;

	if(ulSaved > g_ulLastTick)
	{
		ulRetVal = ulSaved - g_ulLastTick;
	}
	else
	{
		ulRetVal = g_ulLastTick - ulSaved;
	}

	//
	// This could miss a few milliseconds but the timings here are on a
	// much larger scale.
	//
	g_ulLastTick = ulSaved;

	//
	// Return the number of milliseconds since the last time this was called.
	//
	return(ulRetVal * MS_PER_SYSTICK);
}

//*****************************************************************************
//
// USB Mode callback
//
// \param ulIndex is the zero-based index of the USB controller making the
//        callback.
// \param eMode indicates the new operating mode.
//
// This function is called by the USB library whenever an OTG mode change
// occurs and, if a connection has been made, informs us of whether we are to
// operate as a host or device.
//
// \return None.
//
//*****************************************************************************
void
ModeCallback(unsigned int ulIndex, tUSBMode eMode)
{
	//
	// Save the new mode.
	//

	g_eCurrentUSBMode = eMode;
}

//*****************************************************************************
//
// This function returns a string representation of an error code that was
// returned from a function call to FatFs.  It can be used for printing human
// readable error messages.
//
//*****************************************************************************
const char *
StringFromFresult(FRESULT fresult)
{
	unsigned int uIdx;

	//
	// Enter a loop to search the error code table for a matching error code.
	//
	for(uIdx = 0; uIdx < NUM_FRESULT_CODES; uIdx++)
	{
		//
		// If a match is found, then return the string name of the error code.
		//
		if(g_sFresultStrings[uIdx].fresult == fresult)
		{
			return(g_sFresultStrings[uIdx].pcResultStr);
		}
	}

	//
	// At this point no matching code was found, so return a string indicating
	// unknown error.
	//
	return("UNKNOWN ERROR CODE");
}

//*****************************************************************************
//
// This function implements the "ls" command.  It opens the current directory
// and enumerates through the contents, and prints a line for each item it
// finds.  It shows details such as file attributes, time and date, and the
// file size, along with the name.  It shows a summary of file sizes at the end
// along with free space.
//
//*****************************************************************************
int
Cmd_ls(int argc, char *argv[])
{
	unsigned int ulTotalSize;
	unsigned int ulFileCount;
	unsigned int ulDirCount;
	FRESULT fresult;
	FATFS *pFatFs;

	//
	// Do not attempt to do anything if there is not a drive attached.
	//
	if(g_eState != STATE_DEVICE_READY)
	{
		return(FR_NOT_READY);
	}

	//
	// Open the current directory for access.
	//
	fresult = f_opendir(&g_sDirObject, g_cCwdBuf);

	//
	// Check for error and return if there is a problem.
	//
	if(fresult != FR_OK)
	{
		return(fresult);
	}

	ulTotalSize = 0;
	ulFileCount = 0;
	ulDirCount = 0;

	//
	// Enter loop to enumerate through all directory entries.
	//
	while(1)
	{
		//
		// Read an entry from the directory.
		//
		fresult = f_readdir(&g_sDirObject, &g_sFileInfo);

		//
		// Check for error and return if there is a problem.
		//
		if(fresult != FR_OK)
		{
			return(fresult);
		}

		//
		// If the file name is blank, then this is the end of the listing.
		//
		if(!g_sFileInfo.fname[0])
		{
			break;
		}

		//
		// If the attribute is directory, then increment the directory count.
		//
		if(g_sFileInfo.fattrib & AM_DIR)
		{
			ulDirCount++;
		}

		//
		// Otherwise, it is a file.  Increment the file count, and add in the
		// file size to the total.
		//
		else
		{
			ulFileCount++;
			ulTotalSize += g_sFileInfo.fsize;
		}

		//
		// Print the entry information on a single line with formatting to show
		// the attributes, date, time, size, and name.
		//
		UARTprintf("%c%c%c%c%c %u/%02u/%02u %02u:%02u %9u  %s\n",
				(g_sFileInfo.fattrib & AM_DIR) ? 'D' : '-',
						(g_sFileInfo.fattrib & AM_RDO) ? 'R' : '-',
								(g_sFileInfo.fattrib & AM_HID) ? 'H' : '-',
										(g_sFileInfo.fattrib & AM_SYS) ? 'S' : '-',
												(g_sFileInfo.fattrib & AM_ARC) ? 'A' : '-',
														(g_sFileInfo.fdate >> 9) + 1980,
														(g_sFileInfo.fdate >> 5) & 15,
														g_sFileInfo.fdate & 31,
														(g_sFileInfo.ftime >> 11),
														(g_sFileInfo.ftime >> 5) & 63,
														g_sFileInfo.fsize,
														g_sFileInfo.fname);
	}

	//
	// Print summary lines showing the file, dir, and size totals.
	//
	UARTprintf("\n%4u File(s),%10u bytes total\n%4u Dir(s)",
			ulFileCount, ulTotalSize, ulDirCount);

	//
	// Get the free space.
	//
	fresult = f_getfree("/", &ulTotalSize, &pFatFs);

	//
	// Check for error and return if there is a problem.
	//
	if(fresult != FR_OK)
	{
		return(fresult);
	}

	//
	// Display the amount of free space that was calculated.
	//
	UARTprintf(", %10uK bytes free\n", ulTotalSize * pFatFs->sects_clust / 2);

	//
	// Made it to here, return with no errors.
	//
	return(0);
}

//*****************************************************************************
//
// This function implements the "cd" command.  It takes an argument that
// specifies the directory to make the current working directory.  Path
// separators must use a forward slash "/".  The argument to cd can be one of
// the following:
//
// * root ("/")
// * a fully specified path ("/my/path/to/mydir")
// * a single directory name that is in the current directory ("mydir")
// * parent directory ("..")
//
// It does not understand relative paths, so don't try something like this:
// ("../my/new/path")
//
// Once the new directory is specified, it attempts to open the directory to
// make sure it exists.  If the new path is opened successfully, then the
// current working directory (cwd) is changed to the new path.
//
//*****************************************************************************
int
Cmd_cd(int argc, char *argv[])
{
	unsigned int uIdx;
	FRESULT fresult;

	//
	// Do not attempt to do anything if there is not a drive attached.
	//
	if(g_eState != STATE_DEVICE_READY)
	{
		return(FR_NOT_READY);
	}

	//
	// Copy the current working path into a temporary buffer so it can be
	// manipulated.
	//
	strcpy(g_cTmpBuf, g_cCwdBuf);

	//
	// If the first character is /, then this is a fully specified path, and it
	// should just be used as-is.
	//
	if(argv[1][0] == '/')
	{
		//
		// Make sure the new path is not bigger than the cwd buffer.
		//
		if(strlen(argv[1]) + 1 > sizeof(g_cCwdBuf))
		{
			UARTprintf("Resulting path name is too long\n");
			return(0);
		}

		//
		// If the new path name (in argv[1])  is not too long, then copy it
		// into the temporary buffer so it can be checked.
		//
		else
		{
			strncpy(g_cTmpBuf, argv[1], sizeof(g_cTmpBuf));
		}
	}

	//
	// If the argument is .. then attempt to remove the lowest level on the
	// CWD.
	//
	else if(!strcmp(argv[1], ".."))
	{
		//
		// Get the index to the last character in the current path.
		//
		uIdx = strlen(g_cTmpBuf) - 1;

		//
		// Back up from the end of the path name until a separator (/) is
		// found, or until we bump up to the start of the path.
		//
		while((g_cTmpBuf[uIdx] != '/') && (uIdx > 1))
		{
			//
			// Back up one character.
			//
			uIdx--;
		}

		//
		// Now we are either at the lowest level separator in the current path,
		// or at the beginning of the string (root).  So set the new end of
		// string here, effectively removing that last part of the path.
		//
		g_cTmpBuf[uIdx] = 0;
	}

	//
	// Otherwise this is just a normal path name from the current directory,
	// and it needs to be appended to the current path.
	//
	else
	{
		//
		// Test to make sure that when the new additional path is added on to
		// the current path, there is room in the buffer for the full new path.
		// It needs to include a new separator, and a trailing null character.
		//
		if(strlen(g_cTmpBuf) + strlen(argv[1]) + 1 + 1 > sizeof(g_cCwdBuf))
		{
			UARTprintf("Resulting path name is too long\n");
			return(0);
		}

		//
		// The new path is okay, so add the separator and then append the new
		// directory to the path.
		//
		else
		{
			//
			// If not already at the root level, then append a /
			//
			if(strcmp(g_cTmpBuf, "/"))
			{
				strcat(g_cTmpBuf, "/");
			}

			//
			// Append the new directory to the path.
			//
			strcat(g_cTmpBuf, argv[1]);
		}
	}

	//
	// At this point, a candidate new directory path is in chTmpBuf.  Try to
	// open it to make sure it is valid.
	//
	fresult = f_opendir(&g_sDirObject, g_cTmpBuf);

	//
	// If it can't be opened, then it is a bad path.  Inform user and return.
	//
	if(fresult != FR_OK)
	{
		UARTprintf("cd: %s\n", g_cTmpBuf);
		return(fresult);
	}

	//
	// Otherwise, it is a valid new path, so copy it into the CWD.
	//
	else
	{
		strncpy(g_cCwdBuf, g_cTmpBuf, sizeof(g_cCwdBuf));
	}

	//
	// Return success.
	//
	return(0);
}

//*****************************************************************************
//
// This function implements the "pwd" command.  It simply prints the current
// working directory.
//
//*****************************************************************************
int
Cmd_pwd(int argc, char *argv[])
{
	//
	// Do not attempt to do anything if there is not a drive attached.
	//
	if(g_eState != STATE_DEVICE_READY)
	{
		return(FR_NOT_READY);
	}

	//
	// Print the CWD to the console.
	//
	UARTprintf("%s\n", g_cCwdBuf);

	//
	// Return success.
	//
	return(0);
}

//*****************************************************************************
//
// This function implements the "cat" command.  It reads the contents of a file
// and prints it to the console.  This should only be used on text files.  If
// it is used on a binary file, then a bunch of garbage is likely to printed on
// the console.
//
//*****************************************************************************
int
Cmd_cat(int argc, char *argv[])
{
	FRESULT fresult;
	unsigned short usBytesRead;

	//
	// Do not attempt to do anything if there is not a drive attached.
	//
	if(g_eState != STATE_DEVICE_READY)
	{
		return(FR_NOT_READY);
	}

	//
	// First, check to make sure that the current path (CWD), plus the file
	// name, plus a separator and trailing null, will all fit in the temporary
	// buffer that will be used to hold the file name.  The file name must be
	// fully specified, with path, to FatFs.
	//
	if(strlen(g_cCwdBuf) + strlen(argv[1]) + 1 + 1 > sizeof(g_cTmpBuf))
	{
		UARTprintf("Resulting path name is too long\n");
		return(0);
	}

	//
	// Copy the current path to the temporary buffer so it can be manipulated.
	//
	strcpy(g_cTmpBuf, g_cCwdBuf);

	//
	// If not already at the root level, then append a separator.
	//
	if(strcmp("/", g_cCwdBuf))
	{
		strcat(g_cTmpBuf, "/");
	}

	//
	// Now finally, append the file name to result in a fully specified file.
	//
	strcat(g_cTmpBuf, argv[1]);

	//
	// Open the file for reading.
	//
	fresult = f_open(&g_sFileObject, g_cTmpBuf, FA_READ);

	//
	// If there was some problem opening the file, then return an error.
	//
	if(fresult != FR_OK)
	{
		return(fresult);
	}

	//
	// Enter a loop to repeatedly read data from the file and display it, until
	// the end of the file is reached.
	//
	do
	{
		//
		// Read a block of data from the file.  Read as much as can fit in the
		// temporary buffer, including a space for the trailing null.
		//
		fresult = f_read(&g_sFileObject, g_cTmpBuf, sizeof(g_cTmpBuf) - 1,
				&usBytesRead);

		//
		// If there was an error reading, then print a newline and return the
		// error to the user.
		//
		if(fresult != FR_OK)
		{
			UARTprintf("\n");
			return(fresult);
		}

		//
		// Null terminate the last block that was read to make it a null
		// terminated string that can be used with printf.
		//
		g_cTmpBuf[usBytesRead] = 0;

		//
		// Print the last chunk of the file that was received.
		//
		UARTprintf("%s", g_cTmpBuf);

		//
		// Continue reading until less than the full number of bytes are read.
		// That means the end of the buffer was reached.
		//
	}
	while(usBytesRead == sizeof(g_cTmpBuf) - 1);

	//
	// Return success.
	//
	return(0);
}

//*****************************************************************************
//
// This function implements the "help" command.  It prints a simple list of the
// available commands with a brief description.
//
//*****************************************************************************
int
Cmd_help(int argc, char *argv[])
{
	tCmdLineEntry *pEntry;

	//
	// Print some header text.
	//
	UARTprintf("\nAvailable commands\n");
	UARTprintf("------------------\n");

	//
	// Point at the beginning of the command table.
	//
	pEntry = &g_sCmdTable[0];

	//
	// Enter a loop to read each entry from the command table.  The end of the
	// table has been reached when the command name is NULL.
	//
	while(pEntry->pcCmd)
	{
		//
		// Print the command name and the brief description.
		//
		UARTprintf("%s%s\n", pEntry->pcCmd, pEntry->pcHelp);

		//
		// Advance to the next entry in the table.
		//
		pEntry++;
	}

	//
	// Return success.
	//
	return(0);
}

//*****************************************************************************
//
// This function implements the "imread" command.  It reads the contents of a bmp file
// and store it to an array.
//
//*****************************************************************************
int
Cmd_imread(int argc, char *argv[])
{
	//
	// Do not attempt to do anything if there is not a drive attached.
	//
	if(g_eState != STATE_DEVICE_READY)
	{
		return(FR_NOT_READY);
	}

	//
	// First, check to make sure that the current path (CWD), plus the file
	// name, plus a separator and trailing null, will all fit in the temporary
	// buffer that will be used to hold the file name.  The file name must be
	// fully specified, with path, to FatFs.
	//
	if(strlen(g_cCwdBuf) + strlen(argv[1]) + 1 + 1 > sizeof(g_cTmpBuf))
	{
		UARTprintf("Resulting path name is too long\n");
		return(0);
	}

	//
	// Copy the current path to the temporary buffer so it can be manipulated.
	//
	strcpy(g_cTmpBuf, g_cCwdBuf);

	//
	// If not already at the root level, then append a separator.
	//
	if(strcmp("/", g_cCwdBuf))
	{
		strcat(g_cTmpBuf, "/");
	}

	//
	// Now finally, append the file name to result in a fully specified file.
	//
	strcat(g_cTmpBuf, argv[1]);
	int i,j;
	mem_init();
	bitmap = usb_imread(g_cTmpBuf);
	image = m_malloc(InfoHeader.Height*InfoHeader.Width*sizeof(unsigned char));
	for(i = 0; i< InfoHeader.Height; i++)
		for(j = 0;j < InfoHeader.Width; j++)
			if(bitmap[(i*InfoHeader.Width+j)*3] < 10)
				image[i*InfoHeader.Width+j] = 1;
			else
				image[i*InfoHeader.Width+j] = 0;

	for(i = 0; i< InfoHeader.Height; i++){
		for(j = 0;j < InfoHeader.Width; j++)
			printf("%d ",image[i*InfoHeader.Width+j]);
		printf("\n");
	}

	m_free(bitmap);
	m_free(image);
	/*
    //
    // Open the file for reading.
    //
    fresult = f_open(&g_sFileObject, g_cTmpBuf, FA_READ);

    //
    // If there was some problem opening the file, then return an error.
    //
    if(fresult != FR_OK)
    {
        return(fresult);
    }


    //
    // Enter a loop to repeatedly read data from the file and display it, until
    // the end of the file is reached.
    //
    do
    {
        //
        // Read a block of data from the file.  Read as much as can fit in the
        // temporary buffer, including a space for the trailing null.
        //
        fresult = f_read(&g_sFileObject, g_cTmpBuf, sizeof(g_cTmpBuf) - 1,
                         &usBytesRead);

        //
        // If there was an error reading, then print a newline and return the
        // error to the user.
        //
        if(fresult != FR_OK)
        {
            UARTprintf("\n");
            return(fresult);
        }

        //
        // Null terminate the last block that was read to make it a null
        // terminated string that can be used with printf.
        //
        g_cTmpBuf[usBytesRead] = 0;

        //
        // Print the last chunk of the file that was received.
        //
        UARTprintf("%s", g_cTmpBuf);

        //
        // Continue reading until less than the full number of bytes are read.
        // That means the end of the buffer was reached.
        //
    }
    while(usBytesRead == sizeof(g_cTmpBuf) - 1);

	 */
	//
	// Return success.
	//
	return(0);
}
//*****************************************************************************
//
// This is the table that holds the command names, implementing functions, and
// brief description.
//
//*****************************************************************************
tCmdLineEntry g_sCmdTable[] =
{
		{ "help",   Cmd_help,      "   : Display list of commands" },
		{ "h",      Cmd_help,   "      : alias for help" },
		{ "?",      Cmd_help,   "      : alias for help" },
		{ "ls",     Cmd_ls,      "     : Display list of files" },
		{ "chdir",  Cmd_cd,         "  : Change directory" },
		{ "cd",     Cmd_cd,      "     : alias for chdir" },
		{ "pwd",    Cmd_pwd,      "    : Show current working directory" },
		{ "cat",    Cmd_cat,      "    : Show contents of a text file" },
		{ "imread", Cmd_imread,   " : Read a bmp file"},
		{ 0, 0, 0 }
};

//*****************************************************************************
//
// This is the callback from the MSC driver.
//
// \param ulInstance is the driver instance which is needed when communicating
// with the driver.
// \param ulEvent is one of the events defined by the driver.
// \param pvData is a pointer to data passed into the initial call to register
// the callback.
//
// This function handles callback events from the MSC driver.  The only events
// currently handled are the MSC_EVENT_OPEN and MSC_EVENT_CLOSE.  This allows
// the main routine to know when an MSC device has been detected and
// enumerated and when an MSC device has been removed from the system.
//
// \return Returns \e true on success or \e false on failure.
//
//*****************************************************************************
void
MSCCallback(unsigned int ulInstance, unsigned int ulEvent, void *pvData)
{
	//
	// Determine the event.
	//
	switch(ulEvent)
	{
	//
	// Called when the device driver has successfully enumerated an MSC
	// device.
	//
	case MSC_EVENT_OPEN:
	{
		//
		// Proceed to the enumeration state.
		//
		g_eState = STATE_DEVICE_ENUM;
		break;
	}

	//
	// Called when the device driver has been unloaded due to error or
	// the device is no longer present.
	//
	case MSC_EVENT_CLOSE:
	{
		//
		// Go back to the "no device" state and wait for a new connection.
		//
		g_eState = STATE_NO_DEVICE;

		break;
	}

	default:
	{
		break;
	}
	}
}

//*****************************************************************************
//
// This is the generic callback from host stack.
//
// \param pvData is actually a pointer to a tEventInfo structure.
//
// This function will be called to inform the application when a USB event has
// occurred that is outside those related to the mass storage device.  At this
// point this is used to detect unsupported devices being inserted and removed.
// It is also used to inform the application when a power fault has occurred.
// This function is required when the g_USBGenericEventDriver is included in
// the host controller driver array that is passed in to the
// USBHCDRegisterDrivers() function.
//
// \return None.
//
//*****************************************************************************
void
USBHCDEvents(void *pvData)
{
	tEventInfo *pEventInfo;

	//
	// Cast this pointer to its actual type.
	//
	pEventInfo = (tEventInfo *)pvData;

	switch(pEventInfo->ulEvent)
	{
	//
	// New keyboard detected.
	//
	case USB_EVENT_CONNECTED:
	{
		//
		// An unknown device was detected.
		//
		g_eState = STATE_UNKNOWN_DEVICE;

		break;
	}

	//
	// Keyboard has been unplugged.
	//
	case USB_EVENT_DISCONNECTED:
	{
		//
		// Unknown device has been removed.
		//
		g_eState = STATE_NO_DEVICE;

		break;
	}

	case USB_EVENT_POWER_FAULT:
	{
		//
		// No power means no device is present.
		//
		g_eState = STATE_POWER_FAULT;

		break;
	}

	case USB_EVENT_BABBLE_ERROR:
	{
		//
		// No power means no device is present.
		//
		g_eState = STATE_BABBLE_INT;

		break;
	}

	default:
	{
		break;
	}
	}
}


//
// \brief  This function confiugres the interrupt controller to receive UART interrupts.
//
static void ConfigureIntUSB(void)
{
#ifdef _TMS320C6X
	/* Initialize the DSP interrupt controller */
	IntDSPINTCInit();

	/* Register ISR to vector table */
	IntRegister(C674X_MASK_INT5, USB0HostIntHandler);

	/* Map system interrupt to DSP maskable interrupt */
	IntEventMap(C674X_MASK_INT5, SYS_INT_USB0);

	/* Enable DSP maskable interrupt */
	IntEnable(C674X_MASK_INT5);

	/* Enable DSP interrupts */
	IntGlobalEnable();
#else
	/* Initialize the ARM Interrupt Controller(AINTC). */
	IntAINTCInit();

	//
	// Registers the USB ISR  in the Interrupt Vector Table of AINTC.
	// The event number of USB 0 interrupt is 58.
	//
	IntRegister(SYS_INT_USB0, USB0HostIntHandler);

	//
	// Map the channel number 2 of AINTC to system interrupt 58.
	// Channel number 2 of AINTC is mapped to IRQ interrupt of ARM9 processor.
	//
	IntChannelSet(SYS_INT_USB0, 2);

	//
	//Enable the system interrupt number 58 in AINTC.
	//
	IntSystemEnable(SYS_INT_USB0);


	/* Enable IRQ in CPSR.*/
	IntMasterIRQEnable();

	/* Enable the interrupts in GER of AINTC.*/
	IntGlobalEnable();

	/* Enable the interrupts in HIER of AINTC.*/
	IntIRQEnable();
#endif
}

//*****************************************************************************
//
// This function initializes and connects the mass storage device to the host
//
//*****************************************************************************
void
msc_inti(void)
{
	/* Sets up 'Level 1" page table entries.
	 * The page table entry consists of the base address of the page
	 * and the attributes for the page. The following operation is to
	 * setup one-to-one mapping page table for DDR memeory range and set
	 * the atributes for the same. The DDR memory range is from 0xC0000000
	 * to 0xDFFFFFFF. Thus the base of the page table ranges from 0xC00 to
	 * 0xDFF. Cache(C bit) and Write Buffer(B bit) are enabled  only for
	 * those page table entries which maps to DDR RAM and internal RAM.
	 * All the pages in the DDR range are provided with R/W permissions
	 */

#ifdef DMA_MODE
#ifdef _TMS320C6X
	/* setup MAR bits to enable cache for range 0xC0000000 to 0xDFFFFFFF */
	/*CacheEnableMAR(0xC0000000, 0x20000000);*/

	/* Enable Cache */
	/*CacheEnable(L1PCFG_L1PMODE_32K | L1DCFG_L1DMODE_32K | L2CFG_L2MODE_256K);*/
#else
	{
		unsigned int index;
		for(index = 0; index < (4*1024); index++)
		{
			if((index >= 0xC00 && index < 0xE00)|| (index == 0x800))
			{
				pageTable[index] = (index << 20) | 0x00000C1E;
			}
			else
			{
				pageTable[index] = (index << 20) | 0x00000C12;
			}
		}
	}

	/* Disable Instruction Cache*/
	CP15ICacheDisable();

	/* Configures translation table base register
	 * with pagetable base address.
	 */
	CP15TtbSet((unsigned int )pageTable);

	/* Enables MMU */
	CP15MMUEnable();

	/* Enable Data Cache */
	CP15DCacheEnable();
#endif
#endif

	deviceRetryOnTimeOut = USBMSC_DRIVE_RETRY;

	do{
			// Initially wait for device connection.
			//
			g_eState = STATE_NO_DEVICE;
			g_eUIState = STATE_NO_DEVICE;


			//
			// Enable the UART.
			//
			UARTStdioInit();
			UARTprintf("\n\nUSB Mass Storage Host program\n");
			UARTprintf("Type \'help\' for help.\n\n");


			//
			//Setup the interrupt controller
			//
			ConfigureIntUSB();

			DelayTimerSetup();

			//
			// Enable Clocking to the USB controller.
			//
			PSCModuleControl(SOC_PSC_1_REGS,1, 0, PSC_MDCTL_NEXT_ENABLE);

			//
			// Register the host class drivers.
			//
			USBHCDRegisterDrivers(USB_INSTANCE, g_ppHostClassDrivers, NUM_CLASS_DRIVERS);

			//
			// Open an instance of the mass storage class driver.
			//
			g_ulMSCInstance = USBHMSCDriveOpen (USB_INSTANCE, 0, MSCCallback);

			//
			// Initialize the power configuration.  This sets the power enable signal
			// to be active high and does not enable the power fault.
			//
			USBHCDPowerConfigInit(USB_INSTANCE, USBHCD_VBUS_AUTO_HIGH);

	#ifdef DMA_MODE
			Cppi41DmaInit(USB_INSTANCE, epInfo, NUMBER_OF_ENDPOINTS);
	#endif

			//
			// Initialize the host controller.
			//
			USBHCDInit(USB_INSTANCE, g_pHCDPool, HCD_MEMORY_SIZE);
			SET_CONNECT_RETRY (USB_INSTANCE, USBMSC_DRIVE_RETRY);
			USBHTimeOut->Value.slEP0 = USB_EP0_TIMEOUT_MILLISECS;
			USBHTimeOut->Value.slNonEP0= USB_NONEP0_TIMEOUT_MILLISECS;
			USBHCDTimeOutHook (USB_INSTANCE, &USBHTimeOut);

			//
			// Initialize the file system.
			//
			f_mount(0, &g_sFatFs);
			while(1) {

				//
				// Start reading at the beginning of the command buffer and print a prompt.
				//
				g_cCmdBuf[0] = '\0';

				//
				// Loop forever.  This loop will be explicitly broken out of when the line
				// has been fully read.
				//
				while(1)
				{

					//
					// See if a mass storage device has been enumerated.
					//
					if(g_eState == STATE_DEVICE_ENUM)
					{

						//
						// Take it easy on the Mass storage device if it is slow to
						// start up after connecting.
						//
						if(USBHMSCDriveReady(g_ulMSCInstance) != 0)
						{
							//
							// Wait about 100ms before attempting to check if the
							// device is ready again.
							//
							delay(100);
						}
						else
						{
							//
							// Reset the working directory to the root.
							//
							g_cCwdBuf[0] = '/';
							g_cCwdBuf[1] = '\0';

							//
							// Attempt to open the directory.  Some drives take longer to
							// start up than others, and this may fail (even though the USB
							// device has enumerated) if it is still initializing.
							//
							f_mount(0, &g_sFatFs);
							if(f_opendir(&g_sDirObject, g_cCwdBuf) == FR_OK)
							{
								//
								// The drive is fully ready, so move to that state.
								//
								g_eState = STATE_DEVICE_READY;
								deviceRetryOnTimeOut = USBMSC_DRIVE_RETRY;
								printf("\nDevice connected!\n");
								break;
							}
							else
							{
								UARTprintf("%s\n", "Device Not Ready!!!");
							}
						}
					}

					if(g_eState == STATE_DEVICE_READY)
						break;
					USBHCDMain (USB_INSTANCE, g_ulMSCInstance);
				}
				if(g_eState == STATE_DEVICE_READY)
					break;
			}
			if(g_eState == STATE_DEVICE_READY)
				break;
			} while(1);
	return;
}

//*****************************************************************************
//
// This is the main loop that runs the application.
//
//*****************************************************************************
int
main(void)
{

	msc_inti();

	//
	//	Read a BMP file from the mass storage device
	//
	int i,j;

	mem_init();

	// Read Image from USB as a 2-d matrix and store it in "image"
	bitmap = usb_imread("im5.bmp");
	image = m_malloc(InfoHeader.Height*InfoHeader.Width*sizeof(unsigned char));
	for(i = 0; i< InfoHeader.Height; i++)
		for(j = 0;j < InfoHeader.Width; j++)
			if(bitmap[(i*InfoHeader.Width+j)*3] < 10)
				image[i*InfoHeader.Width+j] = 1;
			else
				image[i*InfoHeader.Width+j] = 0;

	// Print the imported image
//	for(i = 0; i< InfoHeader.Height; i++){
//		for(j = 0;j < InfoHeader.Width; j++)
//			printf("%d ",image[i*InfoHeader.Width+j]);
//		printf("\n");


//	}


		 width = InfoHeader.Width;
		 height = InfoHeader.Height;
		 max_radius = sqrt(width*width + height*height);











	for(m=0; m<height*width; m++)
	{

			if(image[m] == 1)
			{
				x = m%width; // 100 is number of columns
				y = floor(m/width); //100 is number of columns
				for(theta = 0; theta<13; theta++){
					radius = y*sin((thetas[theta]/180.0)*pi) + x*cos((thetas[theta]/180.0)*pi);
					int_radius = (int)radius + max_radius;
					votes[theta][int_radius]=votes[theta][int_radius]+1; // add vote
				}
			}
		}


	//Free memory
	m_free(bitmap);
	m_free(image);

}


