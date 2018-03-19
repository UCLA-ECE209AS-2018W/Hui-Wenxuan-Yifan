//-----------------------------------------------------------------------------
//
// This code is licensed to you under the terms of the GNU GPL, version 2 or,
// at your option, any later version. See the LICENSE.txt file for the text of
// the license.
//-----------------------------------------------------------------------------
// Low frequency Indala commands
// PSK1, rf/32, 64 or 224 bits (known)
//-----------------------------------------------------------------------------

#include <stdio.h>
#include <string.h>
#include "cmdlfindala.h"
#include "proxmark3.h"
#include "ui.h"
#include "graph.h"
#include "cmdparser.h"
#include "cmddata.h"  //for g_debugMode, demodbuff cmds
#include "lfdemod.h"  //for indala26decode
#include "util.h"     //for sprint_bin_break
#include "cmdlf.h"    //for CmdLFRead
#include "cmdmain.h"  //for clearCommandBuffer

static int CmdHelp(const char *Cmd);

// Indala 26 bit decode
// by marshmellow
// optional arguments - same as PSKDemod (clock & invert & maxerr)
int CmdIndalaDecode(const char *Cmd) {
	int ans;
	if (strlen(Cmd)>0) {
		ans = PSKDemod(Cmd, 0);
	} else { //default to RF/32
		ans = PSKDemod("32", 0);
	}

	if (!ans) {
		if (g_debugMode) PrintAndLog("Error1: %i",ans);
		return 0;
	}
	uint8_t invert=0;
	size_t size = DemodBufferLen;
	int startIdx = indala64decode(DemodBuffer, &size, &invert);
	if (startIdx < 0 || size != 64) {
		// try 224 indala
		invert = 0;
		size = DemodBufferLen;
		startIdx = indala224decode(DemodBuffer, &size, &invert);
		if (startIdx < 0 || size != 224) {
			if (g_debugMode) PrintAndLog("Error2: %i",startIdx);
			return -1;
		}
	}
	setDemodBuf(DemodBuffer, size, (size_t)startIdx);
	setClockGrid(g_DemodClock, g_DemodStartIdx + (startIdx*g_DemodClock));
	if (invert)
		if (g_debugMode)
			PrintAndLog("Had to invert bits");

	PrintAndLog("BitLen: %d",DemodBufferLen);
	//convert UID to HEX
	uint32_t uid1, uid2, uid3, uid4, uid5, uid6, uid7;
	uid1=bytebits_to_byte(DemodBuffer,32);
	uid2=bytebits_to_byte(DemodBuffer+32,32);
	if (DemodBufferLen==64) {
		PrintAndLog("Indala UID=%s (%x%08x)", sprint_bin_break(DemodBuffer,DemodBufferLen,16), uid1, uid2);
	} else if (DemodBufferLen==224) {
		uid3=bytebits_to_byte(DemodBuffer+64,32);
		uid4=bytebits_to_byte(DemodBuffer+96,32);
		uid5=bytebits_to_byte(DemodBuffer+128,32);
		uid6=bytebits_to_byte(DemodBuffer+160,32);
		uid7=bytebits_to_byte(DemodBuffer+192,32);
		PrintAndLog("Indala UID=%s (%x%08x%08x%08x%08x%08x%08x)", 
		    sprint_bin_break(DemodBuffer,DemodBufferLen,16), uid1, uid2, uid3, uid4, uid5, uid6, uid7);
	}
	if (g_debugMode) {
		PrintAndLog("DEBUG: printing demodbuffer:");
		printDemodBuff();
	}
	return 1;
}

int CmdIndalaRead(const char *Cmd) {
	lf_read(true, 30000);
	return CmdIndalaDecode("");
}

// older alternative indala demodulate (has some positives and negatives)
// returns false positives more often - but runs against more sets of samples
// poor psk signal can be difficult to demod this approach might succeed when the other fails
// but the other appears to currently be more accurate than this approach most of the time.
int CmdIndalaDemod(const char *Cmd) {
	// Usage: recover 64bit UID by default, specify "224" as arg to recover a 224bit UID

	int state = -1;
	int count = 0;
	int i, j;

	// worst case with GraphTraceLen=64000 is < 4096
	// under normal conditions it's < 2048

	uint8_t rawbits[4096];
	int rawbit = 0;
	int worst = 0, worstPos = 0;

	//clear clock grid and demod plot
	setClockGrid(0, 0);
	DemodBufferLen = 0;
	
	// PrintAndLog("Expecting a bit less than %d raw bits", GraphTraceLen / 32);
	// loop through raw signal - since we know it is psk1 rf/32 fc/2 skip every other value (+=2)
	for (i = 0; i < GraphTraceLen-1; i += 2) {
		count += 1;
		if ((GraphBuffer[i] > GraphBuffer[i + 1]) && (state != 1)) {
			// appears redundant - marshmellow
			if (state == 0) {
				for (j = 0; j <  count - 8; j += 16) {
					rawbits[rawbit++] = 0;
				}
				if ((abs(count - j)) > worst) {
					worst = abs(count - j);
					worstPos = i;
				}
			}
			state = 1;
			count = 0;
		} else if ((GraphBuffer[i] < GraphBuffer[i + 1]) && (state != 0)) {
			//appears redundant
			if (state == 1) {
				for (j = 0; j <  count - 8; j += 16) {
					rawbits[rawbit++] = 1;
				}
				if ((abs(count - j)) > worst) {
					worst = abs(count - j);
					worstPos = i;
				}
			}
			state = 0;
			count = 0;
		}
	}
	
	if (rawbit>0){
		PrintAndLog("Recovered %d raw bits, expected: %d", rawbit, GraphTraceLen/32);
		PrintAndLog("worst metric (0=best..7=worst): %d at pos %d", worst, worstPos);
	} else {
		return 0;
	}

	// Finding the start of a UID
	int uidlen, long_wait;
	if (strcmp(Cmd, "224") == 0) {
		uidlen = 224;
		long_wait = 30;
	} else {
		uidlen = 64;
		long_wait = 29;
	}

	int start;
	int first = 0;
	for (start = 0; start <= rawbit - uidlen; start++) {
		first = rawbits[start];
		for (i = start; i < start + long_wait; i++) {
			if (rawbits[i] != first) {
				break;
			}
		}
		if (i == (start + long_wait)) {
			break;
		}
	}
	
	if (start == rawbit - uidlen + 1) {
		PrintAndLog("nothing to wait for");
		return 0;
	}

	// Inverting signal if needed
	if (first == 1) {
		for (i = start; i < rawbit; i++) {
			rawbits[i] = !rawbits[i];
		}
	}

	// Dumping UID
	uint8_t bits[224] = {0x00};
	char showbits[225] = {0x00};
	int bit;
	i = start;
	int times = 0;
	
	if (uidlen > rawbit) {
		PrintAndLog("Warning: not enough raw bits to get a full UID");
		for (bit = 0; bit < rawbit; bit++) {
			bits[bit] = rawbits[i++];
			// As we cannot know the parity, let's use "." and "/"
			showbits[bit] = '.' + bits[bit];
		}
		showbits[bit+1]='\0';
		PrintAndLog("Partial UID=%s", showbits);
		return 0;
	} else {
		for (bit = 0; bit < uidlen; bit++) {
			bits[bit] = rawbits[i++];
			showbits[bit] = '0' + bits[bit];
		}
		times = 1;
	}
	
	//convert UID to HEX
	uint32_t uid1, uid2, uid3, uid4, uid5, uid6, uid7;
	int idx;
	uid1 = uid2 = 0;
	
	if (uidlen==64){
		for( idx=0; idx<64; idx++) {
				if (showbits[idx] == '0') {
				uid1=(uid1<<1)|(uid2>>31);
				uid2=(uid2<<1)|0;
				} else {
				uid1=(uid1<<1)|(uid2>>31);
				uid2=(uid2<<1)|1;
				} 
			}
		PrintAndLog("UID=%s (%x%08x)", showbits, uid1, uid2);
	}
	else {
		uid3 = uid4 = uid5 = uid6 = uid7 = 0;

		for( idx=0; idx<224; idx++) {
				uid1=(uid1<<1)|(uid2>>31);
				uid2=(uid2<<1)|(uid3>>31);
				uid3=(uid3<<1)|(uid4>>31);
				uid4=(uid4<<1)|(uid5>>31);
				uid5=(uid5<<1)|(uid6>>31);
				uid6=(uid6<<1)|(uid7>>31);
			
			if (showbits[idx] == '0') 
				uid7 = (uid7<<1) | 0;
			else 
				uid7 = (uid7<<1) | 1;
			}
		PrintAndLog("UID=%s (%x%08x%08x%08x%08x%08x%08x)", showbits, uid1, uid2, uid3, uid4, uid5, uid6, uid7);
	}

	// Checking UID against next occurrences
		int failed = 0;
	for (; i + uidlen <= rawbit;) {
		failed = 0;
		for (bit = 0; bit < uidlen; bit++) {
			if (bits[bit] != rawbits[i++]) {
				failed = 1;
				break;
			}
		}
		if (failed == 1) {
			break;
		}
		times += 1;
	}

	PrintAndLog("Occurrences: %d (expected %d)", times, (rawbit - start) / uidlen);

	// Remodulating for tag cloning
	// HACK: 2015-01-04 this will have an impact on our new way of seening lf commands (demod) 
	// since this changes graphbuffer data.
	GraphTraceLen = 32*uidlen;
	i = 0;
	int phase = 0;
	for (bit = 0; bit < uidlen; bit++) {
		if (bits[bit] == 0) {
			phase = 0;
		} else {
			phase = 1;
		}
		int j;
		for (j = 0; j < 32; j++) {
			GraphBuffer[i++] = phase;
			phase = !phase;
		}
	}

	RepaintGraphWindow();
	return 1;
}

int CmdIndalaClone(const char *Cmd) {
	UsbCommand c;
	unsigned int uid1, uid2, uid3, uid4, uid5, uid6, uid7;

	uid1 =  uid2 = uid3 = uid4 = uid5 = uid6 = uid7 = 0;
	int n = 0, i = 0;

	if (strchr(Cmd,'l') != 0) {
		while (sscanf(&Cmd[i++], "%1x", &n ) == 1) {
			uid1 = (uid1 << 4) | (uid2 >> 28);
			uid2 = (uid2 << 4) | (uid3 >> 28);
			uid3 = (uid3 << 4) | (uid4 >> 28);
			uid4 = (uid4 << 4) | (uid5 >> 28);
			uid5 = (uid5 << 4) | (uid6 >> 28);
			uid6 = (uid6 << 4) | (uid7 >> 28);
			uid7 = (uid7 << 4) | (n & 0xf);
		}
		PrintAndLog("Cloning 224bit tag with UID %x%08x%08x%08x%08x%08x%08x", uid1, uid2, uid3, uid4, uid5, uid6, uid7);
		c.cmd = CMD_INDALA_CLONE_TAG_L;
		c.d.asDwords[0] = uid1;
		c.d.asDwords[1] = uid2;
		c.d.asDwords[2] = uid3;
		c.d.asDwords[3] = uid4;
		c.d.asDwords[4] = uid5;
		c.d.asDwords[5] = uid6;
		c.d.asDwords[6] = uid7;
	} else {
		while (sscanf(&Cmd[i++], "%1x", &n ) == 1) {
			uid1 = (uid1 << 4) | (uid2 >> 28);
			uid2 = (uid2 << 4) | (n & 0xf);
		}
		PrintAndLog("Cloning 64bit tag with UID %x%08x", uid1, uid2);
		c.cmd = CMD_INDALA_CLONE_TAG;
		c.arg[0] = uid1;
		c.arg[1] = uid2;
	}

	clearCommandBuffer();
	SendCommand(&c);
	return 0;
}

static command_t CommandTable[] = {
	{"help",     CmdHelp,         1, "This help"},
	{"demod",    CmdIndalaDecode, 1, "[clock] [invert<0|1>] -- Demodulate an indala tag (PSK1) from GraphBuffer (args optional)"},
	{"read",     CmdIndalaRead,   0, "Read an Indala Prox tag from the antenna"},
	{"clone",    CmdIndalaClone,  0, "<UID> ['l']-- Clone Indala to T55x7 (tag must be on antenna)(UID in HEX)(option 'l' for 224 UID"},
	{"altdemod", CmdIndalaDemod,  1, "['224'] -- Alternative method to Demodulate samples for Indala 64 bit UID (option '224' for 224 bit)"},
	//{"sim",      CmdIndalaSim,    0, "<ID> -- indala tag simulator"},
	{NULL, NULL, 0, NULL}
};

int CmdLFINDALA(const char *Cmd) {
	CmdsParse(CommandTable, Cmd);
	return 0;
}

int CmdHelp(const char *Cmd) {
	CmdsHelp(CommandTable);
	return 0;
}
