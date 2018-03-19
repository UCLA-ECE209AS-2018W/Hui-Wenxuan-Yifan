//-----------------------------------------------------------------------------
// Copyright (C) 2014
//
// This code is licensed to you under the terms of the GNU GPL, version 2 or,
// at your option, any later version. See the LICENSE.txt file for the text of
// the license.
//-----------------------------------------------------------------------------
// Low frequency demod/decode commands   - by marshmellow, holiman, iceman and
//                                         many others who came before
//
// NOTES: 
// LF Demod functions are placed here to allow the flexability to use client or
// device side. Most BUT NOT ALL of these functions are currenlty safe for 
// device side use currently. (DetectST for example...)
//
// There are likely many improvements to the code that could be made, please
// make suggestions...
//
// we tried to include author comments so any questions could be directed to 
// the source.
//
// There are 4 main sections of code below:
// Utilities Section: 
//    for general utilities used by multiple other functions
// Clock / Bitrate Detection Section:
//    for clock detection functions for each modulation
// Modulation Demods &/or Decoding Section:
//    for main general modulation demodulating and encoding decoding code.
// Tag format detection section:
//    for detection of specific tag formats within demodulated data
//
// marshmellow
//-----------------------------------------------------------------------------

#include <string.h>  // for memset, memcmp and size_t
#include "lfdemod.h"
#include <stdint.h>  // for uint_32+
#include <stdbool.h> // for bool
#include "parity.h"  // for parity test

//**********************************************************************************************
//---------------------------------Utilities Section--------------------------------------------
//**********************************************************************************************
#define LOWEST_DEFAULT_CLOCK 32
#define FSK_PSK_THRESHOLD   123

//to allow debug print calls when used not on device
void dummy(char *fmt, ...){}
#ifndef ON_DEVICE
#include "ui.h"
#include "cmdparser.h"
#include "cmddata.h"
#define prnt PrintAndLog
#else 
	uint8_t g_debugMode=0;
#define prnt dummy
#endif

uint8_t justNoise(uint8_t *BitStream, size_t size) {
	//test samples are not just noise
	uint8_t justNoise1 = 1;
	for(size_t idx=0; idx < size && justNoise1 ;idx++){
		justNoise1 = BitStream[idx] < FSK_PSK_THRESHOLD;
	}
	return justNoise1;
}

//by marshmellow
//get high and low values of a wave with passed in fuzz factor. also return noise test = 1 for passed or 0 for only noise
int getHiLo(uint8_t *BitStream, size_t size, int *high, int *low, uint8_t fuzzHi, uint8_t fuzzLo) {
	*high=0;
	*low=255;
	// get high and low thresholds 
	for (size_t i=0; i < size; i++){
		if (BitStream[i] > *high) *high = BitStream[i];
		if (BitStream[i] < *low) *low = BitStream[i];
	}
	if (*high < FSK_PSK_THRESHOLD) return -1; // just noise
	*high = ((*high-128)*fuzzHi + 12800)/100;
	*low = ((*low-128)*fuzzLo + 12800)/100;
	return 1;
}

// by marshmellow
// pass bits to be tested in bits, length bits passed in bitLen, and parity type (even=0 | odd=1) in pType
// returns 1 if passed
bool parityTest(uint32_t bits, uint8_t bitLen, uint8_t pType) {
	return oddparity32(bits) ^ pType;
}

// by marshmellow
// takes a array of binary values, start position, length of bits per parity (includes parity bit - MAX 32),
//   Parity Type (1 for odd; 0 for even; 2 for Always 1's; 3 for Always 0's), and binary Length (length to run)
size_t removeParity(uint8_t *BitStream, size_t startIdx, uint8_t pLen, uint8_t pType, size_t bLen) {
	uint32_t parityWd = 0;
	size_t bitCnt = 0;
	for (int word = 0; word < (bLen); word+=pLen) {
		for (int bit=0; bit < pLen; bit++) {
			if (word+bit >= bLen) break;
			parityWd = (parityWd << 1) | BitStream[startIdx+word+bit];
			BitStream[bitCnt++] = (BitStream[startIdx+word+bit]);
		}
		if (word+pLen > bLen) break;

		bitCnt--; // overwrite parity with next data
		// if parity fails then return 0
		switch (pType) {
			case 3: if (BitStream[bitCnt]==1) {return 0;} break; //should be 0 spacer bit
			case 2: if (BitStream[bitCnt]==0) {return 0;} break; //should be 1 spacer bit
			default: if (parityTest(parityWd, pLen, pType) == 0) {return 0;} break; //test parity
		}
		parityWd = 0;
	}
	// if we got here then all the parities passed
	//return size
	return bitCnt;
}

// by marshmellow
// takes a array of binary values, length of bits per parity (includes parity bit),
//   Parity Type (1 for odd; 0 for even; 2 Always 1's; 3 Always 0's), and binary Length (length to run)
//   Make sure *dest is long enough to store original sourceLen + #_of_parities_to_be_added
size_t addParity(uint8_t *BitSource, uint8_t *dest, uint8_t sourceLen, uint8_t pLen, uint8_t pType) {
	uint32_t parityWd = 0;
	size_t j = 0, bitCnt = 0;
	for (int word = 0; word < sourceLen; word+=pLen-1) {
		for (int bit=0; bit < pLen-1; bit++){
			parityWd = (parityWd << 1) | BitSource[word+bit];
			dest[j++] = (BitSource[word+bit]);
		}
		// if parity fails then return 0
		switch (pType) {
			case 3: dest[j++]=0; break; // marker bit which should be a 0
			case 2: dest[j++]=1; break; // marker bit which should be a 1
			default: 
				dest[j++] = parityTest(parityWd, pLen-1, pType) ^ 1;
				break;
		}
		bitCnt += pLen;
		parityWd = 0;
	}
	// if we got here then all the parities passed
	//return ID start index and size
	return bitCnt;
}

uint32_t bytebits_to_byte(uint8_t *src, size_t numbits) {
	uint32_t num = 0;
	for(int i = 0 ; i < numbits ; i++)
	{
		num = (num << 1) | (*src);
		src++;
	}
	return num;
}

//least significant bit first
uint32_t bytebits_to_byteLSBF(uint8_t *src, size_t numbits) {
	uint32_t num = 0;
	for(int i = 0 ; i < numbits ; i++)
	{
		num = (num << 1) | *(src + (numbits-(i+1)));
	}
	return num;
}

// search for given preamble in given BitStream and return success=1 or fail=0 and startIndex (where it was found) and length if not fineone 
// fineone does not look for a repeating preamble for em4x05/4x69 sends preamble once, so look for it once in the first pLen bits
bool preambleSearchEx(uint8_t *BitStream, uint8_t *preamble, size_t pLen, size_t *size, size_t *startIdx, bool findone) {
	// Sanity check.  If preamble length is bigger than bitstream length.
	if ( *size <= pLen ) return false;

	uint8_t foundCnt = 0;
	for (size_t idx = 0; idx < *size - pLen; idx++) {
		if (memcmp(BitStream+idx, preamble, pLen) == 0) {
			//first index found
			foundCnt++;
			if (foundCnt == 1) {
				if (g_debugMode) prnt("DEBUG: preamble found at %u", idx);
				*startIdx = idx;
				if (findone) return true;
			} else if (foundCnt == 2) {
				*size = idx - *startIdx;
				return true;
			}
		}
	}
	return false;
}

//by marshmellow
//search for given preamble in given BitStream and return success=1 or fail=0 and startIndex and length
uint8_t preambleSearch(uint8_t *BitStream, uint8_t *preamble, size_t pLen, size_t *size, size_t *startIdx) {
	return (preambleSearchEx(BitStream, preamble, pLen, size, startIdx, false)) ? 1 : 0;
}

// find start of modulating data (for fsk and psk) in case of beginning noise or slow chip startup.
size_t findModStart(uint8_t dest[], size_t size, uint8_t expWaveSize) {
	size_t i = 0;
	size_t waveSizeCnt = 0;
	uint8_t thresholdCnt = 0;
	bool isAboveThreshold = dest[i++] >= FSK_PSK_THRESHOLD;
	for (; i < size-20; i++ ) {
		if(dest[i] < FSK_PSK_THRESHOLD && isAboveThreshold) {
			thresholdCnt++;
			if (thresholdCnt > 2 && waveSizeCnt < expWaveSize+1) break;			
			isAboveThreshold = false;
			waveSizeCnt = 0;
		} else if (dest[i] >= FSK_PSK_THRESHOLD && !isAboveThreshold) {
			thresholdCnt++;
			if (thresholdCnt > 2 && waveSizeCnt < expWaveSize+1) break;			
			isAboveThreshold = true;
			waveSizeCnt = 0;
		} else {
			waveSizeCnt++;
		}
		if (thresholdCnt > 10) break;
	}
	if (g_debugMode == 2) prnt("DEBUG: threshold Count reached at %u, count: %u",i, thresholdCnt);
	return i;
}

int getClosestClock(int testclk) {
	uint8_t fndClk[] = {8,16,32,40,50,64,128};

	for (uint8_t clkCnt = 0; clkCnt<7; clkCnt++)
		if (testclk >= fndClk[clkCnt]-(fndClk[clkCnt]/8) && testclk <= fndClk[clkCnt]+1)
			return fndClk[clkCnt];

	return 0;
}

void getNextLow(uint8_t samples[], size_t size, int low, size_t *i) {
	while ((samples[*i] > low) && (*i < size))
		*i+=1;
}

void getNextHigh(uint8_t samples[], size_t size, int high, size_t *i) {
	while ((samples[*i] < high) && (*i < size))
		*i+=1;
}

// load wave counters
bool loadWaveCounters(uint8_t samples[], size_t size, int lowToLowWaveLen[], int highToLowWaveLen[], int *waveCnt, int *skip, int *minClk, int *high, int *low) {
	size_t i=0, firstLow, firstHigh;
	size_t testsize = (size < 512) ? size : 512;

	if ( getHiLo(samples, testsize, high, low, 80, 80) == -1 ) {
		if (g_debugMode==2) prnt("DEBUG STT: just noise detected - quitting");
		return false; //just noise
	}

	// get to first full low to prime loop and skip incomplete first pulse
	getNextHigh(samples, size, *high, &i);
	getNextLow(samples, size, *low, &i);
	*skip = i;

	// populate tmpbuff buffer with pulse lengths
	while (i < size) {
		// measure from low to low
		firstLow = i;
		//find first high point for this wave
		getNextHigh(samples, size, *high, &i);
		firstHigh = i;

		getNextLow(samples, size, *low, &i);

		if (*waveCnt >= (size/LOWEST_DEFAULT_CLOCK))
			break;

		highToLowWaveLen[*waveCnt] = i - firstHigh; //first high to first low
		lowToLowWaveLen[*waveCnt] = i - firstLow;
		*waveCnt += 1;
		if (i-firstLow < *minClk && i < size) {
			*minClk = i - firstLow;
		}
	}
	return true;
}

size_t pskFindFirstPhaseShift(uint8_t samples[], size_t size, uint8_t *curPhase, size_t waveStart, uint16_t fc, uint16_t *fullWaveLen) {
	uint16_t loopCnt = (size+3 < 4096) ? size : 4096;  //don't need to loop through entire array...

	uint16_t avgWaveVal=0, lastAvgWaveVal=0;
	size_t i = waveStart, waveEnd, waveLenCnt, firstFullWave;
	for (; i<loopCnt; i++) {
		// find peak // was "samples[i] + fc" but why?  must have been used to weed out some wave error... removed..
		if (samples[i] < samples[i+1] && samples[i+1] >= samples[i+2]){
			waveEnd = i+1;
			if (g_debugMode == 2) prnt("DEBUG PSK: waveEnd: %u, waveStart: %u", waveEnd, waveStart);
			waveLenCnt = waveEnd-waveStart;
			if (waveLenCnt > fc && waveStart > fc && !(waveLenCnt > fc+8)){ //not first peak and is a large wave but not out of whack
				lastAvgWaveVal = avgWaveVal/(waveLenCnt);
				firstFullWave = waveStart;
				*fullWaveLen = waveLenCnt;
				//if average wave value is > graph 0 then it is an up wave or a 1 (could cause inverting)
				if (lastAvgWaveVal > FSK_PSK_THRESHOLD) *curPhase ^= 1;
				return firstFullWave;
			}
			waveStart = i+1;
			avgWaveVal = 0;
		}
		avgWaveVal += samples[i+2];
	}
	return 0;
}

//by marshmellow
//amplify based on ask edge detection  -  not accurate enough to use all the time
void askAmp(uint8_t *BitStream, size_t size) {
	uint8_t Last = 128;
	for(size_t i = 1; i<size; i++){
		if (BitStream[i]-BitStream[i-1]>=30) //large jump up
			Last = 255;
		else if(BitStream[i-1]-BitStream[i]>=20) //large jump down
			Last = 0;

		BitStream[i-1] = Last;
	}
	return;
}

uint32_t manchesterEncode2Bytes(uint16_t datain) {
	uint32_t output = 0;
	uint8_t curBit = 0;
	for (uint8_t i=0; i<16; i++) {
		curBit = (datain >> (15-i) & 1);
		output |= (1<<(((15-i)*2)+curBit));
	}
	return output;
}

//by marshmellow
//encode binary data into binary manchester 
//NOTE: BitStream must have triple the size of "size" available in memory to do the swap
int ManchesterEncode(uint8_t *BitStream, size_t size) {
	//allow up to 4K out (means BitStream must be at least 2048+4096 to handle the swap)
	size = (size>2048) ? 2048 : size;
	size_t modIdx = size;
	size_t i;
	for (size_t idx=0; idx < size; idx++){
		BitStream[idx+modIdx++] = BitStream[idx];
		BitStream[idx+modIdx++] = BitStream[idx]^1;
	}
	for (i=0; i<(size*2); i++){
		BitStream[i] = BitStream[i+size];
	}
	return i;
}

// by marshmellow
// to detect a wave that has heavily clipped (clean) samples
uint8_t DetectCleanAskWave(uint8_t dest[], size_t size, uint8_t high, uint8_t low) {
	bool allArePeaks = true;
	uint16_t cntPeaks=0;
	size_t loopEnd = 512+160;
	if (loopEnd > size) loopEnd = size;
	for (size_t i=160; i<loopEnd; i++){
		if (dest[i]>low && dest[i]<high) 
			allArePeaks = false;
		else
			cntPeaks++;
	}
	if (!allArePeaks){
		if (cntPeaks > 300) return true;
	}
	return allArePeaks;
}

//**********************************************************************************************
//-------------------Clock / Bitrate Detection Section------------------------------------------
//**********************************************************************************************

// by marshmellow
// to help detect clocks on heavily clipped samples
// based on count of low to low
int DetectStrongAskClock(uint8_t dest[], size_t size, int high, int low, int *clock) {
	size_t startwave;
	size_t i = 100;
	size_t minClk = 255;
	int shortestWaveIdx = 0;
		// get to first full low to prime loop and skip incomplete first pulse
	getNextHigh(dest, size, high, &i);
	getNextLow(dest, size, low, &i);

	// loop through all samples
	while (i < size) {
		// measure from low to low
		startwave = i;

		getNextHigh(dest, size, high, &i);
		getNextLow(dest, size, low, &i);
		//get minimum measured distance
		if (i-startwave < minClk && i < size) {
			minClk = i - startwave;
			shortestWaveIdx = startwave;
		}
	}
	// set clock
	if (g_debugMode==2) prnt("DEBUG ASK: DetectStrongAskClock smallest wave: %d",minClk);
	*clock = getClosestClock(minClk);
	if (*clock == 0) 
		return 0;
	
	return shortestWaveIdx;
}

// by marshmellow
// not perfect especially with lower clocks or VERY good antennas (heavy wave clipping)
// maybe somehow adjust peak trimming value based on samples to fix?
// return start index of best starting position for that clock and return clock (by reference)
int DetectASKClock(uint8_t dest[], size_t size, int *clock, int maxErr) {
	size_t i=1;
	uint8_t clk[] = {255,8,16,32,40,50,64,100,128,255};
	uint8_t clkEnd = 9;
	uint8_t loopCnt = 255;  //don't need to loop through entire array...
	if (size <= loopCnt+60) return -1; //not enough samples
	size -= 60; //sometimes there is a strange end wave - filter out this....
	//if we already have a valid clock
	uint8_t clockFnd=0;
	for (;i<clkEnd;++i)
		if (clk[i] == *clock) clockFnd = i;
		//clock found but continue to find best startpos

	//get high and low peak
	int peak, low;
	if (getHiLo(dest, loopCnt, &peak, &low, 75, 75) < 1) return -1;
	
	//test for large clean peaks
	if (!clockFnd){
		if (DetectCleanAskWave(dest, size, peak, low)==1){
			int ans = DetectStrongAskClock(dest, size, peak, low, clock);
			if (g_debugMode==2) prnt("DEBUG ASK: detectaskclk Clean Ask Wave Detected: clk %i, ShortestWave: %i",clock, ans);
			if (ans > 0) {
				return ans; //return shortest wave start position
			}
		}
	}
	uint8_t ii;
	uint8_t clkCnt, tol = 0;
	uint16_t bestErr[]={1000,1000,1000,1000,1000,1000,1000,1000,1000};
	uint8_t bestStart[]={0,0,0,0,0,0,0,0,0};
	size_t errCnt = 0;
	size_t arrLoc, loopEnd;

	if (clockFnd>0) {
		clkCnt = clockFnd;
		clkEnd = clockFnd+1;
	}
	else clkCnt=1;

	//test each valid clock from smallest to greatest to see which lines up
	for(; clkCnt < clkEnd; clkCnt++){
		if (clk[clkCnt] <= 32){
			tol=1;
		}else{
			tol=0;
		}
		//if no errors allowed - keep start within the first clock
		if (!maxErr && size > clk[clkCnt]*2 + tol && clk[clkCnt]<128) loopCnt=clk[clkCnt]*2;
		bestErr[clkCnt]=1000;
		//try lining up the peaks by moving starting point (try first few clocks)
		for (ii=0; ii < loopCnt; ii++){
			if (dest[ii] < peak && dest[ii] > low) continue;

			errCnt=0;
			// now that we have the first one lined up test rest of wave array
			loopEnd = ((size-ii-tol) / clk[clkCnt]) - 1;
			for (i=0; i < loopEnd; ++i){
				arrLoc = ii + (i * clk[clkCnt]);
				if (dest[arrLoc] >= peak || dest[arrLoc] <= low){
				}else if (dest[arrLoc-tol] >= peak || dest[arrLoc-tol] <= low){
				}else if (dest[arrLoc+tol] >= peak || dest[arrLoc+tol] <= low){
				}else{  //error no peak detected
					errCnt++;
				}
			}
			//if we found no errors then we can stop here and a low clock (common clocks)
			//  this is correct one - return this clock
			if (g_debugMode == 2) prnt("DEBUG ASK: clk %d, err %d, startpos %d, endpos %d",clk[clkCnt],errCnt,ii,i);
			if(errCnt==0 && clkCnt<7) { 
				if (!clockFnd) *clock = clk[clkCnt];
				return ii;
			}
			//if we found errors see if it is lowest so far and save it as best run
			if(errCnt<bestErr[clkCnt]){
				bestErr[clkCnt]=errCnt;
				bestStart[clkCnt]=ii;
			}
		}
	}
	uint8_t iii;
	uint8_t best=0;
	for (iii=1; iii<clkEnd; ++iii){
		if (bestErr[iii] < bestErr[best]){
			if (bestErr[iii] == 0) bestErr[iii]=1;
			// current best bit to error ratio     vs  new bit to error ratio
			if ( (size/clk[best])/bestErr[best] < (size/clk[iii])/bestErr[iii] ){
				best = iii;
			}
		}
		if (g_debugMode == 2) prnt("DEBUG ASK: clk %d, # Errors %d, Current Best Clk %d, bestStart %d",clk[iii],bestErr[iii],clk[best],bestStart[best]);
	}
	if (!clockFnd) *clock = clk[best];
	return bestStart[best];
}

int DetectStrongNRZClk(uint8_t *dest, size_t size, int peak, int low, bool *strong) {
	//find shortest transition from high to low
	*strong = false;
	size_t i = 0;
	size_t transition1 = 0;
	int lowestTransition = 255;
	bool lastWasHigh = false;
	size_t transitionSampleCount = 0;
	//find first valid beginning of a high or low wave
	while ((dest[i] >= peak || dest[i] <= low) && (i < size))
		++i;
	while ((dest[i] < peak && dest[i] > low) && (i < size))
		++i;
	lastWasHigh = (dest[i] >= peak);

	if (i==size) return 0;
	transition1 = i;

	for (;i < size; i++) {
		if ((dest[i] >= peak && !lastWasHigh) || (dest[i] <= low && lastWasHigh)) {
			lastWasHigh = (dest[i] >= peak);
			if (i-transition1 < lowestTransition) lowestTransition = i-transition1;
			transition1 = i;
		} else if (dest[i] < peak && dest[i] > low) {
			transitionSampleCount++;
		}
	}
	if (lowestTransition == 255) lowestTransition = 0;
	if (g_debugMode==2) prnt("DEBUG NRZ: detectstrongNRZclk smallest wave: %d",lowestTransition);
	// if less than 10% of the samples were not peaks (or 90% were peaks) then we have a strong wave
	if (transitionSampleCount / size < 10) {
		*strong = true;
		lowestTransition = getClosestClock(lowestTransition);
	}
	return lowestTransition;
}

//by marshmellow
//detect nrz clock by reading #peaks vs no peaks(or errors)
int DetectNRZClock(uint8_t dest[], size_t size, int clock, size_t *clockStartIdx) {
	size_t i=0;
	uint8_t clk[]={8,16,32,40,50,64,100,128,255};
	size_t loopCnt = 4096;  //don't need to loop through entire array...
	if (size == 0) return 0;
	if (size<loopCnt) loopCnt = size-20;
	//if we already have a valid clock quit
	for (; i < 8; ++i)
		if (clk[i] == clock) return clock;

	//get high and low peak
	int peak, low;
	if (getHiLo(dest, loopCnt, &peak, &low, 90, 90) < 1) return 0;

	bool strong = false;
	int lowestTransition = DetectStrongNRZClk(dest, size-20, peak, low, &strong);
	if (strong) return lowestTransition;
	size_t ii;
	uint8_t clkCnt;
	uint8_t tol = 0;
	uint16_t smplCnt = 0;
	int16_t peakcnt = 0;
	int16_t peaksdet[] = {0,0,0,0,0,0,0,0};
	uint16_t minPeak = 255;
	bool firstpeak = true;
	//test for large clipped waves - ignore first peak
	for (i=0; i<loopCnt; i++) {
		if (dest[i] >= peak || dest[i] <= low) {
			if (firstpeak) continue;
			smplCnt++;
		} else {
			firstpeak = false;
			if (smplCnt > 0) {
				if (minPeak > smplCnt && smplCnt > 7) minPeak = smplCnt;
				peakcnt++;
				if (g_debugMode == 2) prnt("DEBUG NRZ: minPeak: %d, smplCnt: %d, peakcnt: %d",minPeak,smplCnt,peakcnt);
				smplCnt = 0;				
			}
		}
	}
	if (minPeak < 8) return 0;
	bool errBitHigh = 0;
	bool bitHigh = 0;
	uint8_t ignoreCnt = 0;
	uint8_t ignoreWindow = 4;
	bool lastPeakHigh = 0;
	int lastBit = 0; 
	size_t bestStart[]={0,0,0,0,0,0,0,0,0};
	peakcnt=0;
	//test each valid clock from smallest to greatest to see which lines up
	for(clkCnt=0; clkCnt < 8; ++clkCnt) {
		//ignore clocks smaller than smallest peak
		if (clk[clkCnt] < minPeak - (clk[clkCnt]/4)) continue;
		//try lining up the peaks by moving starting point (try first 256)
		for (ii=20; ii < loopCnt; ++ii) {
			if ((dest[ii] >= peak) || (dest[ii] <= low)) {
				peakcnt = 0;
				bitHigh = false;
				ignoreCnt = 0;
				lastBit = ii-clk[clkCnt]; 
				//loop through to see if this start location works
				for (i = ii; i < size-20; ++i) {
					//if we are at a clock bit
					if ((i >= lastBit + clk[clkCnt] - tol) && (i <= lastBit + clk[clkCnt] + tol)) {
						//test high/low
						if (dest[i] >= peak || dest[i] <= low) {
							//if same peak don't count it
							if ((dest[i] >= peak && !lastPeakHigh) || (dest[i] <= low && lastPeakHigh)) {
								peakcnt++;
							}
							lastPeakHigh = (dest[i] >= peak);
							bitHigh = true;
							errBitHigh = false;
							ignoreCnt = ignoreWindow;
							lastBit += clk[clkCnt];
						} else if (i == lastBit + clk[clkCnt] + tol) {
							lastBit += clk[clkCnt];
						}
					//else if not a clock bit and no peaks
					} else if (dest[i] < peak && dest[i] > low) {
						if (ignoreCnt==0) {
							bitHigh=false;
							if (errBitHigh==true) peakcnt--;
							errBitHigh=false;
						} else {
							ignoreCnt--;
						}
						// else if not a clock bit but we have a peak
					} else if ((dest[i]>=peak || dest[i]<=low) && (!bitHigh)) {
						//error bar found no clock...
						errBitHigh=true;
					}
				}
				if(peakcnt>peaksdet[clkCnt]) {
					bestStart[clkCnt]=ii;
					peaksdet[clkCnt]=peakcnt;
				}
			}
		}
	}
	int iii=7;
	uint8_t best=0;
	for (iii=7; iii > 0; iii--) {
		if ((peaksdet[iii] >= (peaksdet[best]-1)) && (peaksdet[iii] <= peaksdet[best]+1) && lowestTransition) {
			if (clk[iii] > (lowestTransition - (clk[iii]/8)) && clk[iii] < (lowestTransition + (clk[iii]/8))) {
				best = iii;
			}
		} else if (peaksdet[iii] > peaksdet[best]) {
			best = iii;
		}
		if (g_debugMode==2) prnt("DEBUG NRZ: Clk: %d, peaks: %d, minPeak: %d, bestClk: %d, lowestTrs: %d",clk[iii],peaksdet[iii],minPeak, clk[best], lowestTransition);
	}
	*clockStartIdx	= bestStart[best];
	return clk[best];
}

//by marshmellow
//countFC is to detect the field clock lengths.
//counts and returns the 2 most common wave lengths
//mainly used for FSK field clock detection
uint16_t countFC(uint8_t *BitStream, size_t size, uint8_t fskAdj) {
	uint8_t fcLens[] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
	uint16_t fcCnts[] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
	uint8_t fcLensFnd = 0;
	uint8_t lastFCcnt = 0;
	uint8_t fcCounter = 0;
	size_t i;
	if (size < 180) return 0;

	// prime i to first up transition
	for (i = 160; i < size-20; i++)
		if (BitStream[i] > BitStream[i-1] && BitStream[i] >= BitStream[i+1])
			break;

	for (; i < size-20; i++){
		if (BitStream[i] > BitStream[i-1] && BitStream[i] >= BitStream[i+1]){
			// new up transition
			fcCounter++;
			if (fskAdj){
				//if we had 5 and now have 9 then go back to 8 (for when we get a fc 9 instead of an 8)
				if (lastFCcnt==5 && fcCounter==9) fcCounter--;
				//if fc=9 or 4 add one (for when we get a fc 9 instead of 10 or a 4 instead of a 5)
				if ((fcCounter==9) || fcCounter==4) fcCounter++;
			// save last field clock count  (fc/xx)
			lastFCcnt = fcCounter;
			}
			// find which fcLens to save it to:
			for (int ii=0; ii<15; ii++){
				if (fcLens[ii]==fcCounter){
					fcCnts[ii]++;
					fcCounter=0;
					break;
				}
			}
			if (fcCounter>0 && fcLensFnd<15){
				//add new fc length 
				fcCnts[fcLensFnd]++;
				fcLens[fcLensFnd++]=fcCounter;
			}
			fcCounter=0;
		} else {
			// count sample
			fcCounter++;
		}
	}
	
	uint8_t best1=14, best2=14, best3=14;
	uint16_t maxCnt1=0;
	// go through fclens and find which ones are bigest 2  
	for (i=0; i<15; i++){
		// get the 3 best FC values
		if (fcCnts[i]>maxCnt1) {
			best3=best2;
			best2=best1;
			maxCnt1=fcCnts[i];
			best1=i;
		} else if(fcCnts[i]>fcCnts[best2]){
			best3=best2;
			best2=i;
		} else if(fcCnts[i]>fcCnts[best3]){
			best3=i;
		}
		if (g_debugMode==2) prnt("DEBUG countfc: FC %u, Cnt %u, best fc: %u, best2 fc: %u",fcLens[i],fcCnts[i],fcLens[best1],fcLens[best2]);
		if (fcLens[i]==0) break;
	}
	if (fcLens[best1]==0) return 0;
	uint8_t fcH=0, fcL=0;
	if (fcLens[best1]>fcLens[best2]){
		fcH=fcLens[best1];
		fcL=fcLens[best2];
	} else{
		fcH=fcLens[best2];
		fcL=fcLens[best1];
	}
	if ((size-180)/fcH/3 > fcCnts[best1]+fcCnts[best2]) {
		if (g_debugMode==2) prnt("DEBUG countfc: fc is too large: %u > %u. Not psk or fsk",(size-180)/fcH/3,fcCnts[best1]+fcCnts[best2]);
		return 0; //lots of waves not psk or fsk
	}
	// TODO: take top 3 answers and compare to known Field clocks to get top 2

	uint16_t fcs = (((uint16_t)fcH)<<8) | fcL;
	if (fskAdj) return fcs;
	return (uint16_t)fcLens[best2] << 8 | fcLens[best1];
}

//by marshmellow
//detect psk clock by reading each phase shift
// a phase shift is determined by measuring the sample length of each wave
int DetectPSKClock(uint8_t dest[], size_t size, int clock, size_t *firstPhaseShift, uint8_t *curPhase, uint8_t *fc) {
	uint8_t clk[]={255,16,32,40,50,64,100,128,255}; //255 is not a valid clock
	uint16_t loopCnt = 4096;  //don't need to loop through entire array...
	if (size == 0) return 0;
	if (size+3<loopCnt) loopCnt = size-20;

	uint16_t fcs = countFC(dest, size, 0);
	*fc = fcs & 0xFF;
	if (g_debugMode==2) prnt("DEBUG PSK: FC: %d, FC2: %d",*fc, fcs>>8);
	if ((fcs>>8) == 10 && *fc == 8) return 0;
	if (*fc!=2 && *fc!=4 && *fc!=8) return 0;

	//if we already have a valid clock quit
	size_t i=1;
	for (; i < 8; ++i)
		if (clk[i] == clock) return clock;

	size_t waveStart=0, waveEnd=0, firstFullWave=0, lastClkBit=0;

	uint8_t clkCnt, tol=1;
	uint16_t peakcnt=0, errCnt=0, waveLenCnt=0, fullWaveLen=0;
	uint16_t bestErr[]={1000,1000,1000,1000,1000,1000,1000,1000,1000};
	uint16_t peaksdet[]={0,0,0,0,0,0,0,0,0};

	//find start of modulating data in trace 
	i = findModStart(dest, size, *fc);

	firstFullWave = pskFindFirstPhaseShift(dest, size, curPhase, i, *fc, &fullWaveLen);
	if (firstFullWave == 0) {
		// no phase shift detected - could be all 1's or 0's - doesn't matter where we start
		// so skip a little to ensure we are past any Start Signal
		firstFullWave = 160;
		fullWaveLen = 0;
	}

	*firstPhaseShift = firstFullWave;
	if (g_debugMode ==2) prnt("DEBUG PSK: firstFullWave: %d, waveLen: %d",firstFullWave,fullWaveLen);
	//test each valid clock from greatest to smallest to see which lines up
	for(clkCnt=7; clkCnt >= 1 ; clkCnt--) {
		tol = *fc/2;
		lastClkBit = firstFullWave; //set end of wave as clock align
		waveStart = 0;
		errCnt=0;
		peakcnt=0;
		if (g_debugMode == 2) prnt("DEBUG PSK: clk: %d, lastClkBit: %d",clk[clkCnt],lastClkBit);

		for (i = firstFullWave+fullWaveLen-1; i < loopCnt-2; i++){
			//top edge of wave = start of new wave 
			if (dest[i] < dest[i+1] && dest[i+1] >= dest[i+2]){
				if (waveStart == 0) {
					waveStart = i+1;
					waveLenCnt=0;
				} else { //waveEnd
					waveEnd = i+1;
					waveLenCnt = waveEnd-waveStart;
					if (waveLenCnt > *fc){ 
						//if this wave is a phase shift
						if (g_debugMode == 2) prnt("DEBUG PSK: phase shift at: %d, len: %d, nextClk: %d, i: %d, fc: %d",waveStart,waveLenCnt,lastClkBit+clk[clkCnt]-tol,i+1,*fc);
						if (i+1 >= lastClkBit + clk[clkCnt] - tol){ //should be a clock bit
							peakcnt++;
							lastClkBit+=clk[clkCnt];
						} else if (i<lastClkBit+8){
							//noise after a phase shift - ignore
						} else { //phase shift before supposed to based on clock
							errCnt++;
						}
					} else if (i+1 > lastClkBit + clk[clkCnt] + tol + *fc){
						lastClkBit+=clk[clkCnt]; //no phase shift but clock bit
					}
					waveStart=i+1;
				}
			}
		}
		if (errCnt == 0){
			return clk[clkCnt];
		}
		if (errCnt <= bestErr[clkCnt]) bestErr[clkCnt]=errCnt;
		if (peakcnt > peaksdet[clkCnt]) peaksdet[clkCnt]=peakcnt;
	} 
	//all tested with errors 
	//return the highest clk with the most peaks found
	uint8_t best=7;
	for (i=7; i>=1; i--){
		if (peaksdet[i] > peaksdet[best]) {
			best = i;
		}
		if (g_debugMode == 2) prnt("DEBUG PSK: Clk: %d, peaks: %d, errs: %d, bestClk: %d",clk[i],peaksdet[i],bestErr[i],clk[best]);
	}
	return clk[best];
}

//by marshmellow
//detects the bit clock for FSK given the high and low Field Clocks
uint8_t detectFSKClk(uint8_t *BitStream, size_t size, uint8_t fcHigh, uint8_t fcLow, int *firstClockEdge) {
	uint8_t clk[] = {8,16,32,40,50,64,100,128,0};
	uint16_t rfLens[] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
	uint8_t rfCnts[] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
	uint8_t rfLensFnd = 0;
	uint8_t lastFCcnt = 0;
	uint16_t fcCounter = 0;
	uint16_t rfCounter = 0;
	uint8_t firstBitFnd = 0;
	size_t i;
	if (size == 0) return 0;

	uint8_t fcTol = ((fcHigh*100 - fcLow*100)/2 + 50)/100; //(uint8_t)(0.5+(float)(fcHigh-fcLow)/2);
	rfLensFnd=0;
	fcCounter=0;
	rfCounter=0;
	firstBitFnd=0;
	//PrintAndLog("DEBUG: fcTol: %d",fcTol);
	// prime i to first peak / up transition
	for (i = 160; i < size-20; i++)
		if (BitStream[i] > BitStream[i-1] && BitStream[i]>=BitStream[i+1])
			break;

	for (; i < size-20; i++){
		fcCounter++;
		rfCounter++;

		if (BitStream[i] <= BitStream[i-1] || BitStream[i] < BitStream[i+1]) 
			continue;		
		// else new peak 
		// if we got less than the small fc + tolerance then set it to the small fc
		// if it is inbetween set it to the last counter
		if (fcCounter < fcHigh && fcCounter > fcLow)
			fcCounter = lastFCcnt;
		else if (fcCounter < fcLow+fcTol) 
			fcCounter = fcLow;
		else //set it to the large fc
			fcCounter = fcHigh;

		//look for bit clock  (rf/xx)
		if ((fcCounter < lastFCcnt || fcCounter > lastFCcnt)){
			//not the same size as the last wave - start of new bit sequence
			if (firstBitFnd > 1){ //skip first wave change - probably not a complete bit
				for (int ii=0; ii<15; ii++){
					if (rfLens[ii] >= (rfCounter-4) && rfLens[ii] <= (rfCounter+4)){
						rfCnts[ii]++;
						rfCounter = 0;
						break;
					}
				}
				if (rfCounter > 0 && rfLensFnd < 15){
					//PrintAndLog("DEBUG: rfCntr %d, fcCntr %d",rfCounter,fcCounter);
					rfCnts[rfLensFnd]++;
					rfLens[rfLensFnd++] = rfCounter;
				}
			} else {
				*firstClockEdge = i;
				firstBitFnd++;
			}
			rfCounter=0;
			lastFCcnt=fcCounter;
		}
		fcCounter=0;
	}
	uint8_t rfHighest=15, rfHighest2=15, rfHighest3=15;

	for (i=0; i<15; i++){
		//get highest 2 RF values  (might need to get more values to compare or compare all?)
		if (rfCnts[i]>rfCnts[rfHighest]){
			rfHighest3=rfHighest2;
			rfHighest2=rfHighest;
			rfHighest=i;
		} else if(rfCnts[i]>rfCnts[rfHighest2]){
			rfHighest3=rfHighest2;
			rfHighest2=i;
		} else if(rfCnts[i]>rfCnts[rfHighest3]){
			rfHighest3=i;
		}
		if (g_debugMode==2) prnt("DEBUG FSK: RF %d, cnts %d",rfLens[i], rfCnts[i]);
	}  
	// set allowed clock remainder tolerance to be 1 large field clock length+1 
	//   we could have mistakenly made a 9 a 10 instead of an 8 or visa versa so rfLens could be 1 FC off  
	uint8_t tol1 = fcHigh+1; 
	
	if (g_debugMode==2) prnt("DEBUG FSK: most counted rf values: 1 %d, 2 %d, 3 %d",rfLens[rfHighest],rfLens[rfHighest2],rfLens[rfHighest3]);

	// loop to find the highest clock that has a remainder less than the tolerance
	//   compare samples counted divided by
	// test 128 down to 32 (shouldn't be possible to have fc/10 & fc/8 and rf/16 or less)
	int ii=7;
	for (; ii>=2; ii--){
		if (rfLens[rfHighest] % clk[ii] < tol1 || rfLens[rfHighest] % clk[ii] > clk[ii]-tol1){
			if (rfLens[rfHighest2] % clk[ii] < tol1 || rfLens[rfHighest2] % clk[ii] > clk[ii]-tol1){
				if (rfLens[rfHighest3] % clk[ii] < tol1 || rfLens[rfHighest3] % clk[ii] > clk[ii]-tol1){
					if (g_debugMode==2) prnt("DEBUG FSK: clk %d divides into the 3 most rf values within tolerance",clk[ii]);
					break;
				}
			}
		}
	}

	if (ii<2) return 0; // oops we went too far

	return clk[ii];
}

//**********************************************************************************************
//--------------------Modulation Demods &/or Decoding Section-----------------------------------
//**********************************************************************************************

// look for Sequence Terminator - should be pulses of clk*(1 or 2), clk*2, clk*(1.5 or 2), by idx we mean graph position index...
bool findST(int *stStopLoc, int *stStartIdx, int lowToLowWaveLen[], int highToLowWaveLen[], int clk, int tol, int buffSize, size_t *i) {
	if (buffSize < *i+4) return false;

	for (; *i < buffSize - 4; *i+=1) {
		*stStartIdx += lowToLowWaveLen[*i]; //caution part of this wave may be data and part may be ST....  to be accounted for in main function for now...
		if (lowToLowWaveLen[*i] >= clk*1-tol && lowToLowWaveLen[*i] <= (clk*2)+tol && highToLowWaveLen[*i] < clk+tol) {           //1 to 2 clocks depending on 2 bits prior
			if (lowToLowWaveLen[*i+1] >= clk*2-tol && lowToLowWaveLen[*i+1] <= clk*2+tol && highToLowWaveLen[*i+1] > clk*3/2-tol) {       //2 clocks and wave size is 1 1/2
				if (lowToLowWaveLen[*i+2] >= (clk*3)/2-tol && lowToLowWaveLen[*i+2] <= clk*2+tol && highToLowWaveLen[*i+2] > clk-tol) { //1 1/2 to 2 clocks and at least one full clock wave
					if (lowToLowWaveLen[*i+3] >= clk*1-tol && lowToLowWaveLen[*i+3] <= clk*2+tol) { //1 to 2 clocks for end of ST + first bit
						*stStopLoc = *i + 3;
						return true;
					}
				}
			}
		}
	}
	return false;
}
//by marshmellow
//attempt to identify a Sequence Terminator in ASK modulated raw wave
bool DetectST(uint8_t buffer[], size_t *size, int *foundclock, size_t *ststart, size_t *stend) {
	size_t bufsize = *size;
	//need to loop through all samples and identify our clock, look for the ST pattern
	int clk = 0; 
	int tol = 0;
	int j=0, high, low, skip=0, start=0, end=0, minClk=255;
	size_t i = 0;
	//probably should malloc... || test if memory is available ... handle device side? memory danger!!! [marshmellow]
	int tmpbuff[bufsize / LOWEST_DEFAULT_CLOCK]; // low to low wave count //guess rf/32 clock, if click is smaller we will only have room for a fraction of the samples captured
	int waveLen[bufsize / LOWEST_DEFAULT_CLOCK]; // high to low wave count //if clock is larger then we waste memory in array size that is not needed...
	//size_t testsize = (bufsize < 512) ? bufsize : 512;
	int phaseoff = 0;
	high = low = 128;
	memset(tmpbuff, 0, sizeof(tmpbuff));
	memset(waveLen, 0, sizeof(waveLen));

	if (!loadWaveCounters(buffer, bufsize, tmpbuff, waveLen, &j, &skip, &minClk, &high, &low)) return false;
	// set clock  - might be able to get this externally and remove this work...
	clk = getClosestClock(minClk);
	// clock not found - ERROR
	if (clk == 0) {
		if (g_debugMode==2) prnt("DEBUG STT: clock not found - quitting");
		return false;
	}
	*foundclock = clk;

	tol = clk/8;
	if (!findST(&start, &skip, tmpbuff, waveLen, clk, tol, j, &i)) {
		// first ST not found - ERROR
		if (g_debugMode==2) prnt("DEBUG STT: first STT not found - quitting");
		return false;
	} else {
		if (g_debugMode==2) prnt("DEBUG STT: first STT found at wave: %i, skip: %i, j=%i", start, skip, j);
	}
	if (waveLen[i+2] > clk*1+tol)
		phaseoff = 0;
	else
		phaseoff = clk/2;
	
	// skip over the remainder of ST
	skip += clk*7/2; //3.5 clocks from tmpbuff[i] = end of st - also aligns for ending point

	// now do it again to find the end
	int dummy1 = 0;
	end = skip;
	i+=3;
	if (!findST(&dummy1, &end, tmpbuff, waveLen, clk, tol, j, &i)) {
		//didn't find second ST - ERROR
		if (g_debugMode==2) prnt("DEBUG STT: second STT not found - quitting");
		return false;
	}
	end -= phaseoff;
	if (g_debugMode==2) prnt("DEBUG STT: start of data: %d end of data: %d, datalen: %d, clk: %d, bits: %d, phaseoff: %d", skip, end, end-skip, clk, (end-skip)/clk, phaseoff);
	//now begin to trim out ST so we can use normal demod cmds
	start = skip;
	size_t datalen = end - start;
	// check validity of datalen (should be even clock increments)  - use a tolerance of up to 1/8th a clock
	if ( clk - (datalen % clk) <= clk/8) {
		// padd the amount off - could be problematic...  but shouldn't happen often
		datalen += clk - (datalen % clk);
	} else if ( (datalen % clk) <= clk/8 ) {
		// padd the amount off - could be problematic...  but shouldn't happen often
		datalen -= datalen % clk;
	} else {
		if (g_debugMode==2) prnt("DEBUG STT: datalen not divisible by clk: %u %% %d = %d - quitting", datalen, clk, datalen % clk);
		return false;
	}
	// if datalen is less than one t55xx block - ERROR
	if (datalen/clk < 8*4) {
		if (g_debugMode==2) prnt("DEBUG STT: datalen is less than 1 full t55xx block - quitting");		
		return false;
	}
	size_t dataloc = start;
	if (buffer[dataloc-(clk*4)-(clk/4)] <= low && buffer[dataloc] <= low && buffer[dataloc-(clk*4)] >= high) {
		//we have low drift (and a low just before the ST and a low just after the ST) - compensate by backing up the start 
		for ( i=0; i <= (clk/4); ++i ) {
			if ( buffer[dataloc - (clk*4) - i] <= low ) {
				dataloc -= i;
				break;
			}
		}
	}
	
	size_t newloc = 0;
	i=0;
	if (g_debugMode==2) prnt("DEBUG STT: Starting STT trim - start: %d, datalen: %d ",dataloc, datalen);		
	bool firstrun = true;
	// warning - overwriting buffer given with raw wave data with ST removed...
	while ( dataloc < bufsize-(clk/2) ) {
		//compensate for long high at end of ST not being high due to signal loss... (and we cut out the start of wave high part)
		if (buffer[dataloc]<high && buffer[dataloc]>low && buffer[dataloc+clk/4]<high && buffer[dataloc+clk/4]>low) {
			for(i=0; i < clk/2-tol; ++i) {
				buffer[dataloc+i] = high+5;
			}
		} //test for small spike outlier (high between two lows) in the case of very strong waves
		if (buffer[dataloc] > low && buffer[dataloc+clk/4] <= low) {
			for(i=0; i < clk/4; ++i) {
				buffer[dataloc+i] = buffer[dataloc+clk/4];
			}
		}
		if (firstrun) {
			*stend = dataloc;
			*ststart = dataloc-(clk*4);
			firstrun=false;
		}
		for (i=0; i<datalen; ++i) {
			if (i+newloc < bufsize) {
				if (i+newloc < dataloc)
					buffer[i+newloc] = buffer[dataloc];

				dataloc++;
			}
		}
		newloc += i;
		//skip next ST  -  we just assume it will be there from now on...
		if (g_debugMode==2) prnt("DEBUG STT: skipping STT at %d to %d", dataloc, dataloc+(clk*4));
		dataloc += clk*4;
	}
	*size = newloc;
	return true;
}

//by marshmellow
//take 11 10 01 11 00 and make 01100 ... miller decoding 
//check for phase errors - should never have half a 1 or 0 by itself and should never exceed 1111 or 0000 in a row
//decodes miller encoded binary
//NOTE  askrawdemod will NOT demod miller encoded ask unless the clock is manually set to 1/2 what it is detected as!
int millerRawDecode(uint8_t *BitStream, size_t *size, int invert) {
	if (*size < 16) return -1;
	uint16_t MaxBits = 512, errCnt = 0;
	size_t i, bitCnt=0;
	uint8_t alignCnt = 0, curBit = BitStream[0], alignedIdx = 0;
	uint8_t halfClkErr = 0;
	//find alignment, needs 4 1s or 0s to properly align
	for (i=1; i < *size-1; i++) {
		alignCnt = (BitStream[i] == curBit) ? alignCnt+1 : 0;
		curBit = BitStream[i];
		if (alignCnt == 4) break;
	}
	// for now error if alignment not found.  later add option to run it with multiple offsets...
	if (alignCnt != 4) {
		if (g_debugMode) prnt("ERROR MillerDecode: alignment not found so either your bitstream is not miller or your data does not have a 101 in it");
		return -1;
	}
	alignedIdx = (i-1) % 2;
	for (i=alignedIdx; i < *size-3; i+=2) {
		halfClkErr = (uint8_t)((halfClkErr << 1 | BitStream[i]) & 0xFF);
		if ( (halfClkErr & 0x7) == 5 || (halfClkErr & 0x7) == 2 || (i > 2 && (halfClkErr & 0x7) == 0) || (halfClkErr & 0x1F) == 0x1F) {
			errCnt++;
			BitStream[bitCnt++] = 7;
			continue;
		}
		BitStream[bitCnt++] = BitStream[i] ^ BitStream[i+1] ^ invert;

		if (bitCnt > MaxBits) break;
	}
	*size = bitCnt;
	return errCnt;
}

//by marshmellow
//take 01 or 10 = 1 and 11 or 00 = 0
//check for phase errors - should never have 111 or 000 should be 01001011 or 10110100 for 1010
//decodes biphase or if inverted it is AKA conditional dephase encoding AKA differential manchester encoding
int BiphaseRawDecode(uint8_t *BitStream, size_t *size, int *offset, int invert) {
	uint16_t bitnum = 0;
	uint16_t errCnt = 0;
	size_t i = *offset;
	uint16_t MaxBits=512;
	//if not enough samples - error
	if (*size < 51) return -1;
	//check for phase change faults - skip one sample if faulty
	uint8_t offsetA = 1, offsetB = 1;
	for (; i<48; i+=2){
		if (BitStream[i+1]==BitStream[i+2]) offsetA=0; 
		if (BitStream[i+2]==BitStream[i+3]) offsetB=0;					
	}
	if (!offsetA && offsetB) *offset+=1;
	for (i=*offset; i<*size-3; i+=2){
		//check for phase error
		if (BitStream[i+1]==BitStream[i+2]) {
			BitStream[bitnum++]=7;
			errCnt++;
		}
		if((BitStream[i]==1 && BitStream[i+1]==0) || (BitStream[i]==0 && BitStream[i+1]==1)){
			BitStream[bitnum++]=1^invert;
		} else if((BitStream[i]==0 && BitStream[i+1]==0) || (BitStream[i]==1 && BitStream[i+1]==1)){
			BitStream[bitnum++]=invert;
		} else {
			BitStream[bitnum++]=7;
			errCnt++;
		}
		if(bitnum>MaxBits) break;
	}
	*size=bitnum;
	return errCnt;
}

//by marshmellow
//take 10 and 01 and manchester decode
//run through 2 times and take least errCnt
int manrawdecode(uint8_t * BitStream, size_t *size, uint8_t invert, uint8_t *alignPos) {
	uint16_t bitnum=0, MaxBits = 512, errCnt = 0;
	size_t i, ii;
	uint16_t bestErr = 1000, bestRun = 0;
	if (*size < 16) return -1;
	//find correct start position [alignment]
	for (ii=0;ii<2;++ii){
		for (i=ii; i<*size-3; i+=2)
			if (BitStream[i]==BitStream[i+1])
				errCnt++;

		if (bestErr>errCnt){
			bestErr=errCnt;
			bestRun=ii;
		}
		errCnt=0;
	}
	*alignPos=bestRun;
	//decode
	for (i=bestRun; i < *size-3; i+=2){
		if(BitStream[i] == 1 && (BitStream[i+1] == 0)){
			BitStream[bitnum++]=invert;
		} else if((BitStream[i] == 0) && BitStream[i+1] == 1){
			BitStream[bitnum++]=invert^1;
		} else {
			BitStream[bitnum++]=7;
		}
		if(bitnum>MaxBits) break;
	}
	*size=bitnum;
	return bestErr;
}

//by marshmellow
//demodulates strong heavily clipped samples
int cleanAskRawDemod(uint8_t *BinStream, size_t *size, int clk, int invert, int high, int low, int *startIdx)
{
	*startIdx=0;
	size_t bitCnt=0, smplCnt=1, errCnt=0;
	bool waveHigh = (BinStream[0] >= high);
	for (size_t i=1; i < *size; i++){
		if (BinStream[i] >= high && waveHigh){
			smplCnt++;
		} else if (BinStream[i] <= low && !waveHigh){
			smplCnt++;
		} else { //transition
			if ((BinStream[i] >= high && !waveHigh) || (BinStream[i] <= low && waveHigh)){
				if (smplCnt > clk-(clk/4)-1) { //full clock
					if (smplCnt > clk + (clk/4)+1) { //too many samples
						errCnt++;
						if (g_debugMode==2) prnt("DEBUG ASK: Modulation Error at: %u", i);
						BinStream[bitCnt++] = 7;
					} else if (waveHigh) {
						BinStream[bitCnt++] = invert;
						BinStream[bitCnt++] = invert;
					} else if (!waveHigh) {
						BinStream[bitCnt++] = invert ^ 1;
						BinStream[bitCnt++] = invert ^ 1;
					}
					if (*startIdx==0) *startIdx = i-clk;
					waveHigh = !waveHigh;  
					smplCnt = 0;
				} else if (smplCnt > (clk/2) - (clk/4)-1) { //half clock
					if (waveHigh) {
						BinStream[bitCnt++] = invert;
					} else if (!waveHigh) {
						BinStream[bitCnt++] = invert ^ 1;
					}
					if (*startIdx==0) *startIdx = i-(clk/2);
					waveHigh = !waveHigh;  
					smplCnt = 0;
				} else {
					smplCnt++;
					//transition bit oops
				}
			} else { //haven't hit new high or new low yet
				smplCnt++;
			}
		}
	}
	*size = bitCnt;
	return errCnt;
}

//by marshmellow
//attempts to demodulate ask modulations, askType == 0 for ask/raw, askType==1 for ask/manchester
int askdemod_ext(uint8_t *BinStream, size_t *size, int *clk, int *invert, int maxErr, uint8_t amp, uint8_t askType, int *startIdx) {
	if (*size==0) return -1;
	int start = DetectASKClock(BinStream, *size, clk, maxErr); //clock default
	if (*clk==0 || start < 0) return -3;
	if (*invert != 1) *invert = 0;
	if (amp==1) askAmp(BinStream, *size);
	if (g_debugMode==2) prnt("DEBUG ASK: clk %d, beststart %d, amp %d", *clk, start, amp);

	//start pos from detect ask clock is 1/2 clock offset
	// NOTE: can be negative (demod assumes rest of wave was there)
	*startIdx = start - (*clk/2); 
	uint8_t initLoopMax = 255;
	if (initLoopMax > *size) initLoopMax = *size;
	// Detect high and lows
	//25% clip in case highs and lows aren't clipped [marshmellow]
	int high, low;
	if (getHiLo(BinStream, initLoopMax, &high, &low, 75, 75) < 1) 
		return -2; //just noise

	size_t errCnt = 0;
	// if clean clipped waves detected run alternate demod
	if (DetectCleanAskWave(BinStream, *size, high, low)) {
		if (g_debugMode==2) prnt("DEBUG ASK: Clean Wave Detected - using clean wave demod");
		errCnt = cleanAskRawDemod(BinStream, size, *clk, *invert, high, low, startIdx);
		if (askType) { //askman
			uint8_t alignPos = 0;
			errCnt = manrawdecode(BinStream, size, 0, &alignPos);
			*startIdx += *clk/2 * alignPos;
			if (g_debugMode) prnt("DEBUG ASK CLEAN: startIdx %i, alignPos %u", *startIdx, alignPos);
			return errCnt;
		} else { //askraw
			return errCnt;
		}
	}
	if (g_debugMode) prnt("DEBUG ASK WEAK: startIdx %i", *startIdx);
	if (g_debugMode==2) prnt("DEBUG ASK: Weak Wave Detected - using weak wave demod");

	int lastBit;              //set first clock check - can go negative
	size_t i, bitnum = 0;     //output counter
	uint8_t midBit = 0;
	uint8_t tol = 0;          //clock tolerance adjust - waves will be accepted as within the clock if they fall + or - this value + clock from last valid wave
	if (*clk <= 32) tol = 1;  //clock tolerance may not be needed anymore currently set to + or - 1 but could be increased for poor waves or removed entirely
	size_t MaxBits = 3072;    //max bits to collect
	lastBit = start - *clk;

	for (i = start; i < *size; ++i) {
		if (i-lastBit >= *clk-tol){
			if (BinStream[i] >= high) {
				BinStream[bitnum++] = *invert;
			} else if (BinStream[i] <= low) {
				BinStream[bitnum++] = *invert ^ 1;
			} else if (i-lastBit >= *clk+tol) {
				if (bitnum > 0) {
					if (g_debugMode==2) prnt("DEBUG ASK: Modulation Error at: %u", i);
					BinStream[bitnum++]=7;
					errCnt++;						
				} 
			} else { //in tolerance - looking for peak
				continue;
			}
			midBit = 0;
			lastBit += *clk;
		} else if (i-lastBit >= (*clk/2-tol) && !midBit && !askType){
			if (BinStream[i] >= high) {
				BinStream[bitnum++] = *invert;
			} else if (BinStream[i] <= low) {
				BinStream[bitnum++] = *invert ^ 1;
			} else if (i-lastBit >= *clk/2+tol) {
				BinStream[bitnum] = BinStream[bitnum-1];
				bitnum++;
			} else { //in tolerance - looking for peak
				continue;
			}
			midBit = 1;
		}
		if (bitnum >= MaxBits) break;
	}
	*size = bitnum;
	return errCnt;
}

int askdemod(uint8_t *BinStream, size_t *size, int *clk, int *invert, int maxErr, uint8_t amp, uint8_t askType) {
	int start = 0;
	return askdemod_ext(BinStream, size, clk, invert, maxErr, amp, askType, &start);
}

// by marshmellow - demodulate NRZ wave - requires a read with strong signal
// peaks invert bit (high=1 low=0) each clock cycle = 1 bit determined by last peak
int nrzRawDemod(uint8_t *dest, size_t *size, int *clk, int *invert, int *startIdx) {
	if (justNoise(dest, *size)) return -1;
	size_t clkStartIdx = 0;
	*clk = DetectNRZClock(dest, *size, *clk, &clkStartIdx);
	if (*clk==0) return -2;
	size_t i, gLen = 4096;
	if (gLen>*size) gLen = *size-20;
	int high, low;
	if (getHiLo(dest, gLen, &high, &low, 75, 75) < 1) return -3; //25% fuzz on high 25% fuzz on low
	
	uint8_t bit=0;
	//convert wave samples to 1's and 0's
	for(i=20; i < *size-20; i++){
		if (dest[i] >= high) bit = 1;
		if (dest[i] <= low)  bit = 0;
		dest[i] = bit;
	}
	//now demod based on clock (rf/32 = 32 1's for one 1 bit, 32 0's for one 0 bit) 
	size_t lastBit = 0;
	size_t numBits = 0;
	for(i=21; i < *size-20; i++) {
		//if transition detected or large number of same bits - store the passed bits
		if (dest[i] != dest[i-1] || (i-lastBit) == (10 * *clk)) {
			memset(dest+numBits, dest[i-1] ^ *invert, (i - lastBit + (*clk/4)) / *clk);
			numBits += (i - lastBit + (*clk/4)) / *clk;
			if (lastBit == 0) {
				*startIdx = i - (numBits * *clk);
				if (g_debugMode==2) prnt("DEBUG NRZ: startIdx %i", *startIdx);
			}
			lastBit = i-1;
		}
	}
	*size = numBits;
	return 0;
}

//translate wave to 11111100000 (1 for each short wave [higher freq] 0 for each long wave [lower freq])
size_t fsk_wave_demod(uint8_t * dest, size_t size, uint8_t fchigh, uint8_t fclow, int *startIdx) {
	size_t last_transition = 0;
	size_t idx = 1;
	if (fchigh==0) fchigh=10;
	if (fclow==0) fclow=8;
	//set the threshold close to 0 (graph) or 128 std to avoid static
	size_t preLastSample = 0;
	size_t LastSample = 0;
	size_t currSample = 0;
	if ( size < 1024 ) return 0; // not enough samples

	//find start of modulating data in trace 
	idx = findModStart(dest, size, fchigh);
	// Need to threshold first sample
	if(dest[idx] < FSK_PSK_THRESHOLD) dest[0] = 0;
	else dest[0] = 1;
	
	last_transition = idx;
	idx++;
	size_t numBits = 0;
	// count cycles between consecutive lo-hi transitions, there should be either 8 (fc/8)
	// or 10 (fc/10) cycles but in practice due to noise etc we may end up with anywhere
	// between 7 to 11 cycles so fuzz it by treat anything <9 as 8 and anything else as 10
	//  (could also be fc/5 && fc/7 for fsk1 = 4-9)
	for(; idx < size; idx++) {
		// threshold current value
		if (dest[idx] < FSK_PSK_THRESHOLD) dest[idx] = 0;
		else dest[idx] = 1;

		// Check for 0->1 transition
		if (dest[idx-1] < dest[idx]) {
			preLastSample = LastSample;
			LastSample = currSample;
			currSample = idx-last_transition;
			if (currSample < (fclow-2)) {                   //0-5 = garbage noise (or 0-3)
				//do nothing with extra garbage
			} else if (currSample < (fchigh-1)) {           //6-8 = 8 sample waves  (or 3-6 = 5)
				//correct previous 9 wave surrounded by 8 waves (or 6 surrounded by 5)
				if (numBits > 1 && LastSample > (fchigh-2) && (preLastSample < (fchigh-1))){
					dest[numBits-1]=1;
				}
				dest[numBits++]=1;
			if (numBits > 0 && *startIdx==0) *startIdx = idx - fclow;
			} else if (currSample > (fchigh+1) && numBits < 3) { //12 + and first two bit = unusable garbage
				//do nothing with beginning garbage and reset..  should be rare..
				numBits = 0; 
			} else if (currSample == (fclow+1) && LastSample == (fclow-1)) { // had a 7 then a 9 should be two 8's (or 4 then a 6 should be two 5's)
				dest[numBits++]=1;
			if (numBits > 0 && *startIdx==0) *startIdx = idx - fclow;
			} else {                                        //9+ = 10 sample waves (or 6+ = 7)
				dest[numBits++]=0;
			if (numBits > 0 && *startIdx==0) *startIdx = idx - fchigh;
			}
			last_transition = idx;
		}
	}
	return numBits; //Actually, it returns the number of bytes, but each byte represents a bit: 1 or 0
}

//translate 11111100000 to 10
//rfLen = clock, fchigh = larger field clock, fclow = smaller field clock
size_t aggregate_bits(uint8_t *dest, size_t size, uint8_t rfLen, uint8_t invert, uint8_t fchigh, uint8_t fclow, int *startIdx) {
	uint8_t lastval=dest[0];
	size_t idx=0;
	size_t numBits=0;
	uint32_t n=1;
	for( idx=1; idx < size; idx++) {
		n++;
		if (dest[idx]==lastval) continue; //skip until we hit a transition
		
		//find out how many bits (n) we collected (use 1/2 clk tolerance)
		//if lastval was 1, we have a 1->0 crossing
		if (dest[idx-1]==1) {
			n = (n * fclow + rfLen/2) / rfLen;
		} else {// 0->1 crossing 
			n = (n * fchigh + rfLen/2) / rfLen; 
		}
		if (n == 0) n = 1;
		
		//first transition - save startidx
		if (numBits == 0) {
			if (lastval == 1) {  //high to low
				*startIdx += (fclow * idx) - (n*rfLen);
				if (g_debugMode==2) prnt("DEBUG FSK: startIdx %i, fclow*idx %i, n*rflen %u", *startIdx, fclow*(idx), n*rfLen);
			} else {
				*startIdx += (fchigh * idx) - (n*rfLen);
				if (g_debugMode==2) prnt("DEBUG FSK: startIdx %i, fchigh*idx %i, n*rflen %u", *startIdx, fchigh*(idx), n*rfLen);
			}
		}

		//add to our destination the bits we collected		
		memset(dest+numBits, dest[idx-1]^invert , n);
		numBits += n;
		n=0;
		lastval=dest[idx];
	}//end for
	// if valid extra bits at the end were all the same frequency - add them in
	if (n > rfLen/fchigh) {
		if (dest[idx-2]==1) {
			n = (n * fclow + rfLen/2) / rfLen;
		} else {
			n = (n * fchigh + rfLen/2) / rfLen;
		}
		memset(dest+numBits, dest[idx-1]^invert , n);
		numBits += n;
	}
	return numBits;
}

//by marshmellow  (from holiman's base)
// full fsk demod from GraphBuffer wave to decoded 1s and 0s (no mandemod)
int fskdemod(uint8_t *dest, size_t size, uint8_t rfLen, uint8_t invert, uint8_t fchigh, uint8_t fclow, int *startIdx) {
	if (justNoise(dest, size)) return 0;
	// FSK demodulator
	size = fsk_wave_demod(dest, size, fchigh, fclow, startIdx);
	size = aggregate_bits(dest, size, rfLen, invert, fchigh, fclow, startIdx);
	return size;
}

// by marshmellow
// convert psk1 demod to psk2 demod
// only transition waves are 1s
void psk1TOpsk2(uint8_t *BitStream, size_t size) {
	size_t i=1;
	uint8_t lastBit=BitStream[0];
	for (; i<size; i++){
		if (BitStream[i]==7){
			//ignore errors
		} else if (lastBit!=BitStream[i]){
			lastBit=BitStream[i];
			BitStream[i]=1;
		} else {
			BitStream[i]=0;
		}
	}
	return;
}

// by marshmellow
// convert psk2 demod to psk1 demod
// from only transition waves are 1s to phase shifts change bit
void psk2TOpsk1(uint8_t *BitStream, size_t size) {
	uint8_t phase=0;
	for (size_t i=0; i<size; i++){
		if (BitStream[i]==1){
			phase ^=1;
		}
		BitStream[i]=phase;
	}
	return;
}

//by marshmellow - demodulate PSK1 wave 
//uses wave lengths (# Samples) 
int pskRawDemod_ext(uint8_t dest[], size_t *size, int *clock, int *invert, int *startIdx) {
	if (*size < 170) return -1;

	uint8_t curPhase = *invert;
	uint8_t fc=0;
	size_t i=0, numBits=0, waveStart=1, waveEnd=0, firstFullWave=0, lastClkBit=0;
	uint16_t fullWaveLen=0, waveLenCnt=0, avgWaveVal;
	uint16_t errCnt=0, errCnt2=0;
	
	*clock = DetectPSKClock(dest, *size, *clock, &firstFullWave, &curPhase, &fc);
	if (*clock <= 0) return -1;
	//if clock detect found firstfullwave...
	uint16_t tol = fc/2;
	if (firstFullWave == 0) {
		//find start of modulating data in trace 
		i = findModStart(dest, *size, fc);
		//find first phase shift
		firstFullWave = pskFindFirstPhaseShift(dest, *size, &curPhase, i, fc, &fullWaveLen);
		if (firstFullWave == 0) {
			// no phase shift detected - could be all 1's or 0's - doesn't matter where we start
			// so skip a little to ensure we are past any Start Signal
			firstFullWave = 160;
			memset(dest, curPhase, firstFullWave / *clock);
		} else {
			memset(dest, curPhase^1, firstFullWave / *clock);
		}
	} else {
		memset(dest, curPhase^1, firstFullWave / *clock);
	}
	//advance bits
	numBits += (firstFullWave / *clock);
	*startIdx = firstFullWave - (*clock * numBits)+2;
	//set start of wave as clock align
	lastClkBit = firstFullWave;
	if (g_debugMode==2) prnt("DEBUG PSK: firstFullWave: %u, waveLen: %u, startIdx %i",firstFullWave,fullWaveLen, *startIdx);
	if (g_debugMode==2) prnt("DEBUG PSK: clk: %d, lastClkBit: %u, fc: %u", *clock, lastClkBit,(unsigned int) fc);
	waveStart = 0;
	dest[numBits++] = curPhase; //set first read bit
	for (i = firstFullWave + fullWaveLen - 1; i < *size-3; i++) {
		//top edge of wave = start of new wave 
		if (dest[i]+fc < dest[i+1] && dest[i+1] >= dest[i+2]) {
			if (waveStart == 0) {
				waveStart = i+1;
				waveLenCnt = 0;
				avgWaveVal = dest[i+1];
			} else { //waveEnd
				waveEnd = i+1;
				waveLenCnt = waveEnd-waveStart;
				if (waveLenCnt > fc) {
					//this wave is a phase shift
					//PrintAndLog("DEBUG: phase shift at: %d, len: %d, nextClk: %d, i: %d, fc: %d",waveStart,waveLenCnt,lastClkBit+*clock-tol,i+1,fc);
					if (i+1 >= lastClkBit + *clock - tol) { //should be a clock bit
						curPhase ^= 1;
						dest[numBits++] = curPhase;
						lastClkBit += *clock;
					} else if (i < lastClkBit+10+fc) {
						//noise after a phase shift - ignore
					} else { //phase shift before supposed to based on clock
						errCnt++;
						dest[numBits++] = 7;
					}
				} else if (i+1 > lastClkBit + *clock + tol + fc) {
					lastClkBit += *clock; //no phase shift but clock bit
					dest[numBits++] = curPhase;
				} else if (waveLenCnt < fc - 1) { //wave is smaller than field clock (shouldn't happen often)
					errCnt2++;
					if(errCnt2 > 101) return errCnt2;
					avgWaveVal += dest[i+1];
					continue;
				}
				avgWaveVal = 0;
				waveStart = i+1;
			}
		}
		avgWaveVal += dest[i+1];
	}
	*size = numBits;
	return errCnt;
}

int pskRawDemod(uint8_t dest[], size_t *size, int *clock, int *invert) {
	int startIdx = 0;
	return pskRawDemod_ext(dest, size, clock, invert, &startIdx);
}

//**********************************************************************************************
//-----------------Tag format detection section-------------------------------------------------
//**********************************************************************************************

// by marshmellow
// FSK Demod then try to locate an AWID ID
int AWIDdemodFSK(uint8_t *dest, size_t *size, int *waveStartIdx) {
	//make sure buffer has enough data
	if (*size < 96*50) return -1;

	// FSK demodulator
	*size = fskdemod(dest, *size, 50, 1, 10, 8, waveStartIdx);  // fsk2a RF/50 
	if (*size < 96) return -3;  //did we get a good demod?

	uint8_t preamble[] = {0,0,0,0,0,0,0,1};
	size_t startIdx = 0;
	uint8_t errChk = preambleSearch(dest, preamble, sizeof(preamble), size, &startIdx);
	if (errChk == 0) return -4; //preamble not found
	if (*size != 96) return -5;
	return (int)startIdx;
}

//by marshmellow
//takes 1s and 0s and searches for EM410x format - output EM ID
uint8_t Em410xDecode(uint8_t *BitStream, size_t *size, size_t *startIdx, uint32_t *hi, uint64_t *lo)
{
	//sanity checks
	if (*size < 64) return 0;
	if (BitStream[1]>1) return 0;  //allow only 1s and 0s

	// 111111111 bit pattern represent start of frame
	//  include 0 in front to help get start pos
	uint8_t preamble[] = {0,1,1,1,1,1,1,1,1,1};
	uint8_t errChk = 0;
	uint8_t FmtLen = 10; // sets of 4 bits = end data 
	*startIdx = 0;
	errChk = preambleSearch(BitStream, preamble, sizeof(preamble), size, startIdx);
	if ( errChk == 0 || (*size != 64 && *size != 128) ) return 0;
	if (*size == 128) FmtLen = 22; // 22 sets of 4 bits

	//skip last 4bit parity row for simplicity
	*size = removeParity(BitStream, *startIdx + sizeof(preamble), 5, 0, FmtLen * 5);
	if (*size == 40) { // std em410x format
		*hi = 0;
		*lo = ((uint64_t)(bytebits_to_byte(BitStream, 8)) << 32) | (bytebits_to_byte(BitStream + 8, 32));
	} else if (*size == 88) { // long em format
		*hi = (bytebits_to_byte(BitStream, 24)); 
		*lo = ((uint64_t)(bytebits_to_byte(BitStream + 24, 32)) << 32) | (bytebits_to_byte(BitStream + 24 + 32, 32));
	} else {
		if (g_debugMode) prnt("Error removing parity: %u", *size);
		return 0;
	}
	return 1;
}

// Ask/Biphase Demod then try to locate an ISO 11784/85 ID
// BitStream must contain previously askrawdemod and biphasedemoded data
int FDXBdemodBI(uint8_t *dest, size_t *size) {
	//make sure buffer has enough data
	if (*size < 128) return -1;

	size_t startIdx = 0;
	uint8_t preamble[] = {0,0,0,0,0,0,0,0,0,0,1};

	uint8_t errChk = preambleSearch(dest, preamble, sizeof(preamble), size, &startIdx);
	if (errChk == 0) return -2; //preamble not found
	if (*size != 128) return -3; //wrong size for fdxb
	//return start position
	return (int)startIdx;
}

// by marshmellow
// demod gProxIIDemod 
// error returns as -x 
// success returns start position in BitStream
// BitStream must contain previously askrawdemod and biphasedemoded data
int gProxII_Demod(uint8_t BitStream[], size_t *size) {
	size_t startIdx=0;
	uint8_t preamble[] = {1,1,1,1,1,0};

	uint8_t errChk = preambleSearch(BitStream, preamble, sizeof(preamble), size, &startIdx);
	if (errChk == 0) return -3; //preamble not found
	if (*size != 96) return -2; //should have found 96 bits
	//check first 6 spacer bits to verify format
	if (!BitStream[startIdx+5] && !BitStream[startIdx+10] && !BitStream[startIdx+15] && !BitStream[startIdx+20] && !BitStream[startIdx+25] && !BitStream[startIdx+30]){
		//confirmed proper separator bits found
		//return start position
		return (int) startIdx;
	}
	return -5; //spacer bits not found - not a valid gproxII
}

// loop to get raw HID waveform then FSK demodulate the TAG ID from it
int HIDdemodFSK(uint8_t *dest, size_t *size, uint32_t *hi2, uint32_t *hi, uint32_t *lo, int *waveStartIdx) {
	size_t numStart=0, size2=*size, startIdx=0; 
	// FSK demodulator  fsk2a so invert and fc/10/8
	*size = fskdemod(dest, size2, 50, 1, 10, 8, waveStartIdx);
	if (*size < 96*2) return -2;
	// 00011101 bit pattern represent start of frame, 01 pattern represents a 0 and 10 represents a 1
	uint8_t preamble[] = {0,0,0,1,1,1,0,1};
	// find bitstring in array  
	uint8_t errChk = preambleSearch(dest, preamble, sizeof(preamble), size, &startIdx);
	if (errChk == 0) return -3; //preamble not found

	numStart = startIdx + sizeof(preamble);
	// final loop, go over previously decoded FSK data and manchester decode into usable tag ID
	for (size_t idx = numStart; (idx-numStart) < *size - sizeof(preamble); idx+=2){
		if (dest[idx] == dest[idx+1]){
			return -4; //not manchester data
		}
		*hi2 = (*hi2<<1)|(*hi>>31);
		*hi = (*hi<<1)|(*lo>>31);
		//Then, shift in a 0 or one into low
		if (dest[idx] && !dest[idx+1])  // 1 0
			*lo=(*lo<<1)|1;
		else // 0 1
			*lo=(*lo<<1)|0;
	}
	return (int)startIdx;
}

int IOdemodFSK(uint8_t *dest, size_t size, int *waveStartIdx) {
	//make sure buffer has data
	if (size < 66*64) return -2;
	// FSK demodulator  RF/64, fsk2a so invert, and fc/10/8
	size = fskdemod(dest, size, 64, 1, 10, 8, waveStartIdx); 
	if (size < 65) return -3;  //did we get a good demod?
	//Index map
	//0           10          20          30          40          50          60
	//|           |           |           |           |           |           |
	//01234567 8 90123456 7 89012345 6 78901234 5 67890123 4 56789012 3 45678901 23
	//-----------------------------------------------------------------------------
	//00000000 0 11110000 1 facility 1 version* 1 code*one 1 code*two 1 ???????? 11
	//
	//XSF(version)facility:codeone+codetwo
	//Handle the data
	size_t startIdx = 0;
	uint8_t preamble[] = {0,0,0,0,0,0,0,0,0,1};
	uint8_t errChk = preambleSearch(dest, preamble, sizeof(preamble), &size, &startIdx);
	if (errChk == 0) return -4; //preamble not found

	if (!dest[startIdx+8] && dest[startIdx+17]==1 && dest[startIdx+26]==1 && dest[startIdx+35]==1 && dest[startIdx+44]==1 && dest[startIdx+53]==1){
		//confirmed proper separator bits found
		//return start position
		return (int) startIdx;
	}
	return -5;
} 

// redesigned by marshmellow adjusted from existing decode functions
// indala id decoding
int indala64decode(uint8_t *bitStream, size_t *size, uint8_t *invert) {
	//standard 64 bit indala formats including 26 bit 40134 format
	uint8_t preamble64[] = {1,0,1,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 1};
	uint8_t preamble64_i[] = {0,1,0,1, 1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1, 0};
	size_t startidx = 0;
	size_t found_size = *size;
	bool found = preambleSearch(bitStream, preamble64, sizeof(preamble64), &found_size, &startidx);
	if (!found) {
		found = preambleSearch(bitStream, preamble64_i, sizeof(preamble64_i), &found_size, &startidx);
		if (!found) return -1;
		*invert ^= 1;
	}
	if (found_size != 64) return -2;
	if (*invert==1)
		for (size_t i = startidx; i < found_size + startidx; i++) 
			bitStream[i] ^= 1;

	// note: don't change *size until we are sure we got it... 
	*size = found_size;
	return (int) startidx;
}

int indala224decode(uint8_t *bitStream, size_t *size, uint8_t *invert) {
	//large 224 bit indala formats (different preamble too...)
	uint8_t preamble224[] = {1,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,1};
	uint8_t preamble224_i[] = {0,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,0};
	size_t startidx = 0;
	size_t found_size = *size;
	bool found = preambleSearch(bitStream, preamble224, sizeof(preamble224), &found_size, &startidx);
	if (!found) {
		found = preambleSearch(bitStream, preamble224_i, sizeof(preamble224_i), &found_size, &startidx);
		if (!found) return -1;
		*invert ^= 1;
	}
	if (found_size != 224) return -2;
	if (*invert==1 && startidx > 0)
		for (size_t i = startidx-1; i < found_size + startidx + 2; i++) 
			bitStream[i] ^= 1;

	// 224 formats are typically PSK2 (afaik 2017 Marshmellow)
	// note loses 1 bit at beginning of transformation...
	// don't need to verify array is big enough as to get here there has to be a full preamble after all of our data
	psk1TOpsk2(bitStream + (startidx-1), found_size+2);
	startidx++;

	*size = found_size;
	return (int) startidx;
}

// loop to get raw paradox waveform then FSK demodulate the TAG ID from it
int ParadoxdemodFSK(uint8_t *dest, size_t *size, uint32_t *hi2, uint32_t *hi, uint32_t *lo, int *waveStartIdx) {
	size_t numStart=0, size2=*size, startIdx=0;
	// FSK demodulator
	*size = fskdemod(dest, size2,50,1,10,8,waveStartIdx); //fsk2a
	if (*size < 96) return -2;

	// 00001111 bit pattern represent start of frame, 01 pattern represents a 0 and 10 represents a 1
	uint8_t preamble[] = {0,0,0,0,1,1,1,1};

	uint8_t errChk = preambleSearch(dest, preamble, sizeof(preamble), size, &startIdx);
	if (errChk == 0) return -3; //preamble not found

	numStart = startIdx + sizeof(preamble);
	// final loop, go over previously decoded FSK data and manchester decode into usable tag ID
	for (size_t idx = numStart; (idx-numStart) < *size - sizeof(preamble); idx+=2){
		if (dest[idx] == dest[idx+1]) 
			return -4; //not manchester data
		*hi2 = (*hi2<<1)|(*hi>>31);
		*hi = (*hi<<1)|(*lo>>31);
		//Then, shift in a 0 or one into low
		if (dest[idx] && !dest[idx+1])	// 1 0
			*lo=(*lo<<1)|1;
		else // 0 1
			*lo=(*lo<<1)|0;
	}
	return (int)startIdx;
}

// find presco preamble 0x10D in already demoded data
int PrescoDemod(uint8_t *dest, size_t *size) {
	//make sure buffer has data
	if (*size < 64*2) return -2;

	size_t startIdx = 0;
	uint8_t preamble[] = {1,0,0,0,0,1,1,0,1,0,0,0,0,0,0,0,0,0,0,0};
	uint8_t errChk = preambleSearch(dest, preamble, sizeof(preamble), size, &startIdx);
	if (errChk == 0) return -4; //preamble not found
	//return start position
	return (int) startIdx;
}

// by marshmellow
// FSK Demod then try to locate a Farpointe Data (pyramid) ID
int PyramiddemodFSK(uint8_t *dest, size_t *size, int *waveStartIdx) {
	//make sure buffer has data
	if (*size < 128*50) return -5;

	// FSK demodulator
	*size = fskdemod(dest, *size, 50, 1, 10, 8, waveStartIdx);  // fsk2a RF/50 
	if (*size < 128) return -2;  //did we get a good demod?

	uint8_t preamble[] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
	size_t startIdx = 0;
	uint8_t errChk = preambleSearch(dest, preamble, sizeof(preamble), size, &startIdx);
	if (errChk == 0) return -4; //preamble not found
	if (*size != 128) return -3;
	return (int)startIdx;
}

// by marshmellow
// find viking preamble 0xF200 in already demoded data
int VikingDemod_AM(uint8_t *dest, size_t *size) {
	//make sure buffer has data
	if (*size < 64*2) return -2;

	size_t startIdx = 0;
	uint8_t preamble[] = {1,1,1,1,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
	uint8_t errChk = preambleSearch(dest, preamble, sizeof(preamble), size, &startIdx);
	if (errChk == 0) return -4; //preamble not found
	uint32_t checkCalc = bytebits_to_byte(dest+startIdx,8) ^ bytebits_to_byte(dest+startIdx+8,8) ^ bytebits_to_byte(dest+startIdx+16,8)
	    ^ bytebits_to_byte(dest+startIdx+24,8) ^ bytebits_to_byte(dest+startIdx+32,8) ^ bytebits_to_byte(dest+startIdx+40,8) 
	    ^ bytebits_to_byte(dest+startIdx+48,8) ^ bytebits_to_byte(dest+startIdx+56,8);
	if ( checkCalc != 0xA8 ) return -5;
	if (*size != 64) return -6;
	//return start position
	return (int) startIdx;
}

// by iceman
// find Visa2000 preamble in already demoded data
int Visa2kDemod_AM(uint8_t *dest, size_t *size) {
	if (*size < 96) return -1; //make sure buffer has data
	size_t startIdx = 0;
	uint8_t preamble[] = {0,1,0,1,0,1,1,0,0,1,0,0,1,0,0,1,0,1,0,1,0,0,1,1,0,0,1,1,0,0,1,0};
	if (preambleSearch(dest, preamble, sizeof(preamble), size, &startIdx) == 0)
		return -2; //preamble not found
	if (*size != 96) return -3; //wrong demoded size
	//return start position
	return (int)startIdx;
}
