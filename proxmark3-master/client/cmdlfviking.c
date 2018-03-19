//-----------------------------------------------------------------------------
//
// This code is licensed to you under the terms of the GNU GPL, version 2 or,
// at your option, any later version. See the LICENSE.txt file for the text of
// the license.
//-----------------------------------------------------------------------------
// Low frequency Viking tag commands (AKA FDI Matalec Transit)
// ASK/Manchester, RF/32, 64 bits (complete)
//-----------------------------------------------------------------------------
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "proxmark3.h"
#include "cmdlfviking.h"
#include "ui.h"
#include "util.h"
#include "graph.h"
#include "cmdparser.h"
#include "cmddata.h"
#include "cmdmain.h"
#include "cmdlf.h"
#include "lfdemod.h"
static int CmdHelp(const char *Cmd);

int usage_lf_viking_clone(void) {
	PrintAndLog("clone a Viking AM tag to a T55x7 tag.");
	PrintAndLog("Usage: lf viking clone <Card ID - 8 hex digits> <Q5>");
  PrintAndLog("Options :");
  PrintAndLog("  <Card Number>  : 8 digit hex viking card number");
  PrintAndLog("  <Q5>           : specify write to Q5 (t5555 instead of t55x7)");
  PrintAndLog("");
  PrintAndLog("Sample  : lf viking clone 1A337 Q5");
	return 0;
}

int usage_lf_viking_sim(void) {
  PrintAndLog("Enables simulation of viking card with specified card number.");
  PrintAndLog("Simulation runs until the button is pressed or another USB command is issued.");
  PrintAndLog("Per viking format, the card number is 8 digit hex number.  Larger values are truncated.");
  PrintAndLog("");
  PrintAndLog("Usage:  lf viking sim <Card-Number>");
  PrintAndLog("Options :");
  PrintAndLog("  <Card Number>   : 8 digit hex viking card number");
  PrintAndLog("");
  PrintAndLog("Sample  : lf viking sim 1A337");
  return 0;
}

uint64_t getVikingBits(uint32_t id) {
	//calc checksum
	uint8_t checksum = ((id>>24) & 0xFF) ^ ((id>>16) & 0xFF) ^ ((id>>8) & 0xFF) ^ (id & 0xFF) ^ 0xF2 ^ 0xA8;
	return ((uint64_t)0xF2 << 56) | ((uint64_t)id << 8) | checksum;
}

//by marshmellow
//see ASKDemod for what args are accepted
int CmdVikingDemod(const char *Cmd) {
	if (!ASKDemod(Cmd, false, false, 1)) {
		if (g_debugMode) PrintAndLog("ASKDemod failed");
		return 0;
	}
	size_t size = DemodBufferLen;
	//call lfdemod.c demod for Viking
	int ans = VikingDemod_AM(DemodBuffer, &size);
	if (ans < 0) {
		if (g_debugMode) PrintAndLog("Error Viking_Demod %d", ans);
		return 0;
	}
	//got a good demod
	uint32_t raw1 = bytebits_to_byte(DemodBuffer+ans, 32);
	uint32_t raw2 = bytebits_to_byte(DemodBuffer+ans+32, 32);
	uint32_t cardid = bytebits_to_byte(DemodBuffer+ans+24, 32);
	uint8_t  checksum = bytebits_to_byte(DemodBuffer+ans+32+24, 8);
	PrintAndLog("Viking Tag Found: Card ID %08X, Checksum: %02X", cardid, (unsigned int) checksum);
	PrintAndLog("Raw: %08X%08X", raw1,raw2);
	setDemodBuf(DemodBuffer, 64, ans);
	setClockGrid(g_DemodClock, g_DemodStartIdx + (ans*g_DemodClock));
	return 1;
}

//by marshmellow
//see ASKDemod for what args are accepted
int CmdVikingRead(const char *Cmd) {
	// read lf silently
	lf_read(true, 10000);
	// demod and output viking ID	
	return CmdVikingDemod(Cmd);
}

int CmdVikingClone(const char *Cmd) {
	uint32_t id = 0;
	uint64_t rawID = 0;
	bool Q5 = false;
	char cmdp = param_getchar(Cmd, 0);
	if (strlen(Cmd) == 0 || cmdp == 'h' || cmdp == 'H') return usage_lf_viking_clone();

	id = param_get32ex(Cmd, 0, 0, 16);
	if (id == 0) return usage_lf_viking_clone();
	if (param_getchar(Cmd, 1)=='Q' || param_getchar(Cmd, 1)=='q')
		Q5 = true;

	rawID = getVikingBits(id);
	PrintAndLog("Cloning - ID: %08X, Raw: %08X%08X",id,(uint32_t)(rawID >> 32),(uint32_t) (rawID & 0xFFFFFFFF));
	UsbCommand c = {CMD_VIKING_CLONE_TAG,{rawID >> 32, rawID & 0xFFFFFFFF, Q5}};
	clearCommandBuffer();
	SendCommand(&c);
	//check for ACK
	WaitForResponse(CMD_ACK,NULL);
	return 0;
}

int CmdVikingSim(const char *Cmd) {
	uint32_t id = 0;
	uint64_t rawID = 0;
	uint8_t clk = 32, encoding = 1, separator = 0, invert = 0;
	char cmdp = param_getchar(Cmd, 0);

	if (strlen(Cmd) == 0 || cmdp == 'h' || cmdp == 'H') return usage_lf_viking_sim();
	id = param_get32ex(Cmd, 0, 0, 16);
	if (id == 0) return usage_lf_viking_sim();

	rawID = getVikingBits(id);

  uint16_t arg1, arg2;
  size_t size = 64;
  arg1 = clk << 8 | encoding;
  arg2 = invert << 8 | separator;

  UsbCommand c = {CMD_ASK_SIM_TAG, {arg1, arg2, size}};
  PrintAndLog("preparing to sim ask data: %d bits", size);
  num_to_bytebits(rawID, 64, c.d.asBytes);
	clearCommandBuffer();
  SendCommand(&c);
  return 0;
}

static command_t CommandTable[] = {
	{"help",  CmdHelp,        1, "This help"},
	{"demod", CmdVikingDemod, 1, "Demodulate a Viking tag from the GraphBuffer"},
	{"read",  CmdVikingRead,  0, "Attempt to read and Extract tag data from the antenna"},
	{"clone", CmdVikingClone, 0, "<8 digit ID number> clone viking tag"},
	{"sim",   CmdVikingSim,   0, "<8 digit ID number> simulate viking tag"},
	{NULL, NULL, 0, NULL}
};

int CmdLFViking(const char *Cmd) {
	CmdsParse(CommandTable, Cmd);
	return 0;
}

int CmdHelp(const char *Cmd) {
	CmdsHelp(CommandTable);
	return 0;
}
