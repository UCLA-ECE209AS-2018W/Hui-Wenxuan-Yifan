//-----------------------------------------------------------------------------
// Copyright (C) 2010 iZsh <izsh at fail0verflow.com>
//
// This code is licensed to you under the terms of the GNU GPL, version 2 or,
// at your option, any later version. See the LICENSE.txt file for the text of
// the license.
//-----------------------------------------------------------------------------
// Hardware commands
//-----------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "ui.h"
#include "proxmark3.h"
#include "cmdparser.h"
#include "cmdhw.h"
#include "cmdmain.h"
#include "cmddata.h"
#include "data.h"

/* low-level hardware control */

static int CmdHelp(const char *Cmd);

static void lookupChipID(uint32_t iChipID, uint32_t mem_used)
{
	char asBuff[100];
	uint32_t mem_avail = 0;
	
	switch(iChipID)
	{
		case 0x270B0A40:
			sprintf(asBuff,"AT91SAM7S512 Rev A");
			break;
		case 0x270B0A4F:
			sprintf(asBuff,"AT91SAM7S512 Rev B");
			break;
		case 0x270D0940:
			sprintf(asBuff,"AT91SAM7S256 Rev A");
			break;
		case 0x270B0941:
			sprintf(asBuff,"AT91SAM7S256 Rev B");
			break;
		case 0x270B0942:
			sprintf(asBuff,"AT91SAM7S256 Rev C");
			break;
		case 0x270B0943:
			sprintf(asBuff,"AT91SAM7S256 Rev D");
			break;
		case 0x270C0740:
			sprintf(asBuff,"AT91SAM7S128 Rev A");
			break;
		case 0x270A0741:
			sprintf(asBuff,"AT91SAM7S128 Rev B");
			break;
		case 0x270A0742:
			sprintf(asBuff,"AT91SAM7S128 Rev C");
			break;
		case 0x270A0743:
			sprintf(asBuff,"AT91SAM7S128 Rev D");
			break;
		case 0x27090540:
			sprintf(asBuff,"AT91SAM7S64 Rev A");
			break;
		case 0x27090543:
			sprintf(asBuff,"AT91SAM7S64 Rev B");
			break;
		case 0x27090544:
			sprintf(asBuff,"AT91SAM7S64 Rev C");
			break;
		case 0x27080342:
			sprintf(asBuff,"AT91SAM7S321 Rev A");
			break;
		case 0x27080340:
			sprintf(asBuff,"AT91SAM7S32 Rev A");
			break;
		case 0x27080341:
			sprintf(asBuff,"AT91SAM7S32 Rev B");
			break;
		case 0x27050241:
			sprintf(asBuff,"AT9SAM7S161 Rev A");
			break;
		case 0x27050240:
			sprintf(asBuff,"AT91SAM7S16 Rev A");
			break;
	}
	PrintAndLog("uC: %s",asBuff);
	switch((iChipID&0xE0)>>5)
	{
		case 1:
			sprintf(asBuff,"ARM946ES");
			break;
		case 2:
			sprintf(asBuff,"ARM7TDMI");
			break;
		case 4:
			sprintf(asBuff,"ARM920T");
			break;
		case 5:
			sprintf(asBuff,"ARM926EJS");
			break;
	}
	PrintAndLog("Embedded Processor: %s",asBuff);
	switch((iChipID&0xF00)>>8)
	{
		case 0:
			mem_avail = 0;
			break;
		case 1:
			mem_avail = 8;
			break;
		case 2:
			mem_avail = 16;
			break;
		case 3:
			mem_avail = 32;
			break;
		case 5:
			mem_avail = 64;
			break;
		case 7:
			mem_avail = 128;
			break;
		case 9:
			mem_avail = 256;
			break;
		case 10:
			mem_avail = 512;
			break;
		case 12:
			mem_avail = 1024;
			break;
		case 14:
			mem_avail = 2048;
			break;
	}
	PrintAndLog("Nonvolatile Program Memory Size: %dK bytes. Used: %d bytes (%2.0f\%). Free: %d bytes (%2.0f\%).", 
				mem_avail, 
				mem_used, 
				mem_avail == 0 ? 0 : (float)mem_used/(mem_avail*1024)*100,
				mem_avail*1024 - mem_used,
				mem_avail == 0 ? 0 : (float)(mem_avail*1024-mem_used)/(mem_avail*1024)*100
				);
	switch((iChipID&0xF000)>>12)
	{
		case 0:
			sprintf(asBuff,"None");
			break;
		case 1:
			sprintf(asBuff,"8K bytes");
			break;
		case 2:
			sprintf(asBuff,"16K bytes");
			break;
		case 3:
			sprintf(asBuff,"32K bytes");
			break;
		case 5:
			sprintf(asBuff,"64K bytes");
			break;
		case 7:
			sprintf(asBuff,"128K bytes");
			break;
		case 9:
			sprintf(asBuff,"256K bytes");
			break;
		case 10:
			sprintf(asBuff,"512K bytes");
			break;
		case 12:
			sprintf(asBuff,"1024K bytes");
			break;
		case 14:
			sprintf(asBuff,"2048K bytes");
			break;
	}
	PrintAndLog("Second Nonvolatile Program Memory Size: %s",asBuff);
	switch((iChipID&0xF0000)>>16)
	{
		case 1:
			sprintf(asBuff,"1K bytes");
			break;
		case 2:
			sprintf(asBuff,"2K bytes");
			break;
		case 3:
			sprintf(asBuff,"6K bytes");
			break;
		case 4:
			sprintf(asBuff,"112K bytes");
			break;
		case 5:
			sprintf(asBuff,"4K bytes");
			break;
		case 6:
			sprintf(asBuff,"80K bytes");
			break;
		case 7:
			sprintf(asBuff,"160K bytes");
			break;
		case 8:
			sprintf(asBuff,"8K bytes");
			break;
		case 9:
			sprintf(asBuff,"16K bytes");
			break;
		case 10:
			sprintf(asBuff,"32K bytes");
			break;
		case 11:
			sprintf(asBuff,"64K bytes");
			break;
		case 12:
			sprintf(asBuff,"128K bytes");
			break;
		case 13:
			sprintf(asBuff,"256K bytes");
			break;
		case 14:
			sprintf(asBuff,"96K bytes");
			break;
		case 15:
			sprintf(asBuff,"512K bytes");
			break;
	}
	PrintAndLog("Internal SRAM Size: %s",asBuff);
	switch((iChipID&0xFF00000)>>20)
	{
		case 0x19:
			sprintf(asBuff,"AT91SAM9xx Series");
			break;
		case 0x29:
			sprintf(asBuff,"AT91SAM9XExx Series");
			break;
		case 0x34:
			sprintf(asBuff,"AT91x34 Series");
			break;
		case 0x37:
			sprintf(asBuff,"CAP7 Series");
			break;
		case 0x39:
			sprintf(asBuff,"CAP9 Series");
			break;
		case 0x3B:
			sprintf(asBuff,"CAP11 Series");
			break;
		case 0x40:
			sprintf(asBuff,"AT91x40 Series");
			break;
		case 0x42:
			sprintf(asBuff,"AT91x42 Series");
			break;
		case 0x55:
			sprintf(asBuff,"AT91x55 Series");
			break;
		case 0x60:
			sprintf(asBuff,"AT91SAM7Axx Series");
			break;
		case 0x61:
			sprintf(asBuff,"AT91SAM7AQxx Series");
			break;
		case 0x63:
			sprintf(asBuff,"AT91x63 Series");
			break;
		case 0x70:
			sprintf(asBuff,"AT91SAM7Sxx Series");
			break;
		case 0x71:
			sprintf(asBuff,"AT91SAM7XCxx Series");
			break;
		case 0x72:
			sprintf(asBuff,"AT91SAM7SExx Series");
			break;
		case 0x73:
			sprintf(asBuff,"AT91SAM7Lxx Series");
			break;
		case 0x75:
			sprintf(asBuff,"AT91SAM7Xxx Series");
			break;
		case 0x92:
			sprintf(asBuff,"AT91x92 Series");
			break;
		case 0xF0:
			sprintf(asBuff,"AT75Cxx Series");
			break;
	}
	PrintAndLog("Architecture Identifier: %s",asBuff);
	switch((iChipID&0x70000000)>>28)
	{
		case 0:
			sprintf(asBuff,"ROM");
			break;
		case 1:
			sprintf(asBuff,"ROMless or on-chip Flash");
			break;
		case 4:
			sprintf(asBuff,"SRAM emulating ROM");
			break;
		case 2:
			sprintf(asBuff,"Embedded Flash Memory");
			break;
		case 3:
			sprintf(asBuff,"ROM and Embedded Flash Memory\nNVPSIZ is ROM size\nNVPSIZ2 is Flash size");
			break;
	}
	PrintAndLog("Nonvolatile Program Memory Type: %s",asBuff);
}

int CmdDetectReader(const char *Cmd)
{
  UsbCommand c={CMD_LISTEN_READER_FIELD};
  // 'l' means LF - 125/134 kHz
  if(*Cmd == 'l') {
    c.arg[0] = 1;
  } else if (*Cmd == 'h') {
    c.arg[0] = 2;
  } else if (*Cmd != '\0') {
    PrintAndLog("use 'detectreader' or 'detectreader l' or 'detectreader h'");
    return 0;
  }
  SendCommand(&c);
  return 0;
}

// ## FPGA Control
int CmdFPGAOff(const char *Cmd)
{
  UsbCommand c = {CMD_FPGA_MAJOR_MODE_OFF};
  SendCommand(&c);
  return 0;
}

int CmdLCD(const char *Cmd)
{
  int i, j;

  UsbCommand c={CMD_LCD};
  sscanf(Cmd, "%x %d", &i, &j);
  while (j--) {
    c.arg[0] = i & 0x1ff;
    SendCommand(&c);
  }
  return 0;
}

int CmdLCDReset(const char *Cmd)
{
  UsbCommand c = {CMD_LCD_RESET, {strtol(Cmd, NULL, 0), 0, 0}};
  SendCommand(&c);
  return 0;
}

int CmdReadmem(const char *Cmd)
{
  UsbCommand c = {CMD_READ_MEM, {strtol(Cmd, NULL, 0), 0, 0}};
  SendCommand(&c);
  return 0;
}

int CmdReset(const char *Cmd)
{
  UsbCommand c = {CMD_HARDWARE_RESET};
  SendCommand(&c);
  return 0;
}

/*
 * Sets the divisor for LF frequency clock: lets the user choose any LF frequency below
 * 600kHz.
 */
int CmdSetDivisor(const char *Cmd)
{
  UsbCommand c = {CMD_SET_LF_DIVISOR, {strtol(Cmd, NULL, 0), 0, 0}};
  if (c.arg[0] < 19 || c.arg[0] > 255) {
    PrintAndLog("divisor must be between 19 and 255");
  } else {
    SendCommand(&c);
    PrintAndLog("Divisor set, expected freq=%dHz", 12000000 / (c.arg[0]+1));
  }
  return 0;
}

int CmdSetMux(const char *Cmd)
{
  UsbCommand c={CMD_SET_ADC_MUX};
  if (strcmp(Cmd, "lopkd") == 0) {
    c.arg[0] = 0;
  } else if (strcmp(Cmd, "loraw") == 0) {
    c.arg[0] = 1;
  } else if (strcmp(Cmd, "hipkd") == 0) {
    c.arg[0] = 2;
  } else if (strcmp(Cmd, "hiraw") == 0) {
    c.arg[0] = 3;
  }
  SendCommand(&c);
  return 0;
}

int CmdTune(const char *Cmd)
{
    return CmdTuneSamples(Cmd);
}

int CmdVersion(const char *Cmd)
{

	clearCommandBuffer();
	UsbCommand c = {CMD_VERSION};
	static UsbCommand resp = {0, {0, 0, 0}};

	if (resp.arg[0] == 0 && resp.arg[1] == 0) { // no cached information available
		SendCommand(&c);
		if (WaitForResponseTimeout(CMD_ACK,&resp,1000)) {
			PrintAndLog("Prox/RFID mark3 RFID instrument");
			PrintAndLog((char*)resp.d.asBytes);
			lookupChipID(resp.arg[0], resp.arg[1]);
		}
	} else {
		PrintAndLog("[[[ Cached information ]]]\n");
		PrintAndLog("Prox/RFID mark3 RFID instrument");
		PrintAndLog((char*)resp.d.asBytes);
		lookupChipID(resp.arg[0], resp.arg[1]);
		PrintAndLog("");
	}
	return 0;
}

int CmdStatus(const char *Cmd)
{
	uint8_t speed_test_buffer[USB_CMD_DATA_SIZE];
	sample_buf = speed_test_buffer;

	clearCommandBuffer();
	UsbCommand c = {CMD_STATUS};
	SendCommand(&c);
	if (!WaitForResponseTimeout(CMD_ACK,&c,1900)) {
		PrintAndLog("Status command failed. USB Speed Test timed out");
	}
	return 0;
}


int CmdPing(const char *Cmd)
{
	clearCommandBuffer();
	UsbCommand resp;
	UsbCommand c = {CMD_PING};
	SendCommand(&c);
	if (WaitForResponseTimeout(CMD_ACK,&resp,1000)) {
		PrintAndLog("Ping successful");
	}else{
		PrintAndLog("Ping failed");
	}
	return 0;
}

static command_t CommandTable[] = 
{
	{"help",          CmdHelp,        1, "This help"},
	{"detectreader",  CmdDetectReader,0, "['l'|'h'] -- Detect external reader field (option 'l' or 'h' to limit to LF or HF)"},
	{"fpgaoff",       CmdFPGAOff,     0, "Set FPGA off"},
	{"lcd",           CmdLCD,         0, "<HEX command> <count> -- Send command/data to LCD"},
	{"lcdreset",      CmdLCDReset,    0, "Hardware reset LCD"},
	{"readmem",       CmdReadmem,     0, "[address] -- Read memory at decimal address from flash"},
	{"reset",         CmdReset,       0, "Reset the Proxmark3"},
	{"setlfdivisor",  CmdSetDivisor,  0, "<19 - 255> -- Drive LF antenna at 12Mhz/(divisor+1)"},
	{"setmux",        CmdSetMux,      0, "<loraw|hiraw|lopkd|hipkd> -- Set the ADC mux to a specific value"},
	{"tune",          CmdTune,        0, "['l'|'h'] -- Measure antenna tuning (option 'l' or 'h' to limit to LF or HF)"},
	{"version",       CmdVersion,     0, "Show version information about the connected Proxmark"},
	{"status",        CmdStatus,      0, "Show runtime status information about the connected Proxmark"},
	{"ping",          CmdPing,        0, "Test if the pm3 is responsive"},
	{NULL, NULL, 0, NULL}
};

int CmdHW(const char *Cmd)
{
  CmdsParse(CommandTable, Cmd);
  return 0;
}

int CmdHelp(const char *Cmd)
{
  CmdsHelp(CommandTable);
  return 0;
}
