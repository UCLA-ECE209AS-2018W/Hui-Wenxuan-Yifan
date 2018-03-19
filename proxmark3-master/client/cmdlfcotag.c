//-----------------------------------------------------------------------------
// Authored by Iceman
//
// This code is licensed to you under the terms of the GNU GPL, version 2 or,
// at your option, any later version. See the LICENSE.txt file for the text of
// the license.
//-----------------------------------------------------------------------------
// Low frequency COTAG commands
//-----------------------------------------------------------------------------
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "proxmark3.h"
#include "ui.h"
#include "cmddata.h"
#include "data.h"
#include "cmdlfcotag.h"
#include "lfdemod.h"
#include "usb_cmd.h"
#include "cmdmain.h"

static int CmdHelp(const char *Cmd);

int usage_lf_cotag_read(void){
	PrintAndLog("Usage: lf COTAG read [h] <signaldata>");
	PrintAndLog("Options:");
	PrintAndLog("      h          : This help");
	PrintAndLog("      <0|1|2>    : 0 - HIGH/LOW signal; maxlength bigbuff");
	PrintAndLog("                 : 1 - translation of HI/LO into bytes with manchester 0,1");
	PrintAndLog("                 : 2 - raw signal; maxlength bigbuff");
	PrintAndLog("");
	PrintAndLog("Sample:");
	PrintAndLog("        lf cotag read 0");
	PrintAndLog("        lf cotag read 1");
	return 0;
}

// COTAG demod should be able to use GraphBuffer,
// when data load samples
int CmdCOTAGDemod(const char *Cmd) {

	uint8_t bits[COTAG_BITS] = {0};
	size_t bitlen = COTAG_BITS;
	memcpy(bits, DemodBuffer, COTAG_BITS);
	
	uint8_t alignPos = 0;
	int err = manrawdecode(bits, &bitlen, 1, &alignPos);
	if (err){
		if (g_debugMode) PrintAndLog("DEBUG: Error - COTAG too many errors: %d", err);
		return -1;
	}

	setDemodBuf(bits, bitlen, 0);

	//got a good demod
	uint16_t cn = bytebits_to_byteLSBF(bits+1, 16);
	uint32_t fc = bytebits_to_byteLSBF(bits+1+16, 8);
	
	uint32_t raw1 = bytebits_to_byteLSBF(bits, 32);
	uint32_t raw2 = bytebits_to_byteLSBF(bits+32, 32);
	uint32_t raw3 = bytebits_to_byteLSBF(bits+64, 32);
	uint32_t raw4 = bytebits_to_byteLSBF(bits+96, 32);
	
	/*
	fc 161:   1010 0001 -> LSB 1000 0101
	cn 33593  1000 0011 0011 1001 -> LSB 1001 1100 1100 0001
        cccc cccc cccc cccc                     ffffffff
	  0 1001 1100 1100 0001 1000 0101 0000 0000 100001010000000001111011100000011010000010000000000000000000000000000000000000000000000000000000100111001100000110000101000
        1001 1100 1100 0001                     10000101                                                                                         
	*/
	PrintAndLog("COTAG Found: FC %u, CN: %u Raw: %08X%08X%08X%08X", fc, cn, raw1 ,raw2, raw3, raw4);
	return 1;
}

// When reading a COTAG.
// 0 = HIGH/LOW signal - maxlength bigbuff
// 1 = translation for HI/LO into bytes with manchester 0,1 - length 300
// 2 = raw signal -  maxlength bigbuff		
int CmdCOTAGRead(const char *Cmd) {
	
	if (Cmd[0] == 'h' || Cmd[0] == 'H') return usage_lf_cotag_read();
	
	uint32_t rawsignal = 1;
	sscanf(Cmd, "%u", &rawsignal);
 
	UsbCommand c = {CMD_COTAG, {rawsignal, 0, 0}};
	clearCommandBuffer();
	SendCommand(&c);
	if ( !WaitForResponseTimeout(CMD_ACK, NULL, 7000) ) {
		PrintAndLog("command execution time out");
		return -1;	
	}
	
	switch ( rawsignal ){
		case 0: 
		case 2: {
			CmdPlot("");
			CmdGrid("384");
			getSamples(0, true); break;
		}
		case 1: {
			GetFromBigBuf(DemodBuffer, COTAG_BITS, 0);
			DemodBufferLen = COTAG_BITS;
			UsbCommand response;
			if ( !WaitForResponseTimeout(CMD_ACK, &response, 1000) ) {
				PrintAndLog("timeout while waiting for reply.");
				return -1;
			}
			return CmdCOTAGDemod("");
		}
	}	
	return 0;
}

static command_t CommandTable[] = {
	{"help",      CmdHelp,         1, "This help"},
	{"demod",     CmdCOTAGDemod,   1, "Tries to decode a COTAG signal"},
	{"read",      CmdCOTAGRead,    0, "Attempt to read and extract tag data"},
	{NULL, NULL, 0, NULL}
};

int CmdLFCOTAG(const char *Cmd) {
	clearCommandBuffer();
	CmdsParse(CommandTable, Cmd);
	return 0;
}

int CmdHelp(const char *Cmd) {
	CmdsHelp(CommandTable);
	return 0;
}
