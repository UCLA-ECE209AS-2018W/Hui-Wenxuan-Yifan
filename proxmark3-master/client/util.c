//-----------------------------------------------------------------------------
// Copyright (C) 2010 iZsh <izsh at fail0verflow.com>
//
// This code is licensed to you under the terms of the GNU GPL, version 2 or,
// at your option, any later version. See the LICENSE.txt file for the text of
// the license.
//-----------------------------------------------------------------------------
// utilities
//-----------------------------------------------------------------------------

#include "util.h"

#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include "data.h"

#ifdef _WIN32
#include <windows.h>
#endif

#define MAX_BIN_BREAK_LENGTH   (3072+384+1)

#ifndef _WIN32
#include <termios.h>
#include <sys/ioctl.h> 
#include <unistd.h>

int ukbhit(void)
{
  int cnt = 0;
  int error;
  static struct termios Otty, Ntty;

  if ( tcgetattr(STDIN_FILENO, &Otty) == -1 ) return -1;
  Ntty = Otty;

  Ntty.c_iflag          = 0x0000;   // input mode
  Ntty.c_oflag          = 0x0000;   // output mode
  Ntty.c_lflag         &= ~ICANON;  // control mode = raw
  Ntty.c_cc[VMIN]       = 1;        // return if at least 1 character is in the queue
  Ntty.c_cc[VTIME]      = 0;   	    // no timeout. Wait forever
  
  if (0 == (error = tcsetattr(STDIN_FILENO, TCSANOW, &Ntty))) {   // set new attributes
    error += ioctl(STDIN_FILENO, FIONREAD, &cnt);                 // get number of characters availabe
    error += tcsetattr(STDIN_FILENO, TCSANOW, &Otty);             // reset attributes
  }

  return ( error == 0 ? cnt : -1 );
}

#else

#include <conio.h>
int ukbhit(void) {
	return kbhit();
}
#endif

// log files functions
void AddLogLine(char *file, char *extData, char *c) {
	FILE *fLog = NULL;
    char filename[FILE_PATH_SIZE] = {0x00};
    int len = 0;

    len = strlen(file);
    if (len > FILE_PATH_SIZE) len = FILE_PATH_SIZE;
    memcpy(filename, file, len);
   
	fLog = fopen(filename, "a");
	if (!fLog) {
		printf("Could not append log file %s", filename);
		return;
	}

	fprintf(fLog, "%s", extData);
	fprintf(fLog, "%s\n", c);
	fclose(fLog);
}

void AddLogHex(char *fileName, char *extData, const uint8_t * data, const size_t len){
	AddLogLine(fileName, extData, sprint_hex(data, len));
}

void AddLogUint64(char *fileName, char *extData, const uint64_t data) {
  char buf[100] = {0};
	sprintf(buf, "%x%x", (unsigned int)((data & 0xFFFFFFFF00000000) >> 32), (unsigned int)(data & 0xFFFFFFFF));
	AddLogLine(fileName, extData, buf);
}

void AddLogCurrentDT(char *fileName) {
	char buff[20];
	struct tm *curTime;

	time_t now = time(0);
	curTime = gmtime(&now);

	strftime (buff, sizeof(buff), "%Y-%m-%d %H:%M:%S", curTime);
	AddLogLine(fileName, "\nanticollision: ", buff);
}

void FillFileNameByUID(char *fileName, uint8_t * uid, char *ext, int byteCount) {
	char * fnameptr = fileName;
	memset(fileName, 0x00, 200);
	
	for (int j = 0; j < byteCount; j++, fnameptr += 2)
		sprintf(fnameptr, "%02x", (unsigned int) uid[j]); 
	sprintf(fnameptr, "%s", ext); 
}

void hex_to_buffer(const uint8_t *buf, const uint8_t *hex_data, const size_t hex_len, const size_t hex_max_len, 
	const size_t min_str_len, const size_t spaces_between, bool uppercase) {
		
	char *tmp = (char *)buf;
	size_t i;
	memset(tmp, 0x00, hex_max_len);

	int maxLen = ( hex_len > hex_max_len) ? hex_max_len : hex_len;

	for (i = 0; i < maxLen; ++i, tmp += 2 + spaces_between) {
		sprintf(tmp, (uppercase) ? "%02X" : "%02x", (unsigned int) hex_data[i]); 
		
		for (int j = 0; j < spaces_between; j++)
			sprintf(tmp + 2 + j, " ");
	}
	
	i *= (2 + spaces_between);
	int minStrLen = min_str_len > i ? min_str_len : 0;
	if (minStrLen > hex_max_len)
		minStrLen = hex_max_len;
	for(; i < minStrLen; i++, tmp += 1) 
		sprintf(tmp, " ");

	return;
}

// printing and converting functions

void print_hex(const uint8_t * data, const size_t len)
{
	size_t i;

	for (i=0; i < len; i++)
		printf("%02x ", data[i]);

	printf("\n");
}

void print_hex_break(const uint8_t *data, const size_t len, uint8_t breaks) {

	int rownum = 0;
	printf("[%02d] | ", rownum);
	for (int i = 0; i < len; ++i) {

		printf("%02X ", data[i]);
		
		// check if a line break is needed
		if ( breaks > 0 && !((i+1) % breaks) && (i+1 < len) ) {
			++rownum;
			printf("\n[%02d] | ", rownum);
		}
	}
	printf("\n");
}

char *sprint_hex(const uint8_t *data, const size_t len) {
	static char buf[1025] = {0};
	
	hex_to_buffer((uint8_t *)buf, data, len, sizeof(buf) - 1, 0, 1, false);

	return buf;
}

char *sprint_hex_inrow_ex(const uint8_t *data, const size_t len, const size_t min_str_len) {
	static char buf[1025] = {0};

	hex_to_buffer((uint8_t *)buf, data, len, sizeof(buf) - 1, min_str_len, 0, false);

	return buf;
}

char *sprint_hex_inrow(const uint8_t *data, const size_t len) {
	return sprint_hex_inrow_ex(data, len, 0);
}

char *sprint_bin_break(const uint8_t *data, const size_t len, const uint8_t breaks) {
	// make sure we don't go beyond our char array memory
	int max_len;
	if (breaks==0)
		max_len = ( len > MAX_BIN_BREAK_LENGTH ) ? MAX_BIN_BREAK_LENGTH : len;
	else
		max_len = ( len+(len/breaks) > MAX_BIN_BREAK_LENGTH ) ? MAX_BIN_BREAK_LENGTH : len+(len/breaks);

	static char buf[MAX_BIN_BREAK_LENGTH]; // 3072 + end of line characters if broken at 8 bits
	//clear memory
	memset(buf, 0x00, sizeof(buf));
	char *tmp = buf;

	size_t in_index = 0;
	// loop through the out_index to make sure we don't go too far
	for (size_t out_index=0; out_index < max_len; out_index++) {
		// set character - (should be binary but verify it isn't more than 1 digit)
		if (data[in_index]<10)
			sprintf(tmp++, "%u", (unsigned int) data[in_index]);
		// check if a line break is needed and we have room to print it in our array
		if ( (breaks > 0) && !((in_index+1) % breaks) && (out_index+1 < max_len) ) {
			// increment and print line break
			out_index++;
			sprintf(tmp++, "%s","\n");
		}
		in_index++;
	}

	return buf;
}

char *sprint_bin(const uint8_t *data, const size_t len) {
	return sprint_bin_break(data, len, 0);
}

char *sprint_hex_ascii(const uint8_t *data, const size_t len) {
	static char buf[1024];
	char *tmp = buf;
	memset(buf, 0x00, 1024);
	size_t max_len = (len > 255) ? 255 : len;
	// max 255 bytes * 3 + 2 characters = 767 in buffer
	sprintf(tmp, "%s| ", sprint_hex(data, max_len) );
	
	size_t i = 0;
	size_t pos = (max_len * 3)+2;
	// add another 255 characters ascii = 1020 characters of buffer used
	while(i < max_len) {
		char c = data[i];
		if ( (c < 32) || (c == 127))
			c = '.';
		sprintf(tmp+pos+i, "%c",  c);
		++i;
	}
	return buf;
}

char *sprint_ascii_ex(const uint8_t *data, const size_t len, const size_t min_str_len) {
	static char buf[1024];
	char *tmp = buf;
	memset(buf, 0x00, 1024);
	size_t max_len = (len > 1010) ? 1010 : len;
	size_t i = 0;
	while(i < max_len){
		char c = data[i];
		tmp[i] = ((c < 32) || (c == 127)) ? '.' : c;
		++i;
	}
	
	int minStrLen = min_str_len > i ? min_str_len : 0;
	for(; i < minStrLen; ++i) 
		tmp[i] = ' ';
	
	return buf;
}

char *sprint_ascii(const uint8_t *data, const size_t len) {
	return sprint_ascii_ex(data, len, 0);
}

void num_to_bytes(uint64_t n, size_t len, uint8_t* dest)
{
	while (len--) {
		dest[len] = (uint8_t) n;
		n >>= 8;
	}
}

uint64_t bytes_to_num(uint8_t* src, size_t len)
{
	uint64_t num = 0;
	while (len--)
	{
		num = (num << 8) | (*src);
		src++;
	}
	return num;
}

void num_to_bytebits(uint64_t	n, size_t len, uint8_t *dest) {
	while (len--) {
		dest[len] = n & 1;
		n >>= 1;
	}
}

//least significant bit first
void num_to_bytebitsLSBF(uint64_t n, size_t len, uint8_t *dest) {
	for(int i = 0 ; i < len ; ++i) {
		dest[i] =  n & 1;
		n >>= 1;
	}
}

// Swap bit order on a uint32_t value.  Can be limited by nrbits just use say 8bits reversal
// And clears the rest of the bits.
uint32_t SwapBits(uint32_t value, int nrbits) {
	uint32_t newvalue = 0;
	for(int i = 0; i < nrbits; i++) {
		newvalue ^= ((value >> i) & 1) << (nrbits - 1 - i);
	}
	return newvalue;
}

// aa,bb,cc,dd,ee,ff,gg,hh, ii,jj,kk,ll,mm,nn,oo,pp
// to
// hh,gg,ff,ee,dd,cc,bb,aa, pp,oo,nn,mm,ll,kk,jj,ii
// up to 64 bytes or 512 bits
uint8_t *SwapEndian64(const uint8_t *src, const size_t len, const uint8_t blockSize){
	static uint8_t buf[64];
	memset(buf, 0x00, 64);
	uint8_t *tmp = buf;
	for (uint8_t block=0; block < (uint8_t)(len/blockSize); block++){
		for (size_t i = 0; i < blockSize; i++){
			tmp[i+(blockSize*block)] = src[(blockSize-1-i)+(blockSize*block)];
		}
	}
	return tmp;
}

// takes a uint8_t src array, for len items and reverses the byte order in blocksizes (8,16,32,64), 
// returns: the dest array contains the reordered src array.
void SwapEndian64ex(const uint8_t *src, const size_t len, const uint8_t blockSize, uint8_t *dest){
	for (uint8_t block=0; block < (uint8_t)(len/blockSize); block++){
		for (size_t i = 0; i < blockSize; i++){
			dest[i+(blockSize*block)] = src[(blockSize-1-i)+(blockSize*block)];
		}
	}
}

//assumes little endian
char * printBits(size_t const size, void const * const ptr)
{
    unsigned char *b = (unsigned char*) ptr;	
    unsigned char byte;
	static char buf[1024];
	char * tmp = buf;
    int i, j;

    for (i=size-1;i>=0;i--)
    {
        for (j=7;j>=0;j--)
        {
            byte = b[i] & (1<<j);
            byte >>= j;
            sprintf(tmp, "%u", (unsigned int)byte);
			tmp++;
        }
    }
	return buf;
}

char * printBitsPar(const uint8_t *b, size_t len) {
	static char buf1[512] = {0};
	static char buf2[512] = {0};
	static char *buf;
	if (buf != buf1)
		buf = buf1;
	else
		buf = buf2;
	memset(buf, 0x00, 512);

	for (int i = 0; i < len; i++) {
		buf[i] = ((b[i / 8] << (i % 8)) & 0x80) ? '1':'0';
	}
	return buf;
}


//  -------------------------------------------------------------------------
//  string parameters lib
//  -------------------------------------------------------------------------

//  -------------------------------------------------------------------------
//  line     - param line
//  bg, en   - symbol numbers in param line of beginning and ending parameter
//  paramnum - param number (from 0)
//  -------------------------------------------------------------------------
int param_getptr(const char *line, int *bg, int *en, int paramnum)
{
	int i;
	int len = strlen(line);
	
	*bg = 0;
	*en = 0;
	
  // skip spaces
	while (line[*bg] ==' ' || line[*bg]=='\t') (*bg)++;
	if (*bg >= len) {
		return 1;
	}

	for (i = 0; i < paramnum; i++) {
		while (line[*bg]!=' ' && line[*bg]!='\t' && line[*bg] != '\0') (*bg)++;
		while (line[*bg]==' ' || line[*bg]=='\t') (*bg)++;
		
		if (line[*bg] == '\0') return 1;
	}
	
	*en = *bg;
	while (line[*en] != ' ' && line[*en] != '\t' && line[*en] != '\0') (*en)++;
	
	(*en)--;

	return 0;
}


int param_getlength(const char *line, int paramnum)
{
	int bg, en;
	
	if (param_getptr(line, &bg, &en, paramnum)) return 0;

	return en - bg + 1;
}

char param_getchar(const char *line, int paramnum) {
	return param_getchar_indx(line, 0, paramnum);
}

char param_getchar_indx(const char *line, int indx, int paramnum) {
	int bg, en;
	
	if (param_getptr(line, &bg, &en, paramnum)) return 0x00;

	if (bg + indx > en)
		return '\0';
	
	return line[bg + indx];
}

uint8_t param_get8(const char *line, int paramnum)
{
	return param_get8ex(line, paramnum, 0, 10);
}

/**
 * @brief Reads a decimal integer (actually, 0-254, not 255)
 * @param line
 * @param paramnum
 * @return -1 if error
 */
uint8_t param_getdec(const char *line, int paramnum, uint8_t *destination)
{
	uint8_t val =  param_get8ex(line, paramnum, 255, 10);
	if( (int8_t) val == -1) return 1;
	(*destination) = val;
	return 0;
}
/**
 * @brief Checks if param is decimal
 * @param line
 * @param paramnum
 * @return
 */
uint8_t param_isdec(const char *line, int paramnum)
{
	int bg, en;
	//TODO, check more thorougly
	if (!param_getptr(line, &bg, &en, paramnum)) return 1;
		//		return strtoul(&line[bg], NULL, 10) & 0xff;

	return 0;
}

uint8_t param_get8ex(const char *line, int paramnum, int deflt, int base)
{
	int bg, en;

	if (!param_getptr(line, &bg, &en, paramnum)) 
		return strtoul(&line[bg], NULL, base) & 0xff;
	else
		return deflt;
}

uint32_t param_get32ex(const char *line, int paramnum, int deflt, int base)
{
	int bg, en;

	if (!param_getptr(line, &bg, &en, paramnum)) 
		return strtoul(&line[bg], NULL, base);
	else
		return deflt;
}

uint64_t param_get64ex(const char *line, int paramnum, int deflt, int base)
{
	int bg, en;

	if (!param_getptr(line, &bg, &en, paramnum)) 
		return strtoull(&line[bg], NULL, base);
	else
		return deflt;
}

int param_gethex(const char *line, int paramnum, uint8_t * data, int hexcnt)
{
	int bg, en, temp, i;

	if (hexcnt % 2)
		return 1;
	
	if (param_getptr(line, &bg, &en, paramnum)) return 1;

	if (en - bg + 1 != hexcnt) 
		return 1;

	for(i = 0; i < hexcnt; i += 2) {
		if (!(isxdigit((unsigned char)line[bg + i]) && isxdigit((unsigned char)line[bg + i + 1])) )	return 1;
		
		sscanf((char[]){line[bg + i], line[bg + i + 1], 0}, "%X", &temp);
		data[i / 2] = temp & 0xff;
	}	

	return 0;
}
int param_gethex_ex(const char *line, int paramnum, uint8_t * data, int *hexcnt)
{
	int bg, en, temp, i;

	//if (hexcnt % 2)
	//	return 1;
	
	if (param_getptr(line, &bg, &en, paramnum)) return 1;

	*hexcnt = en - bg + 1;
	if (*hexcnt % 2) //error if not complete hex bytes
		return 1;

	for(i = 0; i < *hexcnt; i += 2) {
		if (!(isxdigit((unsigned char)line[bg + i]) && isxdigit((unsigned char)line[bg + i + 1])) )	return 1;
		
		sscanf((char[]){line[bg + i], line[bg + i + 1], 0}, "%X", &temp);
		data[i / 2] = temp & 0xff;
	}	

	return 0;
}

int param_gethex_to_eol(const char *line, int paramnum, uint8_t * data, int maxdatalen, int *datalen) {
	int bg, en;
	uint32_t temp;
	char buf[5] = {0};

	if (param_getptr(line, &bg, &en, paramnum)) return 1;

	*datalen = 0;
	
	int indx = bg;
	while (line[indx]) {
		if (line[indx] == '\t' || line[indx] == ' ') {
			indx++;
			continue;
		}
		
		if (isxdigit((unsigned char)line[indx])) {
			buf[strlen(buf) + 1] = 0x00;
			buf[strlen(buf)] = line[indx];
		} else {
			// if we have symbols other than spaces and hex
			return 1;
		}				

		if (*datalen >= maxdatalen) {
			// if we dont have space in buffer and have symbols to translate
			return 2;
		}

		if (strlen(buf) >= 2) {
			sscanf(buf, "%x", &temp);
			data[*datalen] = (uint8_t)(temp & 0xff);
			*buf = 0;
			(*datalen)++;
		}
		
		indx++;
	}

	if (strlen(buf) > 0) 
		//error when not completed hex bytes
		return 3;
		
	return 0;
}

int param_getstr(const char *line, int paramnum, char * str, size_t buffersize)
{
	int bg, en;

	if (param_getptr(line, &bg, &en, paramnum)) {	
		return 0;
	}

	// Prevent out of bounds errors
	if (en - bg + 1 >= buffersize) {
		printf("out of bounds error: want %d bytes have %zd bytes\n", en - bg + 1 + 1, buffersize);
		return 0;
	}
	
	memcpy(str, line + bg, en - bg + 1);
	str[en - bg + 1] = 0;
	
	return en - bg + 1;
}

/*
The following methods comes from Rfidler sourcecode.
https://github.com/ApertureLabsLtd/RFIDler/blob/master/firmware/Pic32/RFIDler.X/src/
*/

// convert hex to sequence of 0/1 bit values
// returns number of bits converted
int hextobinarray(char *target, char *source)
{
    int length, i, count= 0;
    char* start = source;
    char x;

    length = strlen(source);
    // process 4 bits (1 hex digit) at a time
    while(length--)
    {
        x= *(source++);
        // capitalize
        if (x >= 'a' && x <= 'f')
            x -= 32;
        // convert to numeric value
        if (x >= '0' && x <= '9')
            x -= '0';
        else if (x >= 'A' && x <= 'F')
            x -= 'A' - 10;
        else {
        	printf("Discovered unknown character %c %d at idx %tu of %s\n", x, x, source - start, start);
            return 0;
        }
        // output
        for(i= 0 ; i < 4 ; ++i, ++count)
            *(target++)= (x >> (3 - i)) & 1;
    }
    
    return count;
}

// convert hex to human readable binary string
int hextobinstring(char *target, char *source)
{
    int length;

    if(!(length= hextobinarray(target, source)))
        return 0;
    binarraytobinstring(target, target, length);
    return length;
}

// convert binary array of 0x00/0x01 values to hex (safe to do in place as target will always be shorter than source)
// return number of bits converted
int binarraytohex(char *target,char *source, int length)
{
    unsigned char i, x;
    int j = length;

    if(j % 4)
        return 0;

    while(j)
    {
        for(i= x= 0 ; i < 4 ; ++i)
            x +=  ( source[i] << (3 - i));
        sprintf(target,"%X", (unsigned int)x);
        ++target;
        source += 4;
        j -= 4;
    }
    return length;
}

// convert binary array to human readable binary
void binarraytobinstring(char *target, char *source,  int length)
{
    int i;

    for(i= 0 ; i < length ; ++i)
        *(target++)= *(source++) + '0';
    *target= '\0';
}

// return parity bit required to match type
uint8_t GetParity( uint8_t *bits, uint8_t type, int length)
{
    int x;

    for(x= 0 ; length > 0 ; --length)
        x += bits[length - 1];
    x %= 2;

    return x ^ type;
}

// add HID parity to binary array: EVEN prefix for 1st half of ID, ODD suffix for 2nd half
void wiegand_add_parity(uint8_t *target, uint8_t *source, uint8_t length)
{
    *(target++)= GetParity(source, EVEN, length / 2);
    memcpy(target, source, length);
    target += length;
    *(target)= GetParity(source + length / 2, ODD, length / 2);
}

// xor two arrays together for len items.  The dst array contains the new xored values.
void xor(unsigned char *dst, unsigned char *src, size_t len) {
   for( ; len > 0; len--,dst++,src++)
       *dst ^= *src;
}

int32_t le24toh (uint8_t data[3]) {
    return (data[2] << 16) | (data[1] << 8) | data[0];
}
uint32_t le32toh (uint8_t *data) {
	return (uint32_t)( (data[3]<<24) | (data[2]<<16) | (data[1]<<8) | data[0]);
}

// RotateLeft - Ultralight, Desfire, works on byte level
// 00-01-02  >> 01-02-00
void rol(uint8_t *data, const size_t len){
    uint8_t first = data[0];
    for (size_t i = 0; i < len-1; i++) {
        data[i] = data[i+1];
    }
    data[len-1] = first;
}


// Replace unprintable characters with a dot in char buffer
void clean_ascii(unsigned char *buf, size_t len) {
  for (size_t i = 0; i < len; i++) {
    if (!isprint(buf[i]))
      buf[i] = '.';
  }
}

// replace \r \n to \0
void strcleanrn(char *buf, size_t len) {
	strcreplace(buf, len, '\n', '\0');
	strcreplace(buf, len, '\r', '\0');
}

// replace char in buffer
void strcreplace(char *buf, size_t len, char from, char to) {
  for (size_t i = 0; i < len; i++) {
    if (buf[i] == from)
      buf[i] = to;
  }
}

char *strmcopy(char *buf) {
	char * str = NULL;
	if ((str = (char*) malloc(strlen(buf) + 1)) != NULL) {
		memset(str, 0, strlen(buf) + 1);
		strcpy(str, buf);
	}	
	return str;
}


// determine number of logical CPU cores (use for multithreaded functions)
extern int num_CPUs(void)
{
#if defined(_WIN32)
	#include <sysinfoapi.h>
	SYSTEM_INFO sysinfo;
	GetSystemInfo(&sysinfo);
	return sysinfo.dwNumberOfProcessors;
#elif defined(__linux__) || defined(__APPLE__)
	#include <unistd.h>
	return sysconf(_SC_NPROCESSORS_ONLN);
#else
	return 1;
#endif
}

