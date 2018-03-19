//-----------------------------------------------------------------------------
// Copyright (C) 2010 iZsh <izsh at fail0verflow.com>
//
// This code is licensed to you under the terms of the GNU GPL, version 2 or,
// at your option, any later version. See the LICENSE.txt file for the text of
// the license.
//-----------------------------------------------------------------------------
// Data and Graph commands
//-----------------------------------------------------------------------------

#include <stdio.h>    // also included in util.h
#include <string.h>   // also included in util.h
#include <inttypes.h>
#include <limits.h>   // for CmdNorm INT_MIN && INT_MAX
#include "data.h"     // also included in util.h
#include "cmddata.h"
#include "util.h"
#include "cmdmain.h"
#include "proxmark3.h"
#include "ui.h"       // for show graph controls
#include "graph.h"    // for graph data
#include "cmdparser.h"// already included in cmdmain.h
#include "usb_cmd.h"  // already included in cmdmain.h and proxmark3.h
#include "lfdemod.h"  // for demod code
#include "loclass/cipherutils.h" // for decimating samples in getsamples
#include "cmdlfem4x.h"// for em410x demod

uint8_t DemodBuffer[MAX_DEMOD_BUF_LEN];
uint8_t g_debugMode=0;
size_t DemodBufferLen=0;
int g_DemodStartIdx=0;
int g_DemodClock=0;

static int CmdHelp(const char *Cmd);

//set the demod buffer with given array of binary (one bit per byte)
//by marshmellow
void setDemodBuf(uint8_t *buff, size_t size, size_t startIdx)
{
	if (buff == NULL) 
		return;

	if ( size > MAX_DEMOD_BUF_LEN - startIdx)
		size = MAX_DEMOD_BUF_LEN - startIdx;

	size_t i = 0;
	for (; i < size; i++){
		DemodBuffer[i]=buff[startIdx++];
	}
	DemodBufferLen=size;
	return;
}

bool getDemodBuf(uint8_t *buff, size_t *size) {
	if (buff == NULL) return false;
	if (size == NULL) return false;
	if (*size == 0) return false;

	*size = (*size > DemodBufferLen) ? DemodBufferLen : *size;

	memcpy(buff, DemodBuffer, *size);
	return true;
}

// option '1' to save DemodBuffer any other to restore
void save_restoreDB(uint8_t saveOpt)
{
	static uint8_t SavedDB[MAX_DEMOD_BUF_LEN];
	static size_t SavedDBlen;
	static bool DB_Saved = false;
	static int savedDemodStartIdx = 0;
	static int savedDemodClock = 0;

	if (saveOpt == GRAPH_SAVE) { //save

		memcpy(SavedDB, DemodBuffer, sizeof(DemodBuffer));
		SavedDBlen = DemodBufferLen;
		DB_Saved=true;
		savedDemodStartIdx = g_DemodStartIdx;
		savedDemodClock = g_DemodClock;
	} else if (DB_Saved) { //restore
		memcpy(DemodBuffer, SavedDB, sizeof(DemodBuffer));
		DemodBufferLen = SavedDBlen;
		g_DemodClock = savedDemodClock;
		g_DemodStartIdx = savedDemodStartIdx;
	}
	return;
}

int CmdSetDebugMode(const char *Cmd)
{
	int demod=0;
	sscanf(Cmd, "%i", &demod);
	g_debugMode=(uint8_t)demod;
	return 1;
}

int usage_data_printdemodbuf(){
		PrintAndLog("Usage: data printdemodbuffer x o <offset> l <length>");
		PrintAndLog("Options:        ");
		PrintAndLog("       h          This help");
		PrintAndLog("       x          output in hex (omit for binary output)");
		PrintAndLog("       o <offset> enter offset in # of bits");
		PrintAndLog("       l <length> enter length to print in # of bits or hex characters respectively");
		return 0;	
}

//by marshmellow
void printDemodBuff(void)
{
	int bitLen = DemodBufferLen;
	if (bitLen<1) {
		PrintAndLog("no bits found in demod buffer");
		return;
	}
	if (bitLen>512) bitLen=512; //max output to 512 bits if we have more - should be plenty

	char *bin = sprint_bin_break(DemodBuffer,bitLen,16);
	PrintAndLog("%s",bin);

	return;
}

int CmdPrintDemodBuff(const char *Cmd)
{
	char hex[512]={0x00};
	bool hexMode = false;
	bool errors = false;
	uint32_t offset = 0; //could be size_t but no param_get16...
	uint32_t length = 512;
	char cmdp = 0;
	while(param_getchar(Cmd, cmdp) != 0x00)
	{
		switch(param_getchar(Cmd, cmdp))
		{
		case 'h':
		case 'H':
			return usage_data_printdemodbuf();
		case 'x':
		case 'X':
			hexMode = true;
			cmdp++;
			break;
		case 'o':
		case 'O':
			offset = param_get32ex(Cmd, cmdp+1, 0, 10);
			if (!offset) errors = true;
			cmdp += 2;
			break;
		case 'l':
		case 'L':
			length = param_get32ex(Cmd, cmdp+1, 512, 10);
			if (!length) errors = true;
			cmdp += 2;
			break;
		default:
			PrintAndLog("Unknown parameter '%c'", param_getchar(Cmd, cmdp));
			errors = true;
			break;
		}
		if(errors) break;
	}
	//Validations
	if(errors) return usage_data_printdemodbuf();
	length = (length > (DemodBufferLen-offset)) ? DemodBufferLen-offset : length; 
	int numBits = (length) & 0x00FFC; //make sure we don't exceed our string

	if (hexMode){
		char *buf = (char *) (DemodBuffer + offset);
		numBits = (numBits > sizeof(hex)) ? sizeof(hex) : numBits;
		numBits = binarraytohex(hex, buf, numBits);
		if (numBits==0) return 0;
		PrintAndLog("DemodBuffer: %s",hex);		
	} else {
		PrintAndLog("DemodBuffer:\n%s", sprint_bin_break(DemodBuffer+offset,numBits,16));
	}
	return 1;
}

//by marshmellow
//this function strictly converts >1 to 1 and <1 to 0 for each sample in the graphbuffer
int CmdGetBitStream(const char *Cmd)
{
	int i;
	CmdHpf(Cmd);
	for (i = 0; i < GraphTraceLen; i++) {
		if (GraphBuffer[i] >= 1) {
			GraphBuffer[i] = 1;
		} else {
			GraphBuffer[i] = 0;
		}
	}
	RepaintGraphWindow();
	return 0;
}

//by marshmellow
//Cmd Args: Clock, invert, maxErr, maxLen as integers and amplify as char == 'a'
//   (amp may not be needed anymore)
//verbose will print results and demoding messages
//emSearch will auto search for EM410x format in bitstream
//askType switches decode: ask/raw = 0, ask/manchester = 1 
int ASKDemod_ext(const char *Cmd, bool verbose, bool emSearch, uint8_t askType, bool *stCheck) {
	int invert=0;
	int clk=0;
	int maxErr=100;
	int maxLen=0;
	uint8_t askamp = 0;
	char amp = param_getchar(Cmd, 0);
	uint8_t BitStream[MAX_GRAPH_TRACE_LEN]={0};
	sscanf(Cmd, "%i %i %i %i %c", &clk, &invert, &maxErr, &maxLen, &amp);
	if (!maxLen) maxLen = BIGBUF_SIZE;
	if (invert != 0 && invert != 1) {
		PrintAndLog("Invalid argument: %s", Cmd);
		return 0;
	}
	if (clk==1){
		invert=1;
		clk=0;
	}
	size_t BitLen = getFromGraphBuf(BitStream);
	if (g_debugMode) PrintAndLog("DEBUG: Bitlen from grphbuff: %d",BitLen);
	if (BitLen < 255) return 0;
	if (maxLen < BitLen && maxLen != 0) BitLen = maxLen;
	int foundclk = 0;
	//amp before ST check
	if (amp == 'a' || amp == 'A') {
		askAmp(BitStream, BitLen); 
	}
	bool st = false;
	size_t ststart = 0, stend = 0;
	if (*stCheck) st = DetectST(BitStream, &BitLen, &foundclk, &ststart, &stend);
	*stCheck = st;
	if (st) {
		clk = (clk == 0) ? foundclk : clk;
		CursorCPos = ststart;
		CursorDPos = stend;
		if (verbose || g_debugMode) PrintAndLog("\nFound Sequence Terminator - First one is shown by orange and blue graph markers");
		//Graph ST trim (for testing)
		//for (int i = 0; i < BitLen; i++) {
		//	GraphBuffer[i] = BitStream[i]-128;
		//}
		//RepaintGraphWindow();
	}
	int startIdx = 0;
	int errCnt = askdemod_ext(BitStream, &BitLen, &clk, &invert, maxErr, askamp, askType, &startIdx);
	if (errCnt<0 || BitLen<16){  //if fatal error (or -1)
		if (g_debugMode) PrintAndLog("DEBUG: no data found %d, errors:%d, bitlen:%d, clock:%d",errCnt,invert,BitLen,clk);
		return 0;
	}
	if (errCnt > maxErr){
		if (g_debugMode) PrintAndLog("DEBUG: Too many errors found, errors:%d, bits:%d, clock:%d",errCnt, BitLen, clk);
		return 0;
	}
	if (verbose || g_debugMode) PrintAndLog("\nUsing Clock:%d, Invert:%d, Bits Found:%d",clk,invert,BitLen);
	//output
	setDemodBuf(BitStream,BitLen,0);
	setClockGrid(clk, startIdx);

	if (verbose || g_debugMode){
		if (errCnt>0) PrintAndLog("# Errors during Demoding (shown as 7 in bit stream): %d",errCnt);
		if (askType) PrintAndLog("ASK/Manchester - Clock: %d - Decoded bitstream:",clk);
		else PrintAndLog("ASK/Raw - Clock: %d - Decoded bitstream:",clk);
		// Now output the bitstream to the scrollback by line of 16 bits
		printDemodBuff();
		
	}
	uint64_t lo = 0;
	uint32_t hi = 0;
	if (emSearch){
		AskEm410xDecode(true, &hi, &lo);
	}
	return 1;
}
int ASKDemod(const char *Cmd, bool verbose, bool emSearch, uint8_t askType) {
	bool st = false;
	return ASKDemod_ext(Cmd, verbose, emSearch, askType, &st);
}

//by marshmellow
//takes 5 arguments - clock, invert, maxErr, maxLen as integers and amplify as char == 'a'
//attempts to demodulate ask while decoding manchester
//prints binary found and saves in graphbuffer for further commands
int Cmdaskmandemod(const char *Cmd)
{
	char cmdp = param_getchar(Cmd, 0);
	if (strlen(Cmd) > 45 || cmdp == 'h' || cmdp == 'H') {
		PrintAndLog("Usage:  data rawdemod am <s> [clock] <invert> [maxError] [maxLen] [amplify]");
		PrintAndLog("     ['s'] optional, check for Sequence Terminator");
		PrintAndLog("     [set clock as integer] optional, if not set, autodetect");
		PrintAndLog("     <invert>, 1 to invert output");
		PrintAndLog("     [set maximum allowed errors], default = 100");
		PrintAndLog("     [set maximum Samples to read], default = 32768 (512 bits at rf/64)");
		PrintAndLog("     <amplify>, 'a' to attempt demod with ask amplification, default = no amp");
		PrintAndLog("");
		PrintAndLog("    sample: data rawdemod am        = demod an ask/manchester tag from GraphBuffer");
		PrintAndLog("          : data rawdemod am 32     = demod an ask/manchester tag from GraphBuffer using a clock of RF/32");
		PrintAndLog("          : data rawdemod am 32 1   = demod an ask/manchester tag from GraphBuffer using a clock of RF/32 and inverting data");
		PrintAndLog("          : data rawdemod am 1      = demod an ask/manchester tag from GraphBuffer while inverting data");
		PrintAndLog("          : data rawdemod am 64 1 0 = demod an ask/manchester tag from GraphBuffer using a clock of RF/64, inverting data and allowing 0 demod errors");
		return 0;
	}
	bool st = true;
	if (Cmd[0]=='s') 
		return ASKDemod_ext(Cmd++, true, false, 1, &st);
	else if (Cmd[1] == 's')
		return ASKDemod_ext(Cmd+=2, true, false, 1, &st);
	else
		return ASKDemod(Cmd, true, false, 1);
}

//by marshmellow
//manchester decode
//stricktly take 10 and 01 and convert to 0 and 1
int Cmdmandecoderaw(const char *Cmd)
{
	int i =0;
	int errCnt=0;
	size_t size=0;
	int invert=0;
	int maxErr = 20;
	char cmdp = param_getchar(Cmd, 0);
	if (strlen(Cmd) > 5 || cmdp == 'h' || cmdp == 'H') {
		PrintAndLog("Usage:  data manrawdecode [invert] [maxErr]");
		PrintAndLog("     Takes 10 and 01 and converts to 0 and 1 respectively");
		PrintAndLog("     --must have binary sequence in demodbuffer (run data askrawdemod first)");
		PrintAndLog("  [invert]  invert output");		
		PrintAndLog("  [maxErr]  set number of errors allowed (default = 20)");		
		PrintAndLog("");
		PrintAndLog("    sample: data manrawdecode   = decode manchester bitstream from the demodbuffer");
		return 0;
	}
	if (DemodBufferLen==0) return 0;
	uint8_t BitStream[MAX_DEMOD_BUF_LEN]={0};
	int high=0,low=0;
	for (;i<DemodBufferLen;++i){
		if (DemodBuffer[i]>high) high=DemodBuffer[i];
		else if(DemodBuffer[i]<low) low=DemodBuffer[i];
		BitStream[i]=DemodBuffer[i];
	}
	if (high>7 || low <0 ){
		PrintAndLog("Error: please raw demod the wave first then manchester raw decode");
		return 0;
	}

	sscanf(Cmd, "%i %i", &invert, &maxErr);
	size=i;
	uint8_t alignPos = 0;
	errCnt=manrawdecode(BitStream, &size, invert, &alignPos);
	if (errCnt>=maxErr){
		PrintAndLog("Too many errors: %d",errCnt);
		return 0;
	}
	PrintAndLog("Manchester Decoded - # errors:%d - data:",errCnt);
	PrintAndLog("%s", sprint_bin_break(BitStream, size, 16));
	if (errCnt==0){
		uint64_t id = 0;
		uint32_t hi = 0;
		size_t idx=0;
		if (Em410xDecode(BitStream, &size, &idx, &hi, &id)){
			//need to adjust to set bitstream back to manchester encoded data
			//setDemodBuf(BitStream, size, idx);

			printEM410x(hi, id);
		}
	}
	return 1;
}

//by marshmellow
//biphase decode
//take 01 or 10 = 0 and 11 or 00 = 1
//takes 2 arguments "offset" default = 0 if 1 it will shift the decode by one bit
// and "invert" default = 0 if 1 it will invert output
//  the argument offset allows us to manually shift if the output is incorrect - [EDIT: now auto detects]
int CmdBiphaseDecodeRaw(const char *Cmd)
{
	size_t size=0;
	int offset=0, invert=0, maxErr=20, errCnt=0;
	char cmdp = param_getchar(Cmd, 0);
	if (strlen(Cmd) > 3 || cmdp == 'h' || cmdp == 'H') {
		PrintAndLog("Usage:  data biphaserawdecode [offset] [invert] [maxErr]");
		PrintAndLog("     Converts 10 or 01 to 1 and 11 or 00 to 0");
		PrintAndLog("     --must have binary sequence in demodbuffer (run data askrawdemod first)");
		PrintAndLog("     --invert for Conditional Dephase Encoding (CDP) AKA Differential Manchester");
		PrintAndLog("");
		PrintAndLog("     [offset <0|1>], set to 0 not to adjust start position or to 1 to adjust decode start position");
		PrintAndLog("     [invert <0|1>], set to 1 to invert output");
		PrintAndLog("     [maxErr int],   set max errors tolerated - default=20");
		PrintAndLog("");
		PrintAndLog("    sample: data biphaserawdecode     = decode biphase bitstream from the demodbuffer");
		PrintAndLog("    sample: data biphaserawdecode 1 1 = decode biphase bitstream from the demodbuffer, set offset, and invert output");
		return 0;
	}
	sscanf(Cmd, "%i %i %i", &offset, &invert, &maxErr);
	if (DemodBufferLen==0) {
		PrintAndLog("DemodBuffer Empty - run 'data rawdemod ar' first");
		return 0;
	}
	uint8_t BitStream[MAX_DEMOD_BUF_LEN]={0};
	size = sizeof(BitStream);
	if ( !getDemodBuf(BitStream, &size) ) return 0;
	errCnt=BiphaseRawDecode(BitStream, &size, &offset, invert);
	if (errCnt<0){
		PrintAndLog("Error during decode:%d", errCnt);
		return 0;
	}
	if (errCnt>maxErr){
		PrintAndLog("Too many errors attempting to decode: %d",errCnt);
		return 0;
	}

	if (errCnt>0){
		PrintAndLog("# Errors found during Demod (shown as 7 in bit stream): %d",errCnt);
	}

	PrintAndLog("Biphase Decoded using offset: %d - # invert:%d - data:",offset,invert);
	PrintAndLog("%s", sprint_bin_break(BitStream, size, 16));
	
	if (offset) setDemodBuf(DemodBuffer,DemodBufferLen-offset, offset);  //remove first bit from raw demod
	setClockGrid(g_DemodClock, g_DemodStartIdx + g_DemodClock*offset/2);
	return 1;
}

//by marshmellow
// - ASK Demod then Biphase decode GraphBuffer samples
int ASKbiphaseDemod(const char *Cmd, bool verbose)
{
	//ask raw demod GraphBuffer first
	int offset=0, clk=0, invert=0, maxErr=0;
	sscanf(Cmd, "%i %i %i %i", &offset, &clk, &invert, &maxErr);

	uint8_t BitStream[MAX_GRAPH_TRACE_LEN];	  
	size_t size = getFromGraphBuf(BitStream);
	int startIdx = 0;
	//invert here inverts the ask raw demoded bits which has no effect on the demod, but we need the pointer
	int errCnt = askdemod_ext(BitStream, &size, &clk, &invert, maxErr, 0, 0, &startIdx);  
	if ( errCnt < 0 || errCnt > maxErr ) {   
		if (g_debugMode) PrintAndLog("DEBUG: no data or error found %d, clock: %d", errCnt, clk);  
			return 0;  
	}

	//attempt to Biphase decode BitStream
	errCnt = BiphaseRawDecode(BitStream, &size, &offset, invert);
	if (errCnt < 0){
		if (g_debugMode || verbose) PrintAndLog("Error BiphaseRawDecode: %d", errCnt);
		return 0;
	}
	if (errCnt > maxErr) {
		if (g_debugMode || verbose) PrintAndLog("Error BiphaseRawDecode too many errors: %d", errCnt);
		return 0;
	}
	//success set DemodBuffer and return
	setDemodBuf(BitStream, size, 0);
	setClockGrid(clk, startIdx + clk*offset/2);
	if (g_debugMode || verbose){
		PrintAndLog("Biphase Decoded using offset: %d - clock: %d - # errors:%d - data:",offset,clk,errCnt);
		printDemodBuff();
	}
	return 1;
}
//by marshmellow - see ASKbiphaseDemod
int Cmdaskbiphdemod(const char *Cmd)
{
	char cmdp = param_getchar(Cmd, 0);
	if (strlen(Cmd) > 25 || cmdp == 'h' || cmdp == 'H') {
		PrintAndLog("Usage:  data rawdemod ab [offset] [clock] <invert> [maxError] [maxLen] <amplify>");
		PrintAndLog("     [offset], offset to begin biphase, default=0");
		PrintAndLog("     [set clock as integer] optional, if not set, autodetect");
		PrintAndLog("     <invert>, 1 to invert output");
		PrintAndLog("     [set maximum allowed errors], default = 100");
		PrintAndLog("     [set maximum Samples to read], default = 32768 (512 bits at rf/64)");
		PrintAndLog("     <amplify>, 'a' to attempt demod with ask amplification, default = no amp");
		PrintAndLog("     NOTE: <invert>  can be entered as second or third argument");
		PrintAndLog("     NOTE: <amplify> can be entered as first, second or last argument");
		PrintAndLog("     NOTE: any other arg must have previous args set to work");
		PrintAndLog("");
		PrintAndLog("     NOTE: --invert for Conditional Dephase Encoding (CDP) AKA Differential Manchester");
		PrintAndLog("");
		PrintAndLog("    sample: data rawdemod ab              = demod an ask/biph tag from GraphBuffer");
		PrintAndLog("          : data rawdemod ab 0 a          = demod an ask/biph tag from GraphBuffer, amplified");
		PrintAndLog("          : data rawdemod ab 1 32         = demod an ask/biph tag from GraphBuffer using an offset of 1 and a clock of RF/32");
		PrintAndLog("          : data rawdemod ab 0 32 1       = demod an ask/biph tag from GraphBuffer using a clock of RF/32 and inverting data");
		PrintAndLog("          : data rawdemod ab 0 1          = demod an ask/biph tag from GraphBuffer while inverting data");
		PrintAndLog("          : data rawdemod ab 0 64 1 0     = demod an ask/biph tag from GraphBuffer using a clock of RF/64, inverting data and allowing 0 demod errors");
		PrintAndLog("          : data rawdemod ab 0 64 1 0 0 a = demod an ask/biph tag from GraphBuffer using a clock of RF/64, inverting data and allowing 0 demod errors, and amp");
		return 0;
	}
	return ASKbiphaseDemod(Cmd, true);
}

//by marshmellow - see ASKDemod
int Cmdaskrawdemod(const char *Cmd)
{
	char cmdp = param_getchar(Cmd, 0);
	if (strlen(Cmd) > 35 || cmdp == 'h' || cmdp == 'H') {
		PrintAndLog("Usage:  data rawdemod ar [clock] <invert> [maxError] [maxLen] [amplify]");
		PrintAndLog("     [set clock as integer] optional, if not set, autodetect");
		PrintAndLog("     <invert>, 1 to invert output");
		PrintAndLog("     [set maximum allowed errors], default = 100");
		PrintAndLog("     [set maximum Samples to read], default = 32768 (1024 bits at rf/64)");
		PrintAndLog("     <amplify>, 'a' to attempt demod with ask amplification, default = no amp");
		PrintAndLog("");
		PrintAndLog("    sample: data rawdemod ar            = demod an ask tag from GraphBuffer");
		PrintAndLog("          : data rawdemod ar a          = demod an ask tag from GraphBuffer, amplified");
		PrintAndLog("          : data rawdemod ar 32         = demod an ask tag from GraphBuffer using a clock of RF/32");
		PrintAndLog("          : data rawdemod ar 32 1       = demod an ask tag from GraphBuffer using a clock of RF/32 and inverting data");
		PrintAndLog("          : data rawdemod ar 1          = demod an ask tag from GraphBuffer while inverting data");
		PrintAndLog("          : data rawdemod ar 64 1 0     = demod an ask tag from GraphBuffer using a clock of RF/64, inverting data and allowing 0 demod errors");
		PrintAndLog("          : data rawdemod ar 64 1 0 0 a = demod an ask tag from GraphBuffer using a clock of RF/64, inverting data and allowing 0 demod errors, and amp");
		return 0;
	}
	return ASKDemod(Cmd, true, false, 0);
}

int AutoCorrelate(const int *in, int *out, size_t len, int window, bool SaveGrph, bool verbose)
{
	static int CorrelBuffer[MAX_GRAPH_TRACE_LEN];
	size_t Correlation = 0;
	int maxSum = 0;
	int lastMax = 0;
	if (verbose) PrintAndLog("performing %d correlations", GraphTraceLen - window);
	for (int i = 0; i < len - window; ++i) {
		int sum = 0;
		for (int j = 0; j < window; ++j) {
			sum += (in[j]*in[i + j]) / 256;
		}
		CorrelBuffer[i] = sum;
		if (sum >= maxSum-100 && sum <= maxSum+100) {
			//another max
			Correlation = i-lastMax;
			lastMax = i;
			if (sum > maxSum) maxSum = sum;
		} else if (sum > maxSum) {
			maxSum=sum;
			lastMax = i;
		}
	}
	if (Correlation==0) {
		//try again with wider margin
		for (int i = 0; i < len - window; i++) {
			if (CorrelBuffer[i] >= maxSum-(maxSum*0.05) && CorrelBuffer[i] <= maxSum+(maxSum*0.05)) {
				//another max
				Correlation = i-lastMax;
				lastMax = i;
			}
		}
	}
	if (verbose && Correlation > 0) PrintAndLog("Possible Correlation: %d samples",Correlation);

	if (SaveGrph) {
		//GraphTraceLen = GraphTraceLen - window;
		memcpy(out, CorrelBuffer, len * sizeof(int));
		RepaintGraphWindow();  
	}
	return Correlation;
}

int usage_data_autocorr(void)
{
	//print help
	PrintAndLog("Usage: data autocorr [window] [g]");
	PrintAndLog("Options:        ");
	PrintAndLog("       h              This help");
	PrintAndLog("       [window]       window length for correlation - default = 4000");
	PrintAndLog("       g              save back to GraphBuffer (overwrite)");
	return 0;
}

int CmdAutoCorr(const char *Cmd)
{
	char cmdp = param_getchar(Cmd, 0);
	if (cmdp == 'h' || cmdp == 'H') 
		return usage_data_autocorr();
	int window = 4000; //set default
	char grph=0;
	bool updateGrph = false;
	sscanf(Cmd, "%i %c", &window, &grph);

	if (window >= GraphTraceLen) {
		PrintAndLog("window must be smaller than trace (%d samples)",
			GraphTraceLen);
		return 0;
	}
	if (grph == 'g') updateGrph=true;
	return AutoCorrelate(GraphBuffer, GraphBuffer, GraphTraceLen, window, updateGrph, true);
}

int CmdBitsamples(const char *Cmd)
{
	int cnt = 0;
	uint8_t got[12288];

	GetFromBigBuf(got,sizeof(got),0);
	WaitForResponse(CMD_ACK,NULL);

		for (int j = 0; j < sizeof(got); j++) {
			for (int k = 0; k < 8; k++) {
				if(got[j] & (1 << (7 - k))) {
					GraphBuffer[cnt++] = 1;
				} else {
					GraphBuffer[cnt++] = 0;
				}
			}
	}
	GraphTraceLen = cnt;
	RepaintGraphWindow();
	return 0;
}

int CmdBuffClear(const char *Cmd)
{
	UsbCommand c = {CMD_BUFF_CLEAR};
	SendCommand(&c);
	ClearGraph(true);
	return 0;
}

int CmdDec(const char *Cmd)
{
	for (int i = 0; i < (GraphTraceLen / 2); ++i)
		GraphBuffer[i] = GraphBuffer[i * 2];
	GraphTraceLen /= 2;
	PrintAndLog("decimated by 2");
	RepaintGraphWindow();
	return 0;
}
/**
 * Undecimate - I'd call it 'interpolate', but we'll save that
 * name until someone does an actual interpolation command, not just
 * blindly repeating samples
 * @param Cmd
 * @return
 */
int CmdUndec(const char *Cmd)
{
	if(param_getchar(Cmd, 0) == 'h')
	{
		PrintAndLog("Usage: data undec [factor]");
		PrintAndLog("This function performs un-decimation, by repeating each sample N times");
		PrintAndLog("Options:        ");
		PrintAndLog("       h            This help");
		PrintAndLog("       factor       The number of times to repeat each sample.[default:2]");
		PrintAndLog("Example: 'data undec 3'");
		return 0;
	}

	uint8_t factor = param_get8ex(Cmd, 0,2, 10);
	//We have memory, don't we?
	int swap[MAX_GRAPH_TRACE_LEN] = { 0 };
	uint32_t g_index = 0, s_index = 0;
	while(g_index < GraphTraceLen && s_index + factor < MAX_GRAPH_TRACE_LEN)
	{
		int count = 0;
		for(count = 0; count < factor && s_index + count < MAX_GRAPH_TRACE_LEN; count++)
			swap[s_index+count] = GraphBuffer[g_index];

		s_index += count;
		g_index++;
	}

	memcpy(GraphBuffer, swap, s_index * sizeof(int));
	GraphTraceLen = s_index;
	RepaintGraphWindow();
	return 0;
}

//by marshmellow
//shift graph zero up or down based on input + or -
int CmdGraphShiftZero(const char *Cmd)
{

	int shift=0;
	//set options from parameters entered with the command
	sscanf(Cmd, "%i", &shift);
	int shiftedVal=0;
	for(int i = 0; i<GraphTraceLen; i++){
		shiftedVal=GraphBuffer[i]+shift;
		if (shiftedVal>127) 
			shiftedVal=127;
		else if (shiftedVal<-127) 
			shiftedVal=-127;
		GraphBuffer[i]= shiftedVal;
	}
	CmdNorm("");
	return 0;
}

int AskEdgeDetect(const int *in, int *out, int len, int threshold) {
	int Last = 0;
	for(int i = 1; i<len; i++) {
		if (in[i]-in[i-1] >= threshold) //large jump up
			Last = 127;
		else if(in[i]-in[i-1] <= -1 * threshold) //large jump down
			Last = -127;
		out[i-1] = Last;
	}
	return 0;
}

//by marshmellow
//use large jumps in read samples to identify edges of waves and then amplify that wave to max
//similar to dirtheshold, threshold commands 
//takes a threshold length which is the measured length between two samples then determines an edge
int CmdAskEdgeDetect(const char *Cmd)
{
	int thresLen = 25;
	int ans = 0;
	sscanf(Cmd, "%i", &thresLen); 

	ans = AskEdgeDetect(GraphBuffer, GraphBuffer, GraphTraceLen, thresLen);
	RepaintGraphWindow();
	return ans;
}

/* Print our clock rate */
// uses data from graphbuffer
// adjusted to take char parameter for type of modulation to find the clock - by marshmellow.
int CmdDetectClockRate(const char *Cmd)
{
	char cmdp = param_getchar(Cmd, 0);
	if (strlen(Cmd) > 6 || strlen(Cmd) == 0 || cmdp == 'h' || cmdp == 'H') {
		PrintAndLog("Usage:  data detectclock [modulation] <clock>");
		PrintAndLog("     [modulation as char], specify the modulation type you want to detect the clock of");
		PrintAndLog("     <clock>             , specify the clock (optional - to get best start position only)");
		PrintAndLog("       'a' = ask, 'f' = fsk, 'n' = nrz/direct, 'p' = psk");
		PrintAndLog("");
		PrintAndLog("    sample: data detectclock a    = detect the clock of an ask modulated wave in the GraphBuffer");
		PrintAndLog("            data detectclock f    = detect the clock of an fsk modulated wave in the GraphBuffer");
		PrintAndLog("            data detectclock p    = detect the clock of an psk modulated wave in the GraphBuffer");
		PrintAndLog("            data detectclock n    = detect the clock of an nrz/direct modulated wave in the GraphBuffer");
	}
	int ans=0;
	if (cmdp == 'a'){
		ans = GetAskClock(Cmd+1, true, false);
	} else if (cmdp == 'f'){
		ans = GetFskClock("", true, false);
	} else if (cmdp == 'n'){
		ans = GetNrzClock("", true, false);
	} else if (cmdp == 'p'){
		ans = GetPskClock("", true, false);
	} else {
		PrintAndLog ("Please specify a valid modulation to detect the clock of - see option h for help");
	}
	return ans;
}

char *GetFSKType(uint8_t fchigh, uint8_t fclow, uint8_t invert)
{
	static char fType[8];
	memset(fType, 0x00, 8);
	char *fskType = fType;
	if (fchigh==10 && fclow==8){
		if (invert) //fsk2a
			memcpy(fskType, "FSK2a", 5);
		else //fsk2
			memcpy(fskType, "FSK2", 4);
	} else if (fchigh == 8 && fclow == 5) {
		if (invert)
			memcpy(fskType, "FSK1", 4);
		else
			memcpy(fskType, "FSK1a", 5);
	} else {
		memcpy(fskType, "FSK??", 5);
	}
	return fskType;
}

//by marshmellow
//fsk raw demod and print binary
//takes 4 arguments - Clock, invert, fchigh, fclow
//defaults: clock = 50, invert=1, fchigh=10, fclow=8 (RF/10 RF/8 (fsk2a))
int FSKrawDemod(const char *Cmd, bool verbose)
{
	//raw fsk demod  no manchester decoding no start bit finding just get binary from wave
	uint8_t rfLen, invert, fchigh, fclow;
	//set defaults
	//set options from parameters entered with the command
	rfLen = param_get8(Cmd, 0);
	invert = param_get8(Cmd, 1);
	fchigh = param_get8(Cmd, 2);
	fclow = param_get8(Cmd, 3);

	if (strlen(Cmd)>0 && strlen(Cmd)<=2) {
		if (rfLen==1) {
			invert = 1;   //if invert option only is used
			rfLen = 0;
		}
	}
	uint8_t BitStream[MAX_GRAPH_TRACE_LEN]={0};
	size_t BitLen = getFromGraphBuf(BitStream);
	if (BitLen==0) return 0;
	//get field clock lengths
	uint16_t fcs=0;
	if (!fchigh || !fclow) {
		fcs = countFC(BitStream, BitLen, 1);
		if (!fcs) {
			fchigh = 10;
			fclow = 8;
		} else {
			fchigh = (fcs >> 8) & 0x00FF;
			fclow = fcs & 0x00FF;
		}
	}
	//get bit clock length
	if (!rfLen) {
		int firstClockEdge = 0; //todo - align grid on graph with this...
		rfLen = detectFSKClk(BitStream, BitLen, fchigh, fclow, &firstClockEdge);
		if (!rfLen) rfLen = 50;
	}
	int startIdx = 0;
	int size = fskdemod(BitStream, BitLen, rfLen, invert, fchigh, fclow, &startIdx);
	if (size > 0) {
		setDemodBuf(BitStream,size,0);
		setClockGrid(rfLen, startIdx);

    // Now output the bitstream to the scrollback by line of 16 bits
		if (verbose || g_debugMode) {
			PrintAndLog("\nUsing Clock:%u, invert:%u, fchigh:%u, fclow:%u", (unsigned int)rfLen, (unsigned int)invert, (unsigned int)fchigh, (unsigned int)fclow);
			PrintAndLog("%s decoded bitstream:",GetFSKType(fchigh,fclow,invert));
			printDemodBuff();
		}

		return 1;
	} else {
		if (g_debugMode) PrintAndLog("no FSK data found");
	}
	return 0;
}

//by marshmellow
//fsk raw demod and print binary
//takes 4 arguments - Clock, invert, fchigh, fclow
//defaults: clock = 50, invert=1, fchigh=10, fclow=8 (RF/10 RF/8 (fsk2a))
int CmdFSKrawdemod(const char *Cmd)
{
	char cmdp = param_getchar(Cmd, 0);
	if (strlen(Cmd) > 20 || cmdp == 'h' || cmdp == 'H') {
		PrintAndLog("Usage:  data rawdemod fs [clock] <invert> [fchigh] [fclow]");
		PrintAndLog("     [set clock as integer] optional, omit for autodetect.");
		PrintAndLog("     <invert>, 1 for invert output, can be used even if the clock is omitted");
		PrintAndLog("     [fchigh], larger field clock length, omit for autodetect");
		PrintAndLog("     [fclow], small field clock length, omit for autodetect");
		PrintAndLog("");
		PrintAndLog("    sample: data rawdemod fs           = demod an fsk tag from GraphBuffer using autodetect");
		PrintAndLog("          : data rawdemod fs 32        = demod an fsk tag from GraphBuffer using a clock of RF/32, autodetect fc");
		PrintAndLog("          : data rawdemod fs 1         = demod an fsk tag from GraphBuffer using autodetect, invert output");   
		PrintAndLog("          : data rawdemod fs 32 1      = demod an fsk tag from GraphBuffer using a clock of RF/32, invert output, autodetect fc");
		PrintAndLog("          : data rawdemod fs 64 0 8 5  = demod an fsk1 RF/64 tag from GraphBuffer");
		PrintAndLog("          : data rawdemod fs 50 0 10 8 = demod an fsk2 RF/50 tag from GraphBuffer");
		PrintAndLog("          : data rawdemod fs 50 1 10 8 = demod an fsk2a RF/50 tag from GraphBuffer");
		return 0;
	}
	return FSKrawDemod(Cmd, true);
}

//by marshmellow
//attempt to psk1 demod graph buffer
int PSKDemod(const char *Cmd, bool verbose)
{
	int invert=0;
	int clk=0;
	int maxErr=100;
	sscanf(Cmd, "%i %i %i", &clk, &invert, &maxErr);
	if (clk==1){
		invert=1;
		clk=0;
	}
	if (invert != 0 && invert != 1) {
		if (g_debugMode || verbose) PrintAndLog("Invalid argument: %s", Cmd);
		return 0;
	}
	uint8_t BitStream[MAX_GRAPH_TRACE_LEN]={0};
	size_t BitLen = getFromGraphBuf(BitStream);
	if (BitLen==0) return 0;
	int errCnt=0;
	int startIdx = 0;
	errCnt = pskRawDemod_ext(BitStream, &BitLen, &clk, &invert, &startIdx);
	if (errCnt > maxErr){
		if (g_debugMode || verbose) PrintAndLog("Too many errors found, clk: %d, invert: %d, numbits: %d, errCnt: %d",clk,invert,BitLen,errCnt);
		return 0;
	} 
	if (errCnt<0|| BitLen<16){  //throw away static - allow 1 and -1 (in case of threshold command first)
		if (g_debugMode || verbose) PrintAndLog("no data found, clk: %d, invert: %d, numbits: %d, errCnt: %d",clk,invert,BitLen,errCnt);
		return 0;
	}
	if (verbose || g_debugMode){
		PrintAndLog("\nUsing Clock:%d, invert:%d, Bits Found:%d",clk,invert,BitLen);
		if (errCnt>0){
			PrintAndLog("# Errors during Demoding (shown as 7 in bit stream): %d",errCnt);
		}
	}
	//prime demod buffer for output
	setDemodBuf(BitStream,BitLen,0);
	setClockGrid(clk, startIdx);

	return 1;
}

// by marshmellow
// takes 3 arguments - clock, invert, maxErr as integers
// attempts to demodulate nrz only
// prints binary found and saves in demodbuffer for further commands
int NRZrawDemod(const char *Cmd, bool verbose)
{
	int invert=0;
	int clk=0;
	int maxErr=100;
	sscanf(Cmd, "%i %i %i", &clk, &invert, &maxErr);
	if (clk==1){
		invert=1;
		clk=0;
	}
	if (invert != 0 && invert != 1) {
		PrintAndLog("Invalid argument: %s", Cmd);
		return 0;
	}
	uint8_t BitStream[MAX_GRAPH_TRACE_LEN]={0};
	size_t BitLen = getFromGraphBuf(BitStream);
	if (BitLen==0) return 0;
	int errCnt=0;
	int clkStartIdx = 0;
	errCnt = nrzRawDemod(BitStream, &BitLen, &clk, &invert, &clkStartIdx);
	if (errCnt > maxErr){
		if (g_debugMode) PrintAndLog("Too many errors found, clk: %d, invert: %d, numbits: %d, errCnt: %d",clk,invert,BitLen,errCnt);
		return 0;
	} 
	if (errCnt<0 || BitLen<16){  //throw away static - allow 1 and -1 (in case of threshold command first)
		if (g_debugMode) PrintAndLog("no data found, clk: %d, invert: %d, numbits: %d, errCnt: %d",clk,invert,BitLen,errCnt);
		return 0;
	}
	if (verbose || g_debugMode) PrintAndLog("Tried NRZ Demod using Clock: %d - invert: %d - Bits Found: %d",clk,invert,BitLen);
	//prime demod buffer for output
	setDemodBuf(BitStream,BitLen,0);
	setClockGrid(clk, clkStartIdx);


	if (errCnt>0 && (verbose || g_debugMode)) PrintAndLog("# Errors during Demoding (shown as 7 in bit stream): %d",errCnt);
	if (verbose || g_debugMode) {
		PrintAndLog("NRZ demoded bitstream:");
		// Now output the bitstream to the scrollback by line of 16 bits
		printDemodBuff();
	}
	return 1; 
}

int CmdNRZrawDemod(const char *Cmd)
{
	char cmdp = param_getchar(Cmd, 0);
	if (strlen(Cmd) > 16 || cmdp == 'h' || cmdp == 'H') {
		PrintAndLog("Usage:  data rawdemod nr [clock] <0|1> [maxError]");
		PrintAndLog("     [set clock as integer] optional, if not set, autodetect.");
		PrintAndLog("     <invert>, 1 for invert output");
		PrintAndLog("     [set maximum allowed errors], default = 100.");
		PrintAndLog("");
		PrintAndLog("    sample: data rawdemod nr        = demod a nrz/direct tag from GraphBuffer");
		PrintAndLog("          : data rawdemod nr 32     = demod a nrz/direct tag from GraphBuffer using a clock of RF/32");
		PrintAndLog("          : data rawdemod nr 32 1   = demod a nrz/direct tag from GraphBuffer using a clock of RF/32 and inverting data");
		PrintAndLog("          : data rawdemod nr 1      = demod a nrz/direct tag from GraphBuffer while inverting data");
		PrintAndLog("          : data rawdemod nr 64 1 0 = demod a nrz/direct tag from GraphBuffer using a clock of RF/64, inverting data and allowing 0 demod errors");
		return 0;
	}
	return NRZrawDemod(Cmd, true);
}

// by marshmellow
// takes 3 arguments - clock, invert, maxErr as integers
// attempts to demodulate psk only
// prints binary found and saves in demodbuffer for further commands
int CmdPSK1rawDemod(const char *Cmd)
{
	int ans;
	char cmdp = param_getchar(Cmd, 0);
	if (strlen(Cmd) > 16 || cmdp == 'h' || cmdp == 'H') {
		PrintAndLog("Usage:  data rawdemod p1 [clock] <0|1> [maxError]");
		PrintAndLog("     [set clock as integer] optional, if not set, autodetect.");
		PrintAndLog("     <invert>, 1 for invert output");
		PrintAndLog("     [set maximum allowed errors], default = 100.");
		PrintAndLog("");
		PrintAndLog("    sample: data rawdemod p1        = demod a psk1 tag from GraphBuffer");
		PrintAndLog("          : data rawdemod p1 32     = demod a psk1 tag from GraphBuffer using a clock of RF/32");
		PrintAndLog("          : data rawdemod p1 32 1   = demod a psk1 tag from GraphBuffer using a clock of RF/32 and inverting data");
		PrintAndLog("          : data rawdemod p1 1      = demod a psk1 tag from GraphBuffer while inverting data");
		PrintAndLog("          : data rawdemod p1 64 1 0 = demod a psk1 tag from GraphBuffer using a clock of RF/64, inverting data and allowing 0 demod errors");
		return 0;
	}
	ans = PSKDemod(Cmd, true);
	//output
	if (!ans){
		if (g_debugMode) PrintAndLog("Error demoding: %d",ans); 
		return 0;
	}
 
	PrintAndLog("PSK1 demoded bitstream:");
	// Now output the bitstream to the scrollback by line of 16 bits
	printDemodBuff();
	return 1;
}

// by marshmellow
// takes same args as cmdpsk1rawdemod
int CmdPSK2rawDemod(const char *Cmd)
{
	int ans=0;
	char cmdp = param_getchar(Cmd, 0);
	if (strlen(Cmd) > 16 || cmdp == 'h' || cmdp == 'H') {
		PrintAndLog("Usage:  data rawdemod p2 [clock] <0|1> [maxError]");
		PrintAndLog("     [set clock as integer] optional, if not set, autodetect.");
		PrintAndLog("     <invert>, 1 for invert output");
		PrintAndLog("     [set maximum allowed errors], default = 100.");
		PrintAndLog("");
		PrintAndLog("    sample: data rawdemod p2         = demod a psk2 tag from GraphBuffer, autodetect clock");
		PrintAndLog("          : data rawdemod p2 32      = demod a psk2 tag from GraphBuffer using a clock of RF/32");
		PrintAndLog("          : data rawdemod p2 32 1    = demod a psk2 tag from GraphBuffer using a clock of RF/32 and inverting output");
		PrintAndLog("          : data rawdemod p2 1       = demod a psk2 tag from GraphBuffer, autodetect clock and invert output");
		PrintAndLog("          : data rawdemod p2 64 1 0  = demod a psk2 tag from GraphBuffer using a clock of RF/64, inverting output and allowing 0 demod errors");
		return 0;
	}
	ans=PSKDemod(Cmd, true);
	if (!ans){
		if (g_debugMode) PrintAndLog("Error demoding: %d",ans);  
		return 0;
	} 
	psk1TOpsk2(DemodBuffer, DemodBufferLen);
	PrintAndLog("PSK2 demoded bitstream:");
	// Now output the bitstream to the scrollback by line of 16 bits
	printDemodBuff();  
	return 1;
}

// by marshmellow - combines all raw demod functions into one menu command
int CmdRawDemod(const char *Cmd)
{
	char cmdp = Cmd[0]; //param_getchar(Cmd, 0);

	if (strlen(Cmd) > 35 || cmdp == 'h' || cmdp == 'H' || strlen(Cmd)<2) {
		PrintAndLog("Usage:  data rawdemod [modulation] <help>|<options>");
		PrintAndLog("   [modulation] as 2 char, 'ab' for ask/biphase, 'am' for ask/manchester, 'ar' for ask/raw, 'fs' for fsk, ...");		
		PrintAndLog("         'nr' for nrz/direct, 'p1' for psk1, 'p2' for psk2");
		PrintAndLog("   <help> as 'h', prints the help for the specific modulation");	
		PrintAndLog("   <options> see specific modulation help for optional parameters");				
		PrintAndLog("");
		PrintAndLog("    sample: data rawdemod fs h         = print help specific to fsk demod");
		PrintAndLog("          : data rawdemod fs           = demod GraphBuffer using: fsk - autodetect");
		PrintAndLog("          : data rawdemod ab           = demod GraphBuffer using: ask/biphase - autodetect");
		PrintAndLog("          : data rawdemod am           = demod GraphBuffer using: ask/manchester - autodetect");
		PrintAndLog("          : data rawdemod ar           = demod GraphBuffer using: ask/raw - autodetect");
		PrintAndLog("          : data rawdemod nr           = demod GraphBuffer using: nrz/direct - autodetect");
		PrintAndLog("          : data rawdemod p1           = demod GraphBuffer using: psk1 - autodetect");
		PrintAndLog("          : data rawdemod p2           = demod GraphBuffer using: psk2 - autodetect");
		return 0;
	}
	char cmdp2 = Cmd[1];
	int ans = 0;
	if (cmdp == 'f' && cmdp2 == 's'){
		ans = CmdFSKrawdemod(Cmd+2);
	} else if(cmdp == 'a' && cmdp2 == 'b'){
		ans = Cmdaskbiphdemod(Cmd+2);
	} else if(cmdp == 'a' && cmdp2 == 'm'){
		ans = Cmdaskmandemod(Cmd+2);
	} else if(cmdp == 'a' && cmdp2 == 'r'){
		ans = Cmdaskrawdemod(Cmd+2);
	} else if(cmdp == 'n' && cmdp2 == 'r'){
		ans = CmdNRZrawDemod(Cmd+2);
	} else if(cmdp == 'p' && cmdp2 == '1'){
		ans = CmdPSK1rawDemod(Cmd+2);
	} else if(cmdp == 'p' && cmdp2 == '2'){
		ans = CmdPSK2rawDemod(Cmd+2);
	} else { 
		PrintAndLog("unknown modulation entered - see help ('h') for parameter structure");
	}
	return ans;
}

void setClockGrid(int clk, int offset) {
	g_DemodStartIdx = offset;
	g_DemodClock = clk;
	if (g_debugMode) PrintAndLog("demodoffset %d, clk %d",offset,clk);

	if (offset > clk) offset %= clk;
	if (offset < 0) offset += clk;

	if (offset > GraphTraceLen || offset < 0) return;
	if (clk < 8 || clk > GraphTraceLen) {
		GridLocked = false;
		GridOffset = 0;
		PlotGridX = 0;
		PlotGridXdefault = 0;
		RepaintGraphWindow();
	} else {
		GridLocked = true;
		GridOffset = offset;
		PlotGridX = clk;
		PlotGridXdefault = clk;
		RepaintGraphWindow();
	}
}

int CmdGrid(const char *Cmd)
{
	sscanf(Cmd, "%i %i", &PlotGridX, &PlotGridY);
	PlotGridXdefault= PlotGridX;
	PlotGridYdefault= PlotGridY;
	RepaintGraphWindow();
	return 0;
}

int CmdSetGraphMarkers(const char *Cmd) {
	sscanf(Cmd, "%i %i", &CursorCPos, &CursorDPos);
	RepaintGraphWindow();
	return 0;
}

int CmdHexsamples(const char *Cmd)
{
	int i, j;
	int requested = 0;
	int offset = 0;
	char string_buf[25];
	char* string_ptr = string_buf;
	uint8_t got[BIGBUF_SIZE];

	sscanf(Cmd, "%i %i", &requested, &offset);

	/* if no args send something */
	if (requested == 0) {
		requested = 8;
	}
	if (offset + requested > sizeof(got)) {
		PrintAndLog("Tried to read past end of buffer, <bytes> + <offset> > %d", BIGBUF_SIZE);
		return 0;
	}

	GetFromBigBuf(got,requested,offset);
	WaitForResponse(CMD_ACK,NULL);

	i = 0;
	for (j = 0; j < requested; j++) {
		i++;
		string_ptr += sprintf(string_ptr, "%02x ", got[j]);
		if (i == 8) {
			*(string_ptr - 1) = '\0';    // remove the trailing space
			PrintAndLog("%s", string_buf);
			string_buf[0] = '\0';
			string_ptr = string_buf;
			i = 0;
		}
		if (j == requested - 1 && string_buf[0] != '\0') { // print any remaining bytes
			*(string_ptr - 1) = '\0';
			PrintAndLog("%s", string_buf);
			string_buf[0] = '\0';
		}
	}
	return 0;
}

int CmdHide(const char *Cmd)
{
	HideGraphWindow();
	return 0;
}

//zero mean GraphBuffer
int CmdHpf(const char *Cmd)
{
	int i;
	int accum = 0;

	for (i = 10; i < GraphTraceLen; ++i)
		accum += GraphBuffer[i];
	accum /= (GraphTraceLen - 10);
	for (i = 0; i < GraphTraceLen; ++i)
		GraphBuffer[i] -= accum;

	RepaintGraphWindow();
	return 0;
}

uint8_t getByte(uint8_t bits_per_sample, BitstreamIn* b)
{
	int i;
	uint8_t val = 0;
	for(i =0 ; i < bits_per_sample; i++)
	{
		val |= (headBit(b) << (7-i));
	}
	return val;
}

int getSamples(int n, bool silent)
{
	//If we get all but the last byte in bigbuf,
	// we don't have to worry about remaining trash
	// in the last byte in case the bits-per-sample
	// does not line up on byte boundaries

	uint8_t got[BIGBUF_SIZE-1] = { 0 };

	if (n == 0 || n > sizeof(got))
		n = sizeof(got);

	if (!silent) PrintAndLog("Reading %d bytes from device memory\n", n);
	GetFromBigBuf(got,n,0);
	if (!silent) PrintAndLog("Data fetched");
	UsbCommand response;
	WaitForResponse(CMD_ACK, &response);
	uint8_t bits_per_sample = 8;

	//Old devices without this feature would send 0 at arg[0]
	if(response.arg[0] > 0)
	{
		sample_config *sc = (sample_config *) response.d.asBytes;
		if (!silent) PrintAndLog("Samples @ %d bits/smpl, decimation 1:%d ", sc->bits_per_sample
		    , sc->decimation);
		bits_per_sample = sc->bits_per_sample;
	}
	if(bits_per_sample < 8)
	{
		if (!silent) PrintAndLog("Unpacking...");
		BitstreamIn bout = { got, bits_per_sample * n,  0};
		int j =0;
		for (j = 0; j * bits_per_sample < n * 8 && j < n; j++) {
			uint8_t sample = getByte(bits_per_sample, &bout);
			GraphBuffer[j] = ((int) sample )- 128;
		}
		GraphTraceLen = j;
		PrintAndLog("Unpacked %d samples" , j );
	}else
	{
		for (int j = 0; j < n; j++) {
			GraphBuffer[j] = ((int)got[j]) - 128;
		}
		GraphTraceLen = n;
	}

	setClockGrid(0,0);
	DemodBufferLen = 0;
	RepaintGraphWindow();
	return 0;
}

int CmdSamples(const char *Cmd)
{
	int n = strtol(Cmd, NULL, 0);
	return getSamples(n, false);
}

int CmdTuneSamples(const char *Cmd)
{
	int timeout = 0, arg = FLAG_TUNE_ALL;

	if(*Cmd == 'l') {
	  arg = FLAG_TUNE_LF;
	} else if (*Cmd == 'h') {
	  arg = FLAG_TUNE_HF;
	} else if (*Cmd != '\0') {
	  PrintAndLog("use 'tune' or 'tune l' or 'tune h'");
	  return 0;
	}

	printf("\nMeasuring antenna characteristics, please wait...");

	UsbCommand c = {CMD_MEASURE_ANTENNA_TUNING, {arg, 0, 0}};
	SendCommand(&c);

	UsbCommand resp;
	while(!WaitForResponseTimeout(CMD_MEASURED_ANTENNA_TUNING,&resp,1000)) {
		timeout++;
		printf(".");
		if (timeout > 7) {
			PrintAndLog("\nNo response from Proxmark. Aborting...");
			return 1;
		}
	}

	int peakv, peakf;
	int vLf125, vLf134, vHf;
	vLf125 = resp.arg[0] & 0xffff;
	vLf134 = resp.arg[0] >> 16;
	vHf = resp.arg[1] & 0xffff;;
	peakf = resp.arg[2] & 0xffff;
	peakv = resp.arg[2] >> 16;
	PrintAndLog("");
	if (arg & FLAG_TUNE_LF)
	{
		PrintAndLog("# LF antenna: %5.2f V @   125.00 kHz", vLf125/500.0);
		PrintAndLog("# LF antenna: %5.2f V @   134.00 kHz", vLf134/500.0);
		PrintAndLog("# LF optimal: %5.2f V @%9.2f kHz", peakv/500.0, 12000.0/(peakf+1));
	}
	if (arg & FLAG_TUNE_HF)
		PrintAndLog("# HF antenna: %5.2f V @    13.56 MHz", vHf/1000.0);

 #define LF_UNUSABLE_V		3000
 #define LF_MARGINAL_V		15000
 #define HF_UNUSABLE_V		3200
 #define HF_MARGINAL_V		8000

	if (arg & FLAG_TUNE_LF)
	{
		if (peakv<<1 < LF_UNUSABLE_V)
			PrintAndLog("# Your LF antenna is unusable.");
		else if (peakv<<1 < LF_MARGINAL_V)
			PrintAndLog("# Your LF antenna is marginal.");
	}
	if (arg & FLAG_TUNE_HF)
	{
		if (vHf < HF_UNUSABLE_V)
			PrintAndLog("# Your HF antenna is unusable.");
		else if (vHf < HF_MARGINAL_V)
			PrintAndLog("# Your HF antenna is marginal.");
	}

	if (peakv<<1 >= LF_UNUSABLE_V)	{
		for (int i = 0; i < 256; i++) {
			GraphBuffer[i] = resp.d.asBytes[i] - 128;
		}
		PrintAndLog("Displaying LF tuning graph. Divisor 89 is 134khz, 95 is 125khz.\n");
		PrintAndLog("\n");
		GraphTraceLen = 256;
		ShowGraphWindow();
		RepaintGraphWindow();
	}

	return 0;
}


int CmdLoad(const char *Cmd)
{
	char filename[FILE_PATH_SIZE] = {0x00};
	int len = 0;

	len = strlen(Cmd);
	if (len > FILE_PATH_SIZE) len = FILE_PATH_SIZE;
	memcpy(filename, Cmd, len);
	
	FILE *f = fopen(filename, "r");
	if (!f) {
		 PrintAndLog("couldn't open '%s'", filename);
		return 0;
	}

	GraphTraceLen = 0;
	char line[80];
	while (fgets(line, sizeof (line), f)) {
		GraphBuffer[GraphTraceLen] = atoi(line);
		GraphTraceLen++;
	}
	fclose(f);
	PrintAndLog("loaded %d samples", GraphTraceLen);
	setClockGrid(0,0);
	DemodBufferLen = 0;
	RepaintGraphWindow();
	return 0;
}

int CmdLtrim(const char *Cmd)
{
	int ds = atoi(Cmd);
	if (GraphTraceLen<=0) return 0;
	for (int i = ds; i < GraphTraceLen; ++i)
		GraphBuffer[i-ds] = GraphBuffer[i];
	GraphTraceLen -= ds;

	RepaintGraphWindow();
	return 0;
}

// trim graph to input argument length
int CmdRtrim(const char *Cmd)
{
	int ds = atoi(Cmd);

	GraphTraceLen = ds;

	RepaintGraphWindow();
	return 0;
}

// trim graph (middle) piece
int CmdMtrim(const char *Cmd) {
	int start = 0, stop = 0;
	sscanf(Cmd, "%i %i", &start, &stop);

	if (start > GraphTraceLen	|| stop > GraphTraceLen || start > stop) return 0;
	start++; //leave start position sample

	GraphTraceLen = stop - start;
	for (int i = 0; i < GraphTraceLen; i++) {
		GraphBuffer[i] = GraphBuffer[start+i];
	}
	return 0;
}


int CmdNorm(const char *Cmd)
{
	int i;
	int max = INT_MIN, min = INT_MAX;

	for (i = 10; i < GraphTraceLen; ++i) {
		if (GraphBuffer[i] > max)
			max = GraphBuffer[i];
		if (GraphBuffer[i] < min)
			min = GraphBuffer[i];
	}

	if (max != min) {
		for (i = 0; i < GraphTraceLen; ++i) {
			GraphBuffer[i] = ((long)(GraphBuffer[i] - ((max + min) / 2)) * 256) / (max - min);
				//marshmelow: adjusted *1000 to *256 to make +/- 128 so demod commands still work
		}
	}
	RepaintGraphWindow();
	return 0;
}

int CmdPlot(const char *Cmd)
{
	ShowGraphWindow();
	return 0;
}

int CmdSave(const char *Cmd)
{
	char filename[FILE_PATH_SIZE] = {0x00};
	int len = 0;

	len = strlen(Cmd);
	if (len > FILE_PATH_SIZE) len = FILE_PATH_SIZE;
	memcpy(filename, Cmd, len);
	 

	FILE *f = fopen(filename, "w");
	if(!f) {
		PrintAndLog("couldn't open '%s'", filename);
		return 0;
	}
	int i;
	for (i = 0; i < GraphTraceLen; i++) {
		fprintf(f, "%d\n", GraphBuffer[i]);
	}
	fclose(f);
	PrintAndLog("saved to '%s'", Cmd);
	return 0;
}

int CmdScale(const char *Cmd)
{
	CursorScaleFactor = atoi(Cmd);
	if (CursorScaleFactor == 0) {
		PrintAndLog("bad, can't have zero scale");
		CursorScaleFactor = 1;
	}
	RepaintGraphWindow();
	return 0;
}

int directionalThreshold(const int* in, int *out, size_t len, int8_t up, int8_t down)
{
	int lastValue = in[0];
	out[0] = 0; // Will be changed at the end, but init 0 as we adjust to last samples value if no threshold kicks in.

	for (int i = 1; i < len; ++i) {
		// Apply first threshold to samples heading up
		if (in[i] >= up && in[i] > lastValue)
		{
			lastValue = out[i]; // Buffer last value as we overwrite it.
			out[i] = 1;
		}
		// Apply second threshold to samples heading down
		else if (in[i] <= down && in[i] < lastValue)
		{
			lastValue = out[i]; // Buffer last value as we overwrite it.
			out[i] = -1;
		}
		else
		{
			lastValue = out[i]; // Buffer last value as we overwrite it.
			out[i] = out[i-1];
		}
	}
	out[0] = out[1]; // Align with first edited sample.
	return 0;
}

int CmdDirectionalThreshold(const char *Cmd)
{
	int8_t upThres = param_get8(Cmd, 0);
	int8_t downThres = param_get8(Cmd, 1);

	printf("Applying Up Threshold: %d, Down Threshold: %d\n", upThres, downThres);

	directionalThreshold(GraphBuffer, GraphBuffer,GraphTraceLen, upThres, downThres);
	RepaintGraphWindow();
	return 0;
}

int CmdZerocrossings(const char *Cmd)
{
	// Zero-crossings aren't meaningful unless the signal is zero-mean.
	CmdHpf("");

	int sign = 1;
	int zc = 0;
	int lastZc = 0;

	for (int i = 0; i < GraphTraceLen; ++i) {
		if (GraphBuffer[i] * sign >= 0) {
			// No change in sign, reproduce the previous sample count.
			zc++;
			GraphBuffer[i] = lastZc;
		} else {
			// Change in sign, reset the sample count.
			sign = -sign;
			GraphBuffer[i] = lastZc;
			if (sign > 0) {
				lastZc = zc;
				zc = 0;
			}
		}
	}

	RepaintGraphWindow();
	return 0;
}

int usage_data_bin2hex(){
		PrintAndLog("Usage: data bin2hex <binary_digits>");
		PrintAndLog("       This function will ignore all characters not 1 or 0 (but stop reading on whitespace)");
		return 0;
}

/**
 * @brief Utility for conversion via cmdline.
 * @param Cmd
 * @return
 */
int Cmdbin2hex(const char *Cmd)
{
	int bg =0, en =0;
	if(param_getptr(Cmd, &bg, &en, 0))
	{
		return usage_data_bin2hex();
	}
	//Number of digits supplied as argument
	size_t length = en  - bg +1;
	size_t bytelen = (length+7) / 8;
	uint8_t* arr = (uint8_t *) malloc(bytelen);
	memset(arr, 0, bytelen);
	BitstreamOut bout = { arr, 0, 0 };

	for(; bg <= en ;bg++)
	{
		char c = Cmd[bg];
		if( c == '1')	pushBit(&bout, 1);
		else if( c == '0')	pushBit(&bout, 0);
		else PrintAndLog("Ignoring '%c'", c);
	}

	if(bout.numbits % 8 != 0)
	{
		printf("[padded with %d zeroes]\n", 8-(bout.numbits % 8));
	}

	//Uses printf instead of PrintAndLog since the latter
	// adds linebreaks to each printout - this way was more convenient since we don't have to
	// allocate a string and write to that first...
	for(size_t x = 0; x  < bytelen ; x++)
	{
		printf("%02X", arr[x]);
	}
	printf("\n");
	free(arr);
	return 0;
}

int usage_data_hex2bin() {
	PrintAndLog("Usage: data hex2bin <hex_digits>");
	PrintAndLog("       This function will ignore all non-hexadecimal characters (but stop reading on whitespace)");
	return 0;

}

int Cmdhex2bin(const char *Cmd)
{
	int bg =0, en =0;
	if(param_getptr(Cmd, &bg, &en, 0))
	{
		return usage_data_hex2bin();
	}


	while(bg <= en )
	{
		char x = Cmd[bg++];
		// capitalize
		if (x >= 'a' && x <= 'f')
			x -= 32;
		// convert to numeric value
		if (x >= '0' && x <= '9')
			x -= '0';
		else if (x >= 'A' && x <= 'F')
			x -= 'A' - 10;
		else
			continue;

		//Uses printf instead of PrintAndLog since the latter
		// adds linebreaks to each printout - this way was more convenient since we don't have to
		// allocate a string and write to that first...

		for(int i= 0 ; i < 4 ; ++i)
			printf("%d",(x >> (3 - i)) & 1);
	}
	printf("\n");

	return 0;
}

	/* // example of FSK2 RF/50 Tones
	static const int LowTone[]  = {
	1,  1,  1,  1,  1, -1, -1, -1, -1, -1,
	1,  1,  1,  1,  1, -1, -1, -1, -1, -1,
	1,  1,  1,  1,  1, -1, -1, -1, -1, -1,
	1,  1,  1,  1,  1, -1, -1, -1, -1, -1,
	1,  1,  1,  1,  1, -1, -1, -1, -1, -1
	};
	static const int HighTone[] = {
	1,  1,  1,  1,  1,     -1, -1, -1, -1, // note one extra 1 to padd due to 50/8 remainder (1/2 the remainder)
	1,  1,  1,  1,         -1, -1, -1, -1,
	1,  1,  1,  1,         -1, -1, -1, -1,
	1,  1,  1,  1,         -1, -1, -1, -1,
	1,  1,  1,  1,         -1, -1, -1, -1,
	1,  1,  1,  1,     -1, -1, -1, -1, -1, // note one extra -1 to padd due to 50/8 remainder
	};
	*/
void GetHiLoTone(int *LowTone, int *HighTone, int clk, int LowToneFC, int HighToneFC) {
	int i,j=0;
	int Left_Modifier = ((clk % LowToneFC) % 2) + ((clk % LowToneFC)/2);
	int Right_Modifier = (clk % LowToneFC) / 2;
	//int HighToneMod = clk mod HighToneFC;
	int LeftHalfFCCnt = (LowToneFC % 2) + (LowToneFC/2); //truncate
	int FCs_per_clk = clk/LowToneFC;
	
	// need to correctly split up the clock to field clocks.
	// First attempt uses modifiers on each end to make up for when FCs don't evenly divide into Clk

	// start with LowTone
	// set extra 1 modifiers to make up for when FC doesn't divide evenly into Clk
	for (i = 0; i < Left_Modifier; i++) {
		LowTone[i] = 1;
	}

	// loop # of field clocks inside the main clock
	for (i = 0; i < (FCs_per_clk); i++) {
		// loop # of samples per field clock
		for (j = 0; j < LowToneFC; j++) {
			LowTone[(i*LowToneFC)+Left_Modifier+j] = ( j < LeftHalfFCCnt ) ? 1 : -1;
		}
	}

	int k;
	// add last -1 modifiers
	for (k = 0; k < Right_Modifier; k++) {
		LowTone[((i-1)*LowToneFC)+Left_Modifier+j+k] = -1;
	}

	// now do hightone
	Left_Modifier = ((clk % HighToneFC) % 2) + ((clk % HighToneFC)/2);
	Right_Modifier = (clk % HighToneFC) / 2;
	LeftHalfFCCnt = (HighToneFC % 2) + (HighToneFC/2); //truncate
	FCs_per_clk = clk/HighToneFC;

	for (i = 0; i < Left_Modifier; i++) {
		HighTone[i] = 1;
	}

	// loop # of field clocks inside the main clock
	for (i = 0; i < (FCs_per_clk); i++) {
		// loop # of samples per field clock
		for (j = 0; j < HighToneFC; j++) {
			HighTone[(i*HighToneFC)+Left_Modifier+j] = ( j < LeftHalfFCCnt ) ? 1 : -1;
		}
	}

	// add last -1 modifiers
	for (k = 0; k < Right_Modifier; k++) {
		PrintAndLog("(i-1)*HighToneFC+lm+j+k %i",((i-1)*HighToneFC)+Left_Modifier+j+k);
		HighTone[((i-1)*HighToneFC)+Left_Modifier+j+k] = -1;
	}
	if (g_debugMode == 2) {
		for ( i = 0; i < clk; i++) {
			PrintAndLog("Low: %i,  High: %i",LowTone[i],HighTone[i]);
		}
	}
}

//old CmdFSKdemod adapted by marshmellow 
//converts FSK to clear NRZ style wave.  (or demodulates)
int FSKToNRZ(int *data, int *dataLen, int clk, int LowToneFC, int HighToneFC) {
	uint8_t ans=0;
	if (clk == 0 || LowToneFC == 0 || HighToneFC == 0) {
		int firstClockEdge=0;
		ans = fskClocks((uint8_t *) &LowToneFC, (uint8_t *) &HighToneFC, (uint8_t *) &clk, false, &firstClockEdge);
		if (g_debugMode	> 1) {
			PrintAndLog	("DEBUG FSKtoNRZ: detected clocks: fc_low %i, fc_high %i, clk %i, firstClockEdge %i, ans %u", LowToneFC, HighToneFC, clk, firstClockEdge, ans);
		}
	}
	// currently only know fsk modulations with field clocks < 10 samples and > 4 samples. filter out to remove false positives (and possibly destroying ask/psk modulated waves...)
	if (ans == 0 || clk == 0 || LowToneFC == 0 || HighToneFC == 0 || LowToneFC > 10 || HighToneFC	< 4) {
		if (g_debugMode	> 1) {
			PrintAndLog	("DEBUG FSKtoNRZ: no fsk clocks found");
		}
		return 0;
	}
	int LowTone[clk];
	int HighTone[clk];
	GetHiLoTone(LowTone, HighTone, clk, LowToneFC, HighToneFC);
	
	int i, j;

	// loop through ([all samples] - clk)
	for (i = 0; i < *dataLen - clk; ++i) {
		int lowSum = 0, highSum = 0;

		// sum all samples together starting from this sample for [clk] samples for each tone (multiply tone value with sample data)
		for (j = 0; j < clk; ++j) {
			lowSum += LowTone[j] * data[i+j];
			highSum += HighTone[j] * data[i + j];
		}
		// get abs( [average sample value per clk] * 100 )  (or a rolling average of sorts)
		lowSum = abs(100 * lowSum / clk);
		highSum = abs(100 * highSum / clk);
		// save these back to buffer for later use
		data[i] = (highSum << 16) | lowSum;
	}

	// now we have the abs( [average sample value per clk] * 100 ) for each tone
	//   loop through again [all samples] - clk - 16  
	//                  note why 16???  is 16 the largest FC? changed to LowToneFC as that should be the > fc
	for(i = 0; i < *dataLen - clk - LowToneFC; ++i) {
		int lowTot = 0, highTot = 0;

		// sum a field clock width of abs( [average sample values per clk] * 100) for each tone
		for (j = 0; j < LowToneFC; ++j) {  //10 for fsk2
		  lowTot += (data[i + j] & 0xffff);
		}
		for (j = 0; j < HighToneFC; j++) {  //8 for fsk2
		  highTot += (data[i + j] >> 16);
		}

		// subtract the sum of lowTone averages by the sum of highTone averages as it 
		//   and write back the new graph value 
		data[i] = lowTot - highTot;
	}
	// update dataLen to what we put back to the data sample buffer
	*dataLen -= (clk + LowToneFC);
	return 0;
}

int usage_data_fsktonrz() {
		PrintAndLog("Usage: data fsktonrz c <clock> l <fc_low> f <fc_high>");
		PrintAndLog("Options:        ");
		PrintAndLog("       h            This help");
		PrintAndLog("       c <clock>    enter the a clock (omit to autodetect)");
		PrintAndLog("       l <fc_low>   enter a field clock (omit to autodetect)");
		PrintAndLog("       f <fc_high>  enter a field clock (omit to autodetect)");
		return 0;	
}

int CmdFSKToNRZ(const char *Cmd) {
	// take clk, fc_low, fc_high 
	//   blank = auto;
	bool errors = false;
	int clk = 0;
	char cmdp = 0;
	int fc_low = 10, fc_high = 8;
	while(param_getchar(Cmd, cmdp) != 0x00)
	{
		switch(param_getchar(Cmd, cmdp))
		{
		case 'h':
		case 'H':
			return usage_data_fsktonrz();
		case 'C':
		case 'c':
			clk = param_get32ex(Cmd, cmdp+1, 0, 10);
			cmdp += 2;
			break;
		case 'F':
		case 'f':
			fc_high = param_get32ex(Cmd, cmdp+1, 0, 10);
			cmdp += 2;
			break;
		case 'L':
		case 'l':
			fc_low = param_get32ex(Cmd, cmdp+1, 0, 10);
			cmdp += 2;
			break;
		default:
			PrintAndLog("Unknown parameter '%c'", param_getchar(Cmd, cmdp));
			errors = true;
			break;
		}
		if(errors) break;
	}
	//Validations
	if(errors) return usage_data_fsktonrz();

	setClockGrid(0,0);
	DemodBufferLen = 0;
	int ans = FSKToNRZ(GraphBuffer, &GraphTraceLen, clk, fc_low, fc_high);
	CmdNorm("");
	RepaintGraphWindow();
	return ans;
}


static command_t CommandTable[] =
{
	{"help",            CmdHelp,            1, "This help"},
	{"askedgedetect",   CmdAskEdgeDetect,   1, "[threshold] Adjust Graph for manual ask demod using the length of sample differences to detect the edge of a wave (use 20-45, def:25)"},
	{"autocorr",        CmdAutoCorr,        1, "[window length] [g] -- Autocorrelation over window - g to save back to GraphBuffer (overwrite)"},
	{"biphaserawdecode",CmdBiphaseDecodeRaw,1, "[offset] [invert<0|1>] [maxErr] -- Biphase decode bin stream in DemodBuffer (offset = 0|1 bits to shift the decode start)"},
	{"bin2hex",         Cmdbin2hex,         1, "bin2hex <digits>     -- Converts binary to hexadecimal"},
	{"bitsamples",      CmdBitsamples,      0, "Get raw samples as bitstring"},
	{"buffclear",       CmdBuffClear,       1, "Clear sample buffer and graph window"},
	{"dec",             CmdDec,             1, "Decimate samples"},
	{"detectclock",     CmdDetectClockRate, 1, "[modulation] Detect clock rate of wave in GraphBuffer (options: 'a','f','n','p' for ask, fsk, nrz, psk respectively)"},
	{"fsktonrz",        CmdFSKToNRZ,        1, "Convert fsk2 to nrz wave for alternate fsk demodulating (for weak fsk)"},
	{"getbitstream",    CmdGetBitStream,    1, "Convert GraphBuffer's >=1 values to 1 and <1 to 0"},
	{"grid",            CmdGrid,            1, "<x> <y> -- overlay grid on graph window, use zero value to turn off either"},
	{"hexsamples",      CmdHexsamples,      0, "<bytes> [<offset>] -- Dump big buffer as hex bytes"},
	{"hex2bin",         Cmdhex2bin,         1, "hex2bin <hexadecimal> -- Converts hexadecimal to binary"},
	{"hide",            CmdHide,            1, "Hide graph window"},
	{"hpf",             CmdHpf,             1, "Remove DC offset from trace"},
	{"load",            CmdLoad,            1, "<filename> -- Load trace (to graph window"},
	{"ltrim",           CmdLtrim,           1, "<samples> -- Trim samples from left of trace"},
	{"rtrim",           CmdRtrim,           1, "<location to end trace> -- Trim samples from right of trace"},
	{"mtrim",           CmdMtrim,           1, "<start> <stop> -- Trim out samples from the specified start to the specified stop"},
	{"manrawdecode",    Cmdmandecoderaw,    1, "[invert] [maxErr] -- Manchester decode binary stream in DemodBuffer"},
	{"norm",            CmdNorm,            1, "Normalize max/min to +/-128"},
	{"plot",            CmdPlot,            1, "Show graph window (hit 'h' in window for keystroke help)"},
	{"printdemodbuffer",CmdPrintDemodBuff,  1, "[x] [o] <offset> [l] <length> -- print the data in the DemodBuffer - 'x' for hex output"},
	{"rawdemod",        CmdRawDemod,        1, "[modulation] ... <options> -see help (h option) -- Demodulate the data in the GraphBuffer and output binary"},  
	{"samples",         CmdSamples,         0, "[512 - 40000] -- Get raw samples for graph window (GraphBuffer)"},
	{"save",            CmdSave,            1, "<filename> -- Save trace (from graph window)"},
	{"setgraphmarkers", CmdSetGraphMarkers, 1, "[orange_marker] [blue_marker] (in graph window)"},
	{"scale",           CmdScale,           1, "<int> -- Set cursor display scale"},
	{"setdebugmode",    CmdSetDebugMode,    1, "<0|1|2> -- Turn on or off Debugging Level for lf demods"},
	{"shiftgraphzero",  CmdGraphShiftZero,  1, "<shift> -- Shift 0 for Graphed wave + or - shift value"},
	{"dirthreshold",    CmdDirectionalThreshold,   1, "<thres up> <thres down> -- Max rising higher up-thres/ Min falling lower down-thres, keep rest as prev."},
	{"tune",            CmdTuneSamples,     0, "Get hw tune samples for graph window"},
	{"undec",           CmdUndec,           1, "Un-decimate samples by 2"},
	{"zerocrossings",   CmdZerocrossings,   1, "Count time between zero-crossings"},
	{NULL, NULL, 0, NULL}
};

int CmdData(const char *Cmd)
{
	CmdsParse(CommandTable, Cmd);
	return 0;
}

int CmdHelp(const char *Cmd)
{
	CmdsHelp(CommandTable);
	return 0;
}
