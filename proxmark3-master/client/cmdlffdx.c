//-----------------------------------------------------------------------------
//
// This code is licensed to you under the terms of the GNU GPL, version 2 or,
// at your option, any later version. See the LICENSE.txt file for the text of
// the license.
//-----------------------------------------------------------------------------
// Low frequency fdx-b tag commands
// Differential Biphase, rf/32, 128 bits (known)
//-----------------------------------------------------------------------------

#include "cmdlffdx.h"

#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include "proxmark3.h"
#include "ui.h"         // for PrintAndLog
#include "util.h"
#include "cmdparser.h"
#include "cmddata.h"
#include "cmdmain.h"
#include "cmdlf.h"
#include "crc16.h"
#include "protocols.h"
#include "lfdemod.h"

/*
	FDX-B ISO11784/85 demod  (aka animal tag)  BIPHASE, inverted, rf/32,  with preamble of 00000000001 (128bits)
	8 databits + 1 parity (1)
	CIITT 16 checksum
	NATIONAL CODE, ICAR database
	COUNTRY CODE (ISO3166) or http://cms.abvma.ca/uploads/ManufacturersISOsandCountryCodes.pdf
	FLAG (animal/non-animal)

	38 IDbits   
	10 country code 
	1 extra app bit
	14 reserved bits
	1 animal bit
	16 ccitt CRC chksum over 64bit ID CODE.
	24 appli bits.

	sample: 985121004515220  [ 37FF65B88EF94 ]
*/

static int CmdHelp(const char *Cmd);

int usage_lf_fdx_clone(void){
	PrintAndLog("Clone a FDX-B animal tag to a T55x7 tag.");
	PrintAndLog("Usage: lf fdx clone [h] <country id> <animal id> <Q5>");
	PrintAndLog("Options:");
	PrintAndLog("      h            : This help");
	PrintAndLog("      <country id> : Country id");
	PrintAndLog("      <animal id>  : Animal id");
	// has extended data?
	//reserved/rfu
	//is animal tag
	// extended data
	PrintAndLog("      <Q5>        : Specify write to Q5 (t5555 instead of t55x7)");
	PrintAndLog("");
	PrintAndLog("Sample: lf fdx clone 999 112233");
	return 0;
}

int usage_lf_fdx_sim(void) {
	PrintAndLog("Enables simulation of FDX-B animal tag");
	PrintAndLog("Simulation runs until the button is pressed or another USB command is issued.");
	PrintAndLog("");
	PrintAndLog("Usage:  lf fdx sim [h] <country id> <animal id>");
	PrintAndLog("Options:");
	PrintAndLog("      h            : This help");
	PrintAndLog("      <country id> : Country ID");
	PrintAndLog("      <animal id>  : Animal ID");
	PrintAndLog("");
	PrintAndLog("Sample: lf fdx sim 999 112233");
	return 0;
}
// clearing the topbit needed for the preambl detection. 
static void verify_values(uint32_t countryid, uint64_t animalid){
	if ((animalid & 0x3FFFFFFFFF) != animalid) {
		animalid &= 0x3FFFFFFFFF;
		PrintAndLog("Animal ID Truncated to 38bits: %"PRIx64, animalid);
	}	
	if ( (countryid & 0x3ff) != countryid ) {
		countryid &= 0x3ff;
		PrintAndLog("Country ID Truncated to 10bits: %03d", countryid);
	}
}

int getFDXBits(uint64_t national_id, uint16_t country, uint8_t isanimal, uint8_t isextended, uint32_t extended, uint8_t *bits) {

	// 128bits
	// every 9th bit is 0x01, but we can just fill the rest with 0x01 and overwrite
	memset(bits, 0x01, 128);

	// add preamble ten 0x00 and one 0x01
	memset(bits, 0x00, 10);

	// add reserved 
	num_to_bytebitsLSBF(0x00, 7, bits + 66);
	num_to_bytebitsLSBF(0x00 >> 7, 7, bits + 74);

	// add animal flag - OK
	bits[65] = isanimal;

	// add extended flag - OK
	bits[81] = isextended;

	// add national code 40bits - OK
	num_to_bytebitsLSBF(national_id >> 0, 8, bits+11);
	num_to_bytebitsLSBF(national_id >> 8, 8, bits+20);
	num_to_bytebitsLSBF(national_id >> 16, 8, bits+29);
	num_to_bytebitsLSBF(national_id >> 24, 8, bits+38);
	num_to_bytebitsLSBF(national_id >> 32, 6, bits+47);

	// add country code - OK
	num_to_bytebitsLSBF(country >> 0, 2, bits+53);
	num_to_bytebitsLSBF(country >> 2, 8, bits+56);

	// add crc-16 - OK
	uint8_t raw[8];
	for (uint8_t i=0; i<8; ++i)
		raw[i] = bytebits_to_byte(bits + 11 + i * 9, 8);

	uint16_t crc = crc16_ccitt_kermit(raw, 8);
	num_to_bytebitsLSBF(crc >> 0, 8, bits+83);
	num_to_bytebitsLSBF(crc >> 8, 8, bits+92);

	// extended data - OK
	num_to_bytebitsLSBF( extended >> 0 , 8, bits+101);
	num_to_bytebitsLSBF( extended >> 8 , 8, bits+110);
	num_to_bytebitsLSBF( extended >> 16, 8, bits+119);
	return 1;
}

int CmdFdxDemod(const char *Cmd){

	//Differential Biphase / di-phase (inverted biphase)
	//get binary from ask wave
	if (!ASKbiphaseDemod("0 32 1 0", false)) {
		if (g_debugMode) PrintAndLog("DEBUG: Error - FDX-B ASKbiphaseDemod failed");
		return 0;
	}
	size_t size = DemodBufferLen;
	int preambleIndex = FDXBdemodBI(DemodBuffer, &size);
	if (preambleIndex < 0){
		if (g_debugMode){
			if (preambleIndex == -1)
				PrintAndLog("DEBUG: Error - FDX-B too few bits found");
			else if (preambleIndex == -2)
				PrintAndLog("DEBUG: Error - FDX-B preamble not found");
			else if (preambleIndex == -3)
				PrintAndLog("DEBUG: Error - FDX-B Size not correct: %d", size);
			else
				PrintAndLog("DEBUG: Error - FDX-B ans: %d", preambleIndex);
		}
		return 0;
	}

	// set and leave DemodBuffer intact
	setDemodBuf(DemodBuffer, 128, preambleIndex);
	setClockGrid(g_DemodClock, g_DemodStartIdx + (preambleIndex*g_DemodClock));

	uint8_t bits_no_spacer[117];
	memcpy(bits_no_spacer, DemodBuffer + 11, 117);

	// remove marker bits (1's every 9th digit after preamble) (pType = 2)
	size = removeParity(bits_no_spacer, 0, 9, 2, 117);
	if ( size != 104 ) {
		if (g_debugMode) PrintAndLog("DEBUG: Error removeParity:: %d", size);
		return 0;
	}

	//got a good demod
	uint64_t NationalCode = ((uint64_t)(bytebits_to_byteLSBF(bits_no_spacer+32,6)) << 32) | bytebits_to_byteLSBF(bits_no_spacer,32);
	uint32_t countryCode = bytebits_to_byteLSBF(bits_no_spacer+38,10);
	uint8_t dataBlockBit = bits_no_spacer[48];
	uint32_t reservedCode = bytebits_to_byteLSBF(bits_no_spacer+49,14);
	uint8_t animalBit = bits_no_spacer[63];
	uint32_t crc16 = bytebits_to_byteLSBF(bits_no_spacer+64,16);
	uint32_t extended = bytebits_to_byteLSBF(bits_no_spacer+80,24);

	uint64_t rawid = ((uint64_t)bytebits_to_byte(bits_no_spacer,32)<<32) | bytebits_to_byte(bits_no_spacer+32,32);
	uint8_t raw[8];
	num_to_bytes(rawid, 8, raw);

	if (g_debugMode) {
		PrintAndLog("DEBUG: bits_no_spacer:\n%s",sprint_bin_break(bits_no_spacer,size,16));
		PrintAndLog("DEBUG: Start marker %d;   Size %d", preambleIndex, size);
		PrintAndLog("DEBUG: Raw ID Hex: %s", sprint_hex(raw,8));
	}

	uint16_t calcCrc = crc16_ccitt_kermit(raw, 8);
	PrintAndLog("\nFDX-B / ISO 11784/5 Animal Tag ID Found:");
	PrintAndLog("Animal ID:     %04u-%012" PRIu64, countryCode, NationalCode);
	PrintAndLog("National Code: %012" PRIu64, NationalCode);
	PrintAndLog("CountryCode:   %04u", countryCode);
	PrintAndLog("Reserved Code: %u", reservedCode);
	PrintAndLog("Animal Tag:    %s", animalBit ? "True" : "False");
	PrintAndLog("Has Extended:  %s [0x%X]", dataBlockBit ? "True" : "False", extended);
	PrintAndLog("CRC:           0x%04X - 0x%04X - [%s]\n", crc16, calcCrc, (calcCrc == crc16) ? "Passed" : "Failed");
	
	// set block 0 for later
	//g_DemodConfig = T55x7_MODULATION_DIPHASE | T55x7_BITRATE_RF_32 | 4 << T55x7_MAXBLOCK_SHIFT;
	return 1;
}

int CmdFdxRead(const char *Cmd) {
	lf_read(true, 10000);
	return CmdFdxDemod(Cmd);
}

int CmdFdxClone(const char *Cmd) {

	uint32_t countryid = 0;
	uint64_t animalid = 0;
	uint32_t blocks[5] = {T55x7_MODULATION_DIPHASE | T55x7_BITRATE_RF_32 | 4 << T55x7_MAXBLOCK_SHIFT, 0, 0, 0, 0};
	uint8_t bits[128];
	uint8_t *bs = bits;
	memset(bs, 0, sizeof(bits));
	
	char cmdp = param_getchar(Cmd, 0);
	if (strlen(Cmd) == 0 || cmdp == 'h' || cmdp == 'H') return usage_lf_fdx_clone();

	countryid = param_get32ex(Cmd, 0, 0, 10);
	animalid = param_get64ex(Cmd, 1, 0, 10);
	
	//Q5
	if (param_getchar(Cmd, 2) == 'Q' || param_getchar(Cmd, 2) == 'q') {
		//t5555 (Q5) BITRATE = (RF-2)/2 (iceman)
		blocks[0] = T5555_MODULATION_BIPHASE | T5555_INVERT_OUTPUT | ((32-2)>>1) << T5555_BITRATE_SHIFT | 4 << T5555_MAXBLOCK_SHIFT;
	}
	
	verify_values(countryid, animalid);
	
	// getFDXBits(uint64_t national_id, uint16_t country, uint8_t isanimal, uint8_t isextended, uint32_t extended, uint8_t *bits) 
	if ( !getFDXBits(animalid, countryid, 1, 0, 0, bs)) {
		PrintAndLog("Error with tag bitstream generation.");
		return 1;
	}	
	
	// convert from bit stream to block data
	blocks[1] = bytebits_to_byte(bs,32);
	blocks[2] = bytebits_to_byte(bs+32,32);
	blocks[3] = bytebits_to_byte(bs+64,32);
	blocks[4] = bytebits_to_byte(bs+96,32);

	PrintAndLog("Preparing to clone FDX-B to T55x7 with animal ID: %04u-%"PRIu64, countryid, animalid);
	PrintAndLog("Blk | Data ");
	PrintAndLog("----+------------");
	PrintAndLog(" 00 | 0x%08x", blocks[0]);
	PrintAndLog(" 01 | 0x%08x", blocks[1]);
	PrintAndLog(" 02 | 0x%08x", blocks[2]);
	PrintAndLog(" 03 | 0x%08x", blocks[3]);
	PrintAndLog(" 04 | 0x%08x", blocks[4]);
	
	UsbCommand resp;
	UsbCommand c = {CMD_T55XX_WRITE_BLOCK, {0,0,0}};

	for (int i = 4; i >= 0; --i) {
		c.arg[0] = blocks[i];
		c.arg[1] = i;
		clearCommandBuffer();
		SendCommand(&c);
		if (!WaitForResponseTimeout(CMD_ACK, &resp, T55XX_WRITE_TIMEOUT)){
			PrintAndLog("Error occurred, device did not respond during write operation.");
			return -1;
		}
	}
	return 0;
}

int CmdFdxSim(const char *Cmd) {
	uint32_t countryid = 0;
	uint64_t animalid = 0;

	char cmdp = param_getchar(Cmd, 0);
	if (strlen(Cmd) == 0 || cmdp == 'h' || cmdp == 'H') return usage_lf_fdx_sim();

	countryid = param_get32ex(Cmd, 0, 0, 10);
	animalid = param_get64ex(Cmd, 1, 0, 10);
	
	verify_values(countryid, animalid);

	uint8_t clk = 32, encoding = 2, separator = 0, invert = 1;
	uint16_t arg1, arg2;
	size_t size = 128;
	arg1 = clk << 8 | encoding;
	arg2 = invert << 8 | separator;

	PrintAndLog("Simulating FDX-B animal ID: %04u-%"PRIu64, countryid, animalid);

	UsbCommand c = {CMD_ASK_SIM_TAG, {arg1, arg2, size}};

	 //getFDXBits(uint64_t national_id, uint16_t country, uint8_t isanimal, uint8_t isextended, uint32_t extended, uint8_t *bits) 
	getFDXBits(animalid, countryid, 1, 0, 0, c.d.asBytes);
	clearCommandBuffer();
	SendCommand(&c);
	return 0;
}

static command_t CommandTable[] = {
	{"help",  CmdHelp,    1, "This help"},
	{"demod", CmdFdxDemod,1, "Attempt to extract FDX-B ISO11784/85 data from the GraphBuffer"},
	{"read",  CmdFdxRead, 0, "Attempt to read and extract FDX-B ISO11784/85 data"},
	{"clone", CmdFdxClone,0, "Clone animal ID tag to T55x7 (or to q5/T5555)"},
	{"sim",   CmdFdxSim,  0, "Animal ID tag simulator"},
	{NULL, NULL, 0, NULL}
};

int CmdLFFdx(const char *Cmd) {
	clearCommandBuffer();
	CmdsParse(CommandTable, Cmd);
	return 0;
}

int CmdHelp(const char *Cmd) {
	CmdsHelp(CommandTable);
	return 0;
}
