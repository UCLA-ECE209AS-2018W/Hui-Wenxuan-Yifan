// Merlok, 2011, 2012
// people from mifare@nethemba.com, 2010
//
// This code is licensed to you under the terms of the GNU GPL, version 2 or,
// at your option, any later version. See the LICENSE.txt file for the text of
// the license.
//-----------------------------------------------------------------------------
// mifare commands
//-----------------------------------------------------------------------------

#include "mifarehost.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "crapto1/crapto1.h"
#include "proxmark3.h"
#include "usb_cmd.h"
#include "cmdmain.h"
#include "ui.h"
#include "parity.h"
#include "util.h"
#include "iso14443crc.h"

#include "mifare.h"

// mifare tracer flags used in mfTraceDecode()
#define TRACE_IDLE		 				0x00
#define TRACE_AUTH1		 				0x01
#define TRACE_AUTH2		 				0x02
#define TRACE_AUTH_OK	 				0x03
#define TRACE_READ_DATA 				0x04
#define TRACE_WRITE_OK					0x05
#define TRACE_WRITE_DATA				0x06
#define TRACE_ERROR		 				0xFF


static int compare_uint64(const void *a, const void *b) {
	// didn't work: (the result is truncated to 32 bits)
	//return (*(int64_t*)b - *(int64_t*)a);

	// better:
	if (*(uint64_t*)b == *(uint64_t*)a) return 0;
	else if (*(uint64_t*)b < *(uint64_t*)a) return 1;
	else return -1;
}


// create the intersection (common members) of two sorted lists. Lists are terminated by -1. Result will be in list1. Number of elements is returned.
static uint32_t intersection(uint64_t *list1, uint64_t *list2)
{
	if (list1 == NULL || list2 == NULL) {
		return 0;
	}
	uint64_t *p1, *p2, *p3;
	p1 = p3 = list1;
	p2 = list2;

	while ( *p1 != -1 && *p2 != -1 ) {
		if (compare_uint64(p1, p2) == 0) {
			*p3++ = *p1++;
			p2++;
		}
		else {
			while (compare_uint64(p1, p2) < 0) ++p1;
			while (compare_uint64(p1, p2) > 0) ++p2;
		}
	}
	*p3 = -1;
	return p3 - list1;
}


// Darkside attack (hf mf mifare)
static uint32_t nonce2key(uint32_t uid, uint32_t nt, uint32_t nr, uint32_t ar, uint64_t par_info, uint64_t ks_info, uint64_t **keys) {
	struct Crypto1State *states;
	uint32_t i, pos;
	uint8_t bt, ks3x[8], par[8][8];
	uint64_t key_recovered;
	uint64_t *keylist;

	// Reset the last three significant bits of the reader nonce
	nr &= 0xffffff1f;

	for (pos=0; pos<8; pos++) {
		ks3x[7-pos] = (ks_info >> (pos*8)) & 0x0f;
		bt = (par_info >> (pos*8)) & 0xff;
		for (i=0; i<8; i++)	{
				par[7-pos][i] = (bt >> i) & 0x01;
		}
	}

	states = lfsr_common_prefix(nr, ar, ks3x, par, (par_info == 0));

	if (states == NULL) {
		*keys = NULL;
		return 0;
	}

	keylist = (uint64_t*)states;

	for (i = 0; keylist[i]; i++) {
		lfsr_rollback_word(states+i, uid^nt, 0);
		crypto1_get_lfsr(states+i, &key_recovered);
		keylist[i] = key_recovered;
	}
	keylist[i] = -1;

	*keys = keylist;
	return i;
}


int mfDarkside(uint64_t *key)
{
	uint32_t uid = 0;
	uint32_t nt = 0, nr = 0, ar = 0;
	uint64_t par_list = 0, ks_list = 0;
	uint64_t *keylist = NULL, *last_keylist = NULL;
	uint32_t keycount = 0;
	int16_t isOK = 0;

	UsbCommand c = {CMD_READER_MIFARE, {true, 0, 0}};

	// message
	printf("-------------------------------------------------------------------------\n");
	printf("Executing command. Expected execution time: 25sec on average\n");
	printf("Press button on the proxmark3 device to abort both proxmark3 and client.\n");
	printf("-------------------------------------------------------------------------\n");


	while (true) {
		clearCommandBuffer();
		SendCommand(&c);

		//flush queue
		while (ukbhit()) {
			int c = getchar(); (void) c;
		}

		// wait cycle
		while (true) {
			printf(".");
			fflush(stdout);
			if (ukbhit()) {
				return -5;
				break;
			}

			UsbCommand resp;
			if (WaitForResponseTimeout(CMD_ACK, &resp, 1000)) {
				isOK  = resp.arg[0];
				if (isOK < 0) {
					return isOK;
				}
				uid = (uint32_t)bytes_to_num(resp.d.asBytes +  0, 4);
				nt =  (uint32_t)bytes_to_num(resp.d.asBytes +  4, 4);
				par_list = bytes_to_num(resp.d.asBytes +  8, 8);
				ks_list = bytes_to_num(resp.d.asBytes +  16, 8);
				nr = (uint32_t)bytes_to_num(resp.d.asBytes + 24, 4);
				ar = (uint32_t)bytes_to_num(resp.d.asBytes + 28, 4);
				break;
			}
		}

		if (par_list == 0 && c.arg[0] == true) {
			PrintAndLog("Parity is all zero. Most likely this card sends NACK on every failed authentication.");
		}
		c.arg[0] = false;

		keycount = nonce2key(uid, nt, nr, ar, par_list, ks_list, &keylist);

		if (keycount == 0) {
			PrintAndLog("Key not found (lfsr_common_prefix list is null). Nt=%08x", nt);
			PrintAndLog("This is expected to happen in 25%% of all cases. Trying again with a different reader nonce...");
			continue;
		}

		if (par_list == 0) {
			qsort(keylist, keycount, sizeof(*keylist), compare_uint64);
			keycount = intersection(last_keylist, keylist);
			if (keycount == 0) {
				free(last_keylist);
				last_keylist = keylist;
				continue;
			}
		}

		if (keycount > 1) {
			PrintAndLog("Found %u possible keys. Trying to authenticate with each of them ...\n", keycount);
		} else {
			PrintAndLog("Found a possible key. Trying to authenticate...\n");
		}

		*key = -1;
		uint8_t keyBlock[USB_CMD_DATA_SIZE];
		int max_keys = USB_CMD_DATA_SIZE/6;
		for (int i = 0; i < keycount; i += max_keys) {
			int size = keycount - i > max_keys ? max_keys : keycount - i;
			for (int j = 0; j < size; j++) {
				if (par_list == 0) {
					num_to_bytes(last_keylist[i*max_keys + j], 6, keyBlock+(j*6));
				} else {
					num_to_bytes(keylist[i*max_keys + j], 6, keyBlock+(j*6));
				}
			}
			if (!mfCheckKeys(0, 0, false, size, keyBlock, key)) {
				break;
			}
		}

		if (*key != -1) {
			free(last_keylist);
			free(keylist);
			break;
		} else {
			PrintAndLog("Authentication failed. Trying again...");
			free(last_keylist);
			last_keylist = keylist;
		}
	}

	return 0;
}


int mfCheckKeys (uint8_t blockNo, uint8_t keyType, bool clear_trace, uint8_t keycnt, uint8_t * keyBlock, uint64_t * key){

	*key = -1;

	UsbCommand c = {CMD_MIFARE_CHKKEYS, {((blockNo & 0xff) | ((keyType & 0xff) << 8)), clear_trace, keycnt}}; 
	memcpy(c.d.asBytes, keyBlock, 6 * keycnt);
	SendCommand(&c);

	UsbCommand resp;
	if (!WaitForResponseTimeout(CMD_ACK,&resp,3000)) return 1; 
	if ((resp.arg[0] & 0xff) != 0x01) return 2;
	*key = bytes_to_num(resp.d.asBytes, 6);
	return 0;
}

int mfCheckKeysSec(uint8_t sectorCnt, uint8_t keyType, uint8_t timeout14a, bool clear_trace, uint8_t keycnt, uint8_t * keyBlock, sector_t * e_sector){

	uint8_t keyPtr = 0;

	if (e_sector == NULL)
		return -1;

	UsbCommand c = {CMD_MIFARE_CHKKEYS, {((sectorCnt & 0xff) | ((keyType & 0xff) << 8)), (clear_trace | 0x02)|((timeout14a & 0xff) << 8), keycnt}}; 
	memcpy(c.d.asBytes, keyBlock, 6 * keycnt);
	SendCommand(&c);

	UsbCommand resp;
	if (!WaitForResponseTimeoutW(CMD_ACK, &resp, MAX(3000, 1000 + 13 * sectorCnt * keycnt * (keyType == 2 ? 2 : 1)), false)) return 1; // timeout: 13 ms / fail auth
	if ((resp.arg[0] & 0xff) != 0x01) return 2;
	
	bool foundAKey = false;
	for(int sec = 0; sec < sectorCnt; sec++){
		for(int keyAB = 0; keyAB < 2; keyAB++){
			keyPtr = *(resp.d.asBytes + keyAB * 40 + sec);
			if (keyPtr){
				e_sector[sec].foundKey[keyAB] = true;
				e_sector[sec].Key[keyAB] = bytes_to_num(keyBlock + (keyPtr - 1) * 6, 6);
				foundAKey = true;
			}
		}
	}
	return foundAKey ? 0 : 3;
}

// Compare 16 Bits out of cryptostate
int Compare16Bits(const void * a, const void * b) {
	if ((*(uint64_t*)b & 0x00ff000000ff0000) == (*(uint64_t*)a & 0x00ff000000ff0000)) return 0;
	else if ((*(uint64_t*)b & 0x00ff000000ff0000) > (*(uint64_t*)a & 0x00ff000000ff0000)) return 1;
	else return -1;
}

typedef
	struct {
		union {
			struct Crypto1State *slhead;
			uint64_t *keyhead;
		} head;
		union {
			struct Crypto1State *sltail;
			uint64_t *keytail;
		} tail;
		uint32_t len;
		uint32_t uid;
		uint32_t blockNo;
		uint32_t keyType;
		uint32_t nt;
		uint32_t ks1;
	} StateList_t;


// wrapper function for multi-threaded lfsr_recovery32
void
#ifdef __has_attribute
#if __has_attribute(force_align_arg_pointer)
__attribute__((force_align_arg_pointer)) 
#endif
#endif
*nested_worker_thread(void *arg)
{
	struct Crypto1State *p1;
	StateList_t *statelist = arg;

	statelist->head.slhead = lfsr_recovery32(statelist->ks1, statelist->nt ^ statelist->uid);
	for (p1 = statelist->head.slhead; *(uint64_t *)p1 != 0; p1++);
	statelist->len = p1 - statelist->head.slhead;
	statelist->tail.sltail = --p1;
	qsort(statelist->head.slhead, statelist->len, sizeof(uint64_t), Compare16Bits);

	return statelist->head.slhead;
}


int mfnested(uint8_t blockNo, uint8_t keyType, uint8_t *key, uint8_t trgBlockNo, uint8_t trgKeyType, uint8_t *resultKey, bool calibrate)
{
	uint16_t i;
	uint32_t uid;
	UsbCommand resp;

	StateList_t statelists[2];
	struct Crypto1State *p1, *p2, *p3, *p4;

	// flush queue
	WaitForResponseTimeout(CMD_ACK, NULL, 100);

	UsbCommand c = {CMD_MIFARE_NESTED, {blockNo + keyType * 0x100, trgBlockNo + trgKeyType * 0x100, calibrate}};
	memcpy(c.d.asBytes, key, 6);
	SendCommand(&c);

	if (!WaitForResponseTimeout(CMD_ACK, &resp, 1500)) {
		return -1;
	}

	if (resp.arg[0]) {
		return resp.arg[0];  // error during nested
	}

	memcpy(&uid, resp.d.asBytes, 4);
	PrintAndLog("uid:%08x trgbl=%d trgkey=%x", uid, (uint16_t)resp.arg[2] & 0xff, (uint16_t)resp.arg[2] >> 8);

	for (i = 0; i < 2; i++) {
		statelists[i].blockNo = resp.arg[2] & 0xff;
		statelists[i].keyType = (resp.arg[2] >> 8) & 0xff;
		statelists[i].uid = uid;
		memcpy(&statelists[i].nt,  (void *)(resp.d.asBytes + 4 + i * 8 + 0), 4);
		memcpy(&statelists[i].ks1, (void *)(resp.d.asBytes + 4 + i * 8 + 4), 4);
	}

	// calc keys

	pthread_t thread_id[2];

	// create and run worker threads
	for (i = 0; i < 2; i++) {
		pthread_create(thread_id + i, NULL, nested_worker_thread, &statelists[i]);
	}

	// wait for threads to terminate:
	for (i = 0; i < 2; i++) {
		pthread_join(thread_id[i], (void*)&statelists[i].head.slhead);
	}


	// the first 16 Bits of the cryptostate already contain part of our key.
	// Create the intersection of the two lists based on these 16 Bits and
	// roll back the cryptostate
	p1 = p3 = statelists[0].head.slhead;
	p2 = p4 = statelists[1].head.slhead;
	while (p1 <= statelists[0].tail.sltail && p2 <= statelists[1].tail.sltail) {
		if (Compare16Bits(p1, p2) == 0) {
			struct Crypto1State savestate, *savep = &savestate;
			savestate = *p1;
			while(Compare16Bits(p1, savep) == 0 && p1 <= statelists[0].tail.sltail) {
				*p3 = *p1;
				lfsr_rollback_word(p3, statelists[0].nt ^ statelists[0].uid, 0);
				p3++;
				p1++;
			}
			savestate = *p2;
			while(Compare16Bits(p2, savep) == 0 && p2 <= statelists[1].tail.sltail) {
				*p4 = *p2;
				lfsr_rollback_word(p4, statelists[1].nt ^ statelists[1].uid, 0);
				p4++;
				p2++;
			}
		}
		else {
			while (Compare16Bits(p1, p2) == -1) p1++;
			while (Compare16Bits(p1, p2) == 1) p2++;
		}
	}
	*(uint64_t*)p3 = -1;
	*(uint64_t*)p4 = -1;
	statelists[0].len = p3 - statelists[0].head.slhead;
	statelists[1].len = p4 - statelists[1].head.slhead;
	statelists[0].tail.sltail=--p3;
	statelists[1].tail.sltail=--p4;

	// the statelists now contain possible keys. The key we are searching for must be in the
	// intersection of both lists. Create the intersection:
	qsort(statelists[0].head.keyhead, statelists[0].len, sizeof(uint64_t), compare_uint64);
	qsort(statelists[1].head.keyhead, statelists[1].len, sizeof(uint64_t), compare_uint64);
	statelists[0].len = intersection(statelists[0].head.keyhead, statelists[1].head.keyhead);

	memset(resultKey, 0, 6);
	// The list may still contain several key candidates. Test each of them with mfCheckKeys
	for (i = 0; i < statelists[0].len; i++) {
		uint8_t keyBlock[6];
		uint64_t key64;
		crypto1_get_lfsr(statelists[0].head.slhead + i, &key64);
		num_to_bytes(key64, 6, keyBlock);
		key64 = 0;
		if (!mfCheckKeys(statelists[0].blockNo, statelists[0].keyType, false, 1, keyBlock, &key64)) {
			num_to_bytes(key64, 6, resultKey);
			break;
		}
	}

	free(statelists[0].head.slhead);
	free(statelists[1].head.slhead);

	return 0;
}

// EMULATOR

int mfEmlGetMem(uint8_t *data, int blockNum, int blocksCount) {
	UsbCommand c = {CMD_MIFARE_EML_MEMGET, {blockNum, blocksCount, 0}};
 	SendCommand(&c);

  UsbCommand resp;
	if (!WaitForResponseTimeout(CMD_ACK,&resp,1500)) return 1;
	memcpy(data, resp.d.asBytes, blocksCount * 16);
	return 0;
}

int mfEmlSetMem(uint8_t *data, int blockNum, int blocksCount) {
	UsbCommand c = {CMD_MIFARE_EML_MEMSET, {blockNum, blocksCount, 0}};
	memcpy(c.d.asBytes, data, blocksCount * 16);
	SendCommand(&c);
	return 0;
}

// "MAGIC" CARD

int mfCGetBlock(uint8_t blockNo, uint8_t *data, uint8_t params) {
	uint8_t isOK = 0;

	UsbCommand c = {CMD_MIFARE_CGETBLOCK, {params, 0, blockNo}};
	SendCommand(&c);

	UsbCommand resp;
	if (WaitForResponseTimeout(CMD_ACK,&resp,1500)) {
		isOK  = resp.arg[0] & 0xff;
		memcpy(data, resp.d.asBytes, 16);
		if (!isOK) return 2;
	} else {
		PrintAndLog("Command execute timeout");
		return 1;
	}
	return 0;
}

int mfCSetBlock(uint8_t blockNo, uint8_t *data, uint8_t *uid, bool wantWipe, uint8_t params) {

	uint8_t isOK = 0;
	UsbCommand c = {CMD_MIFARE_CSETBLOCK, {wantWipe, params & (0xFE | (uid == NULL ? 0:1)), blockNo}};
	memcpy(c.d.asBytes, data, 16);
	SendCommand(&c);

	UsbCommand resp;
	if (WaitForResponseTimeout(CMD_ACK, &resp, 1500)) {
		isOK  = resp.arg[0] & 0xff;
		if (uid != NULL)
			memcpy(uid, resp.d.asBytes, 4);
		if (!isOK)
			return 2;
	} else {
		PrintAndLog("Command execute timeout");
		return 1;
	}

	return 0;
}

int mfCWipe(uint32_t numSectors, bool gen1b, bool wantWipe, bool wantFill) {
	uint8_t isOK = 0;
	uint8_t cmdParams = wantWipe + wantFill * 0x02 + gen1b * 0x04;
	UsbCommand c = {CMD_MIFARE_CWIPE, {numSectors, cmdParams, 0}};
	SendCommand(&c);

	UsbCommand resp;
	WaitForResponse(CMD_ACK,&resp);
	isOK  = resp.arg[0] & 0xff;
	
	return isOK;
}

int mfCSetUID(uint8_t *uid, uint8_t *atqa, uint8_t *sak, uint8_t *oldUID) {
	uint8_t oldblock0[16] = {0x00};
	uint8_t block0[16] = {0x00};
	int gen = 0, res;

	gen = mfCIdentify();

	/* generation 1a magic card by default */
	uint8_t cmdParams = CSETBLOCK_SINGLE_OPER;
	if (gen == 2) {
		/* generation 1b magic card */
		cmdParams = CSETBLOCK_SINGLE_OPER | CSETBLOCK_MAGIC_1B;
	}
	
	res = mfCGetBlock(0, oldblock0, cmdParams);

	if (res == 0) {
		memcpy(block0, oldblock0, 16);
		PrintAndLog("old block 0:  %s", sprint_hex(block0,16));
	} else {
		PrintAndLog("Couldn't get old data. Will write over the last bytes of Block 0.");
	}

	// fill in the new values
	// UID
	memcpy(block0, uid, 4);
	// Mifare UID BCC
	block0[4] = block0[0] ^ block0[1] ^ block0[2] ^ block0[3];
	// mifare classic SAK(byte 5) and ATQA(byte 6 and 7, reversed)
	if (sak != NULL)
		block0[5] = sak[0];
	if (atqa != NULL) {
		block0[6] = atqa[1];
		block0[7] = atqa[0];
	}
	PrintAndLog("new block 0:  %s", sprint_hex(block0, 16));

	res = mfCSetBlock(0, block0, oldUID, false, cmdParams);
	if (res) {
		PrintAndLog("Can't set block 0. Error: %d", res);
		return res;
	}
	
	return 0;
}

int mfCIdentify() {
	UsbCommand c = {CMD_MIFARE_CIDENT, {0, 0, 0}};
	SendCommand(&c);
	UsbCommand resp;
	WaitForResponse(CMD_ACK,&resp);

	uint8_t isGeneration = resp.arg[0] & 0xff;
	switch( isGeneration ){
		case 1: PrintAndLog("Chinese magic backdoor commands (GEN 1a) detected"); break;
		case 2: PrintAndLog("Chinese magic backdoor command (GEN 1b) detected"); break;
		default: PrintAndLog("No chinese magic backdoor command detected"); break;
	}

	return (int) isGeneration;
}


// SNIFFER

// constants
static uint8_t trailerAccessBytes[4] = {0x08, 0x77, 0x8F, 0x00};

// variables
char logHexFileName[FILE_PATH_SIZE] = {0x00};
static uint8_t traceCard[4096] = {0x00};
static char traceFileName[FILE_PATH_SIZE] = {0x00};
static int traceState = TRACE_IDLE;
static uint8_t traceCurBlock = 0;
static uint8_t traceCurKey = 0;

struct Crypto1State *traceCrypto1 = NULL;

struct Crypto1State *revstate;
uint64_t lfsr;
uint64_t ui64Key;
uint32_t ks2;
uint32_t ks3;

uint32_t uid;       // serial number
uint32_t nt;        // tag challenge
uint32_t nt_enc;    // encrypted tag challenge
uint8_t nt_enc_par; // encrypted tag challenge parity
uint32_t nr_enc;    // encrypted reader challenge
uint32_t ar_enc;    // encrypted reader response
uint8_t ar_enc_par; // encrypted reader response parity
uint32_t at_enc;    // encrypted tag response
uint8_t at_enc_par; // encrypted tag response parity

int isTraceCardEmpty(void) {
	return ((traceCard[0] == 0) && (traceCard[1] == 0) && (traceCard[2] == 0) && (traceCard[3] == 0));
}

int isBlockEmpty(int blockN) {
	for (int i = 0; i < 16; i++)
		if (traceCard[blockN * 16 + i] != 0) return 0;

	return 1;
}

int isBlockTrailer(int blockN) {
 return ((blockN & 0x03) == 0x03);
}

int saveTraceCard(void) {
	FILE * f;

	if ((!strlen(traceFileName)) || (isTraceCardEmpty())) return 0;

	f = fopen(traceFileName, "w+");
	if ( !f ) return 1;

	for (int i = 0; i < 64; i++) {  // blocks
		for (int j = 0; j < 16; j++)  // bytes
			fprintf(f, "%02x", *(traceCard + i * 16 + j));
		if (i < 63)
			fprintf(f,"\n");
	}
	fclose(f);
	return 0;
}

int loadTraceCard(uint8_t *tuid) {
	FILE * f;
	char buf[64] = {0x00};
	uint8_t buf8[64] = {0x00};
	int i, blockNum;

	if (!isTraceCardEmpty())
		saveTraceCard();

	memset(traceCard, 0x00, 4096);
	memcpy(traceCard, tuid + 3, 4);

	FillFileNameByUID(traceFileName, tuid, ".eml", 7);

	f = fopen(traceFileName, "r");
	if (!f) return 1;

	blockNum = 0;

	while(!feof(f)){

		memset(buf, 0, sizeof(buf));
		if (fgets(buf, sizeof(buf), f) == NULL) {
			PrintAndLog("File reading error.");
			fclose(f);
			return 2;
    	}

		if (strlen(buf) < 32){
			if (feof(f)) break;
			PrintAndLog("File content error. Block data must include 32 HEX symbols");
			fclose(f);
			return 2;
		}
		for (i = 0; i < 32; i += 2)
			sscanf(&buf[i], "%02x", (unsigned int *)&buf8[i / 2]);

		memcpy(traceCard + blockNum * 16, buf8, 16);

		blockNum++;
	}
	fclose(f);

	return 0;
}

int mfTraceInit(uint8_t *tuid, uint8_t *atqa, uint8_t sak, bool wantSaveToEmlFile) {

	if (traceCrypto1)
		crypto1_destroy(traceCrypto1);

	traceCrypto1 = NULL;

	if (wantSaveToEmlFile)
		loadTraceCard(tuid);

	traceCard[4] = traceCard[0] ^ traceCard[1] ^ traceCard[2] ^ traceCard[3];
	traceCard[5] = sak;
	memcpy(&traceCard[6], atqa, 2);
	traceCurBlock = 0;
	uid = bytes_to_num(tuid + 3, 4);

	traceState = TRACE_IDLE;

	return 0;
}

void mf_crypto1_decrypt(struct Crypto1State *pcs, uint8_t *data, int len, bool isEncrypted){
	uint8_t	bt = 0;
	int i;

	if (len != 1) {
		for (i = 0; i < len; i++)
			data[i] = crypto1_byte(pcs, 0x00, isEncrypted) ^ data[i];
	} else {
		bt = 0;
		for (i = 0; i < 4; i++)
			bt |= (crypto1_bit(pcs, 0, isEncrypted) ^ BIT(data[0], i)) << i;

		data[0] = bt;
	}
	return;
}

bool NTParityCheck(uint32_t ntx) {
	if (
		(oddparity8(ntx >> 8 & 0xff) ^ (ntx & 0x01) ^ ((nt_enc_par >> 5) & 0x01) ^ (nt_enc & 0x01)) ||
		(oddparity8(ntx >> 16 & 0xff) ^ (ntx >> 8 & 0x01) ^ ((nt_enc_par >> 6) & 0x01) ^ (nt_enc >> 8 & 0x01)) ||
		(oddparity8(ntx >> 24 & 0xff) ^ (ntx >> 16 & 0x01) ^ ((nt_enc_par >> 7) & 0x01) ^ (nt_enc >> 16 & 0x01))
		)
		return false;
	
	uint32_t ar = prng_successor(ntx, 64);
	if (
		(oddparity8(ar >> 8 & 0xff) ^ (ar & 0x01) ^ ((ar_enc_par >> 5) & 0x01) ^ (ar_enc & 0x01)) ||
		(oddparity8(ar >> 16 & 0xff) ^ (ar >> 8 & 0x01) ^ ((ar_enc_par >> 6) & 0x01) ^ (ar_enc >> 8 & 0x01)) ||
		(oddparity8(ar >> 24 & 0xff) ^ (ar >> 16 & 0x01) ^ ((ar_enc_par >> 7) & 0x01) ^ (ar_enc >> 16 & 0x01))
		)
		return false;

	uint32_t at = prng_successor(ntx, 96);
	if (
		(oddparity8(ar & 0xff) ^ (at >> 24 & 0x01) ^ ((ar_enc_par >> 4) & 0x01) ^ (at_enc >> 24 & 0x01)) ||
		(oddparity8(at >> 8 & 0xff) ^ (at & 0x01) ^ ((at_enc_par >> 5) & 0x01) ^ (at_enc & 0x01)) ||
		(oddparity8(at >> 16 & 0xff) ^ (at >> 8 & 0x01) ^ ((at_enc_par >> 6) & 0x01) ^ (at_enc >> 8 & 0x01)) ||
		(oddparity8(at >> 24 & 0xff) ^ (at >> 16 & 0x01) ^ ((at_enc_par >> 7) & 0x01) ^ (at_enc >> 16 & 0x01))
		)
		return false;
		
	return true;
}


int mfTraceDecode(uint8_t *data_src, int len, uint8_t parity, bool wantSaveToEmlFile) {
	uint8_t data[64];

	if (traceState == TRACE_ERROR) return 1;
	if (len > 64) {
		traceState = TRACE_ERROR;
		return 1;
	}

	memcpy(data, data_src, len);
	if ((traceCrypto1) && ((traceState == TRACE_IDLE) || (traceState > TRACE_AUTH_OK))) {
		mf_crypto1_decrypt(traceCrypto1, data, len, 0);
		uint8_t parity[16];
		oddparitybuf(data, len, parity);
		PrintAndLog("dec> %s [%s]", sprint_hex(data, len), printBitsPar(parity, len));
		AddLogHex(logHexFileName, "dec> ", data, len);
	}

	switch (traceState) {
	case TRACE_IDLE:
		// check packet crc16!
		if ((len >= 4) && (!CheckCrc14443(CRC_14443_A, data, len))) {
			PrintAndLog("dec> CRC ERROR!!!");
			AddLogLine(logHexFileName, "dec> ", "CRC ERROR!!!");
			traceState = TRACE_ERROR;  // do not decrypt the next commands
			return 1;
		}

		// AUTHENTICATION
		if ((len ==4) && ((data[0] == 0x60) || (data[0] == 0x61))) {
			traceState = TRACE_AUTH1;
			traceCurBlock = data[1];
			traceCurKey = data[0] == 60 ? 1:0;
			return 0;
		}

		// READ
		if ((len ==4) && ((data[0] == 0x30))) {
			traceState = TRACE_READ_DATA;
			traceCurBlock = data[1];
			return 0;
		}

		// WRITE
		if ((len ==4) && ((data[0] == 0xA0))) {
			traceState = TRACE_WRITE_OK;
			traceCurBlock = data[1];
			return 0;
		}

		// HALT
		if ((len ==4) && ((data[0] == 0x50) && (data[1] == 0x00))) {
			traceState = TRACE_ERROR;  // do not decrypt the next commands
			return 0;
		}

		return 0;
	break;

	case TRACE_READ_DATA:
		if (len == 18) {
			traceState = TRACE_IDLE;

			if (isBlockTrailer(traceCurBlock)) {
				memcpy(traceCard + traceCurBlock * 16 + 6, data + 6, 4);
			} else {
				memcpy(traceCard + traceCurBlock * 16, data, 16);
			}
			if (wantSaveToEmlFile) saveTraceCard();
			return 0;
		} else {
			traceState = TRACE_ERROR;
			return 1;
		}
	break;

	case TRACE_WRITE_OK:
		if ((len == 1) && (data[0] == 0x0a)) {
			traceState = TRACE_WRITE_DATA;

			return 0;
		} else {
			traceState = TRACE_ERROR;
			return 1;
		}
	break;

	case TRACE_WRITE_DATA:
		if (len == 18) {
			traceState = TRACE_IDLE;

			memcpy(traceCard + traceCurBlock * 16, data, 16);
			if (wantSaveToEmlFile) saveTraceCard();
			return 0;
		} else {
			traceState = TRACE_ERROR;
			return 1;
		}
	break;

	case TRACE_AUTH1:
		if (len == 4) {
			traceState = TRACE_AUTH2;
			if (!traceCrypto1) {
				nt = bytes_to_num(data, 4);
			} else {
				nt_enc = bytes_to_num(data, 4);
				nt_enc_par = parity;
			}
			return 0;
		} else {
			traceState = TRACE_ERROR;
			return 1;
		}
	break;

	case TRACE_AUTH2:
		if (len == 8) {
			traceState = TRACE_AUTH_OK;

			nr_enc = bytes_to_num(data, 4);
			ar_enc = bytes_to_num(data + 4, 4);
			ar_enc_par = parity << 4;
			return 0;
		} else {
			traceState = TRACE_ERROR;
			return 1;
		}
	break;

	case TRACE_AUTH_OK:
		if (len ==4) {
			traceState = TRACE_IDLE;

			at_enc = bytes_to_num(data, 4);
			at_enc_par = parity;
			if (!traceCrypto1) {

				//  decode key here)
				ks2 = ar_enc ^ prng_successor(nt, 64);
				ks3 = at_enc ^ prng_successor(nt, 96);
				revstate = lfsr_recovery64(ks2, ks3);
				lfsr_rollback_word(revstate, 0, 0);
				lfsr_rollback_word(revstate, 0, 0);
				lfsr_rollback_word(revstate, nr_enc, 1);
				lfsr_rollback_word(revstate, uid ^ nt, 0);

				crypto1_get_lfsr(revstate, &lfsr);
				crypto1_destroy(revstate);
				ui64Key = lfsr;
				printf("key> probable key:%x%x Prng:%s ks2:%08x ks3:%08x\n", 
					(unsigned int)((lfsr & 0xFFFFFFFF00000000) >> 32), (unsigned int)(lfsr & 0xFFFFFFFF), 
					validate_prng_nonce(nt) ? "WEAK": "HARDEND",
					ks2,
					ks3);
				AddLogUint64(logHexFileName, "key> ", lfsr);
			} else {
				if (validate_prng_nonce(nt)) {
					struct Crypto1State *pcs;
					pcs = crypto1_create(ui64Key);
					uint32_t nt1 = crypto1_word(pcs, nt_enc ^ uid, 1) ^ nt_enc;
					uint32_t ar = prng_successor(nt1, 64);
					uint32_t at = prng_successor(nt1, 96);
					printf("key> nested auth uid: %08x nt: %08x nt_parity: %s ar: %08x at: %08x\n", uid, nt1, printBitsPar(&nt_enc_par, 4), ar, at);
					uint32_t nr1 = crypto1_word(pcs, nr_enc, 1) ^ nr_enc;
					uint32_t ar1 = crypto1_word(pcs, 0, 0) ^ ar_enc;
					uint32_t at1 = crypto1_word(pcs, 0, 0) ^ at_enc;
					printf("key> the same key test. nr1: %08x ar1: %08x at1: %08x \n", nr1, ar1, at1);

					if (NTParityCheck(nt1))
						printf("key> the same key test OK. key=%x%x\n", (unsigned int)((ui64Key & 0xFFFFFFFF00000000) >> 32), (unsigned int)(ui64Key & 0xFFFFFFFF));
					else
						printf("key> the same key test. check nt parity error.\n");
					
					uint32_t ntc = prng_successor(nt, 90);
					uint32_t ntx = 0;
					int ntcnt = 0;
					for (int i = 0; i < 16383; i++) {
						ntc = prng_successor(ntc, 1);
						if (NTParityCheck(ntc)){
							if (!ntcnt)
								ntx = ntc;
							ntcnt++;
						}						
					}
					if (ntcnt)
						printf("key> nt candidate=%08x nonce distance=%d candidates count=%d\n", ntx, nonce_distance(nt, ntx), ntcnt);
					else
						printf("key> don't have any nt candidate( \n");

					nt = ntx;
					ks2 = ar_enc ^ prng_successor(ntx, 64);
					ks3 = at_enc ^ prng_successor(ntx, 96);

					// decode key
					revstate = lfsr_recovery64(ks2, ks3);
					lfsr_rollback_word(revstate, 0, 0);
					lfsr_rollback_word(revstate, 0, 0);
					lfsr_rollback_word(revstate, nr_enc, 1);
					lfsr_rollback_word(revstate, uid ^ nt, 0);

					crypto1_get_lfsr(revstate, &lfsr);
					crypto1_destroy(revstate);
					ui64Key = lfsr;
					printf("key> probable key:%x%x  ks2:%08x ks3:%08x\n", 
						(unsigned int)((lfsr & 0xFFFFFFFF00000000) >> 32), (unsigned int)(lfsr & 0xFFFFFFFF),
						ks2,
						ks3);
					AddLogUint64(logHexFileName, "key> ", lfsr);
				} else {				
					printf("key> hardnested not implemented!\n");
				
					crypto1_destroy(traceCrypto1);

					// not implemented
					traceState = TRACE_ERROR;
				}
			}

			int blockShift = ((traceCurBlock & 0xFC) + 3) * 16;
			if (isBlockEmpty((traceCurBlock & 0xFC) + 3)) memcpy(traceCard + blockShift + 6, trailerAccessBytes, 4);

			if (traceCurKey) {
				num_to_bytes(lfsr, 6, traceCard + blockShift + 10);
			} else {
				num_to_bytes(lfsr, 6, traceCard + blockShift);
			}
			if (wantSaveToEmlFile) saveTraceCard();

			if (traceCrypto1) {
				crypto1_destroy(traceCrypto1);
			}

			// set cryptosystem state
			traceCrypto1 = lfsr_recovery64(ks2, ks3);
			return 0;
		} else {
			traceState = TRACE_ERROR;
			return 1;
		}
	break;

	default:
		traceState = TRACE_ERROR;
		return 1;
	}

	return 0;
}

// DECODING

int tryDecryptWord(uint32_t nt, uint32_t ar_enc, uint32_t at_enc, uint8_t *data, int len){
	/*
	uint32_t nt;      // tag challenge
	uint32_t ar_enc;  // encrypted reader response
	uint32_t at_enc;  // encrypted tag response
	*/
	if (traceCrypto1) {
		crypto1_destroy(traceCrypto1);
	}
	ks2 = ar_enc ^ prng_successor(nt, 64);
	ks3 = at_enc ^ prng_successor(nt, 96);
	traceCrypto1 = lfsr_recovery64(ks2, ks3);

	mf_crypto1_decrypt(traceCrypto1, data, len, 0);

	PrintAndLog("Decrypted data: [%s]", sprint_hex(data,len) );
	crypto1_destroy(traceCrypto1);
	return 0;
}

/** validate_prng_nonce
 * Determine if nonce is deterministic. ie: Suspectable to Darkside attack.
 * returns
 *   true = weak prng
 *   false = hardend prng
 */
bool validate_prng_nonce(uint32_t nonce) {
	uint16_t *dist = 0;
	uint16_t x, i;

	dist = malloc(2 << 16);
	if(!dist)
		return -1;

	// init prng table:
	for (x = i = 1; i; ++i) {
		dist[(x & 0xff) << 8 | x >> 8] = i;
		x = x >> 1 | (x ^ x >> 2 ^ x >> 3 ^ x >> 5) << 15;
	}
	
	uint32_t res = (65535 - dist[nonce >> 16] + dist[nonce & 0xffff]) % 65535;
	
	free(dist);	
	return (res == 16);
}

/* Detect Tag Prng, 
* function performs a partial AUTH,  where it tries to authenticate against block0, key A, but only collects tag nonce.
* the tag nonce is check to see if it has a predictable PRNG.
* @returns 
*	TRUE if tag uses WEAK prng (ie Now the NACK bug also needs to be present for Darkside attack)
*   FALSE is tag uses HARDEND prng (ie hardnested attack possible, with known key)
*/
int DetectClassicPrng(void){

	UsbCommand resp, respA;	
	uint8_t cmd[] = {0x60, 0x00}; // MIFARE_AUTH_KEYA
	uint32_t flags = ISO14A_CONNECT | ISO14A_RAW | ISO14A_APPEND_CRC | ISO14A_NO_RATS;
	
	UsbCommand c = {CMD_READER_ISO_14443a, {flags, sizeof(cmd), 0}};
	memcpy(c.d.asBytes, cmd, sizeof(cmd));

	clearCommandBuffer();
	SendCommand(&c);
	if (!WaitForResponseTimeout(CMD_ACK, &resp, 2000)) {
        PrintAndLog("PRNG UID: Reply timeout.");
		return -1;
	}
	
	// if select tag failed.
	if (resp.arg[0] == 0) {
		PrintAndLog("PRNG error: selecting tag failed, can't detect prng.");
		return -1;
	}
	
	if (!WaitForResponseTimeout(CMD_ACK, &respA, 5000)) {
        PrintAndLog("PRNG data: Reply timeout.");
		return -1;
	}

	// check respA
	if (respA.arg[0] != 4) {
		PrintAndLog("PRNG data error: Wrong length: %d", respA.arg[0]);
		return -1;
	}

	uint32_t nonce = bytes_to_num(respA.d.asBytes, respA.arg[0]);
	return validate_prng_nonce(nonce);
}
