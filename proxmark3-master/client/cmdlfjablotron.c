//-----------------------------------------------------------------------------
//
// This code is licensed to you under the terms of the GNU GPL, version 2 or,
// at your option, any later version. See the LICENSE.txt file for the text of
// the license.
//-----------------------------------------------------------------------------
// Low frequency jablotron tag commands
// Differential Biphase, RF/64, 64 bits long (complete)
//-----------------------------------------------------------------------------

#include "cmdlfjablotron.h"
#include <string.h>
#include <inttypes.h>
#include <stdbool.h>
#include "proxmark3.h"
#include "ui.h"
#include "util.h"
#include "graph.h"
#include "cmdparser.h"
#include "cmddata.h"
#include "cmdmain.h"
#include "cmdlf.h"
#include "protocols.h"  // for T55xx config register definitions
#include "lfdemod.h"    // parityTest

static int CmdHelp(const char *Cmd);

int usage_lf_jablotron_clone(void) {
	PrintAndLog("clone a Jablotron tag to a T55x7 tag.");
	PrintAndLog("Usage: lf jablotron clone [h] <card ID> <Q5>");
	PrintAndLog("Options:");
	PrintAndLog("      h          : This help");
	PrintAndLog("      <card ID>  : jablotron card ID");
	PrintAndLog("      <Q5>       : specify write to Q5 (t5555 instead of t55x7)");
	PrintAndLog("");
	PrintAndLog("Sample: lf jablotron clone 112233");
	return 0;
}

int usage_lf_jablotron_sim(void) {
	PrintAndLog("Enables simulation of jablotron card with specified card number.");
	PrintAndLog("Simulation runs until the button is pressed or another USB command is issued.");
	PrintAndLog("");
	PrintAndLog("Usage:  lf jablotron sim [h] <card ID>");
	PrintAndLog("Options:");
	PrintAndLog("      h          : This help");
	PrintAndLog("      <card ID>  : jablotron card ID");
	PrintAndLog("");
	PrintAndLog("Sample: lf jablotron sim 112233");
	return 0;
}

static uint8_t jablontron_chksum(uint8_t *bits) {
	uint8_t chksum = 0;
	for (int i=16; i < 56; i += 8) {
		chksum += bytebits_to_byte(bits+i,8);
	}
	chksum ^= 0x3A;	
	return chksum;
}

int getJablotronBits(uint64_t fullcode, uint8_t *bits) {	
	//preamp
	num_to_bytebits(0xFFFF, 16, bits);

	//fullcode
	num_to_bytebits(fullcode, 40, bits+16);

	//chksum byte
	uint8_t chksum = jablontron_chksum(bits);
	num_to_bytebits(chksum, 8, bits+56);
	return 1;
}

// ASK/Diphase fc/64 (inverted Biphase)
// Note: this is not a demod, this is only a detection
// the parameter *bits needs to be demoded before call
// 0xFFFF preamble, 64bits
int JablotronDetect(uint8_t *bits, size_t *size) {
	if (*size < 64*2) return -1; //make sure buffer has enough data
	size_t startIdx = 0;
	uint8_t preamble[] = {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0};
	if (preambleSearch(bits, preamble, sizeof(preamble), size, &startIdx) == 0)
		return -2; //preamble not found
	if (*size != 64) return -3; // wrong demoded size
	
	uint8_t checkchksum = jablontron_chksum(bits+startIdx);
	uint8_t crc = bytebits_to_byte(bits+startIdx+56, 8);
	if ( checkchksum != crc ) return -5;
	return (int)startIdx;
}

//see ASKDemod for what args are accepted
int CmdJablotronDemod(const char *Cmd) {

	//Differential Biphase / di-phase (inverted biphase)
	//get binary from ask wave
	if (!ASKbiphaseDemod("0 64 1 0", false)) {
		if (g_debugMode) PrintAndLog("DEBUG: Error - Jablotron ASKbiphaseDemod failed");
		return 0;
	}
	size_t size = DemodBufferLen;
	int ans = JablotronDetect(DemodBuffer, &size);
	if (ans < 0) {
		if (g_debugMode) {
			if (ans == -1)
				PrintAndLog("DEBUG: Error - Jablotron too few bits found");
			else if (ans == -2)
				PrintAndLog("DEBUG: Error - Jablotron preamble not found");
			else if (ans == -3)
				PrintAndLog("DEBUG: Error - Jablotron size not correct: %d", size);
			else if (ans == -5)
				PrintAndLog("DEBUG: Error - Jablotron checksum failed");
			else
				PrintAndLog("DEBUG: Error - Jablotron ans: %d", ans);
		}
		return 0;
	}

	setDemodBuf(DemodBuffer, 64, ans);
	setClockGrid(g_DemodClock, g_DemodStartIdx + (ans*g_DemodClock));

	//got a good demod
	uint32_t raw1 = bytebits_to_byte(DemodBuffer, 32);
	uint32_t raw2 = bytebits_to_byte(DemodBuffer+32, 32);

	uint64_t id = (( (uint64_t)bytebits_to_byte(DemodBuffer+16, 8) )<< 32) | bytebits_to_byte(DemodBuffer+24,32);

	PrintAndLog("Jablotron Tag Found: Card ID: %"PRIx64" :: Raw: %08X%08X", id, raw1, raw2);

	uint8_t chksum = raw2 & 0xFF;
	PrintAndLog("Checksum: %02X [OK]", chksum);

	// Printed format: 1410-nn-nnnn-nnnn	
	PrintAndLog("Printed: 1410-%02X-%04X-%04X",
		(uint8_t)(id >> 32) & 0xFF,
		(uint16_t)(id >> 16) & 0xFFFF,
		(uint16_t)id & 0xFFFF
	);
	return 1;
}

int CmdJablotronRead(const char *Cmd) {
	lf_read(true, 10000);
	return CmdJablotronDemod(Cmd);
}

int CmdJablotronClone(const char *Cmd) {

	uint64_t fullcode = 0;
	uint32_t blocks[3] = {T55x7_MODULATION_DIPHASE | T55x7_BITRATE_RF_64 | 2 << T55x7_MAXBLOCK_SHIFT, 0, 0};

	uint8_t bits[64];
	uint8_t *bs = bits;
	memset(bs, 0, sizeof(bits));

	char cmdp = param_getchar(Cmd, 0);
	if (strlen(Cmd) == 0 || cmdp == 'h' || cmdp == 'H') return usage_lf_jablotron_clone();

	fullcode = param_get64ex(Cmd, 0, 0, 16);

	//Q5
	if (param_getchar(Cmd, 1) == 'Q' || param_getchar(Cmd, 1) == 'q') {
		//t5555 (Q5) BITRATE = (RF-2)/2 (iceman)
		blocks[0] = T5555_MODULATION_BIPHASE | T5555_INVERT_OUTPUT | ((64-2)>>1) << T5555_BITRATE_SHIFT | 2 << T5555_MAXBLOCK_SHIFT;
	}

	// clearing the topbit needed for the preambl detection. 
	if ((fullcode & 0x7FFFFFFFFF) != fullcode) {
		fullcode &= 0x7FFFFFFFFF;
		PrintAndLog("Card Number Truncated to 39bits: %"PRIx64, fullcode);
	}

	if ( !getJablotronBits(fullcode, bs)) {
		PrintAndLog("Error with tag bitstream generation.");
		return 1;
	}

	blocks[1] = bytebits_to_byte(bs,32);
	blocks[2] = bytebits_to_byte(bs+32,32);

	PrintAndLog("Preparing to clone Jablotron to T55x7 with FullCode: %"PRIx64, fullcode);
	PrintAndLog("Blk | Data ");
	PrintAndLog("----+------------");
	PrintAndLog(" 00 | 0x%08x", blocks[0]);
	PrintAndLog(" 01 | 0x%08x", blocks[1]);
	PrintAndLog(" 02 | 0x%08x", blocks[2]);
	
	UsbCommand resp;
	UsbCommand c = {CMD_T55XX_WRITE_BLOCK, {0,0,0}};

	for (int i = 2; i >= 0; --i) {
		c.arg[0] = blocks[i];
		c.arg[1] = i;
		clearCommandBuffer();
		SendCommand(&c);
		if (!WaitForResponseTimeout(CMD_ACK, &resp, T55XX_WRITE_TIMEOUT)) {
			PrintAndLog("Error occurred, device did not respond during write operation.");
			return -1;
		}
	}
	return 0;
}

int CmdJablotronSim(const char *Cmd) {
	uint64_t fullcode = 0;

	char cmdp = param_getchar(Cmd, 0);
	if (strlen(Cmd) == 0 || cmdp == 'h' || cmdp == 'H') return usage_lf_jablotron_sim();

	fullcode = param_get64ex(Cmd, 0, 0, 16);

	// clearing the topbit needed for the preambl detection. 
	if ((fullcode & 0x7FFFFFFFFF) != fullcode) {
		fullcode &= 0x7FFFFFFFFF;
		PrintAndLog("Card Number Truncated to 39bits: %"PRIx64, fullcode);
	}
	
	uint8_t clk = 64, encoding = 2, separator = 0, invert = 1;
	uint16_t arg1, arg2;
	size_t size = 64;
	arg1 = clk << 8 | encoding;
	arg2 = invert << 8 | separator;

	PrintAndLog("Simulating Jablotron - FullCode: %"PRIx64, fullcode);

	UsbCommand c = {CMD_ASK_SIM_TAG, {arg1, arg2, size}};
	getJablotronBits(fullcode, c.d.asBytes);
	clearCommandBuffer();
	SendCommand(&c);
	return 0;
}

static command_t CommandTable[] = {
	{"help",  CmdHelp,           1, "This help"},
	{"demod", CmdJablotronDemod, 1, "Attempt to read and extract tag data from the GraphBuffer"},
	{"read",  CmdJablotronRead,  0, "Attempt to read and extract tag data from the antenna"},
	{"clone", CmdJablotronClone, 0, "clone jablotron tag"},
	{"sim",   CmdJablotronSim,   0, "simulate jablotron tag"},
	{NULL, NULL, 0, NULL}
};

int CmdLFJablotron(const char *Cmd) {
	clearCommandBuffer();
	CmdsParse(CommandTable, Cmd);
	return 0;
}

int CmdHelp(const char *Cmd) {
	CmdsHelp(CommandTable);
	return 0;
}
