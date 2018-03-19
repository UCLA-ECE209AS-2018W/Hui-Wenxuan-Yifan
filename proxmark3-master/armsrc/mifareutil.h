//-----------------------------------------------------------------------------
// Merlok, May 2011
// Many authors, that makes it possible
//
// This code is licensed to you under the terms of the GNU GPL, version 2 or,
// at your option, any later version. See the LICENSE.txt file for the text of
// the license.
//-----------------------------------------------------------------------------
// code for work with mifare cards.
//-----------------------------------------------------------------------------

#ifndef __MIFAREUTIL_H
#define __MIFAREUTIL_H

#include <stdint.h>
#include <stdbool.h>

#include "crapto1/crapto1.h"
#include "usb_cdc.h"

// mifare authentication
#define CRYPT_NONE    0
#define CRYPT_ALL     1
#define CRYPT_REQUEST 2
#define AUTH_FIRST    0	
#define AUTH_NESTED   2

// mifare 4bit card answers
#define CARD_ACK      0x0A  // 1010 - ACK
#define CARD_NACK_NA  0x04  // 0100 - NACK, not allowed (command not allowed)
#define CARD_NACK_TR  0x05  // 0101 - NACK, transmission error

// reader voltage field detector
#define MF_MINFIELDV      4000

// debug
// 0 - no debug messages 1 - error messages 2 - all messages 4 - extended debug mode
#define MF_DBG_NONE          0
#define MF_DBG_ERROR         1
#define MF_DBG_ALL           2
#define MF_DBG_EXTENDED      4

extern int MF_DBGLEVEL;

//functions
int mifare_sendcmd(uint8_t cmd, uint8_t *data, uint8_t data_size, uint8_t* answer, uint8_t *answer_parity, uint32_t *timing);
int mifare_sendcmd_short(struct Crypto1State *pcs, uint8_t crypted, uint8_t cmd, uint8_t data, uint8_t* answer, uint8_t *answer_parity, uint32_t *timing);

// mifare classic
int mifare_classic_auth(struct Crypto1State *pcs, uint32_t uid, uint8_t blockNo, uint8_t keyType, uint64_t ui64Key, uint8_t isNested);
int mifare_classic_authex(struct Crypto1State *pcs, uint32_t uid, uint8_t blockNo, uint8_t keyType, uint64_t ui64Key, uint8_t isNested, uint32_t * ntptr, uint32_t *timing);
int mifare_classic_readblock(struct Crypto1State *pcs, uint32_t uid, uint8_t blockNo, uint8_t *blockData);
int mifare_classic_halt(struct Crypto1State *pcs, uint32_t uid); 
int mifare_classic_writeblock(struct Crypto1State *pcs, uint32_t uid, uint8_t blockNo, uint8_t *blockData);

// Ultralight/NTAG...
int mifare_ul_ev1_auth(uint8_t *key, uint8_t *pack);
int mifare_ultra_auth(uint8_t *key);
int mifare_ultra_readblock(uint8_t blockNo, uint8_t *blockData);
//int mifare_ultra_writeblock_compat(uint8_t blockNo, uint8_t *blockData);
int mifare_ultra_writeblock(uint8_t blockNo, uint8_t *blockData);
int mifare_ultra_halt();

// desfire
int mifare_sendcmd_special(struct Crypto1State *pcs, uint8_t crypted, uint8_t cmd, uint8_t* data, uint8_t* answer, uint8_t *answer_parity, uint32_t *timing);
int mifare_sendcmd_special2(struct Crypto1State *pcs, uint8_t crypted, uint8_t cmd, uint8_t* data, uint8_t* answer,uint8_t *answer_parity, uint32_t *timing);
int mifare_desfire_des_auth1(uint32_t uid, uint8_t *blockData);
int mifare_desfire_des_auth2(uint32_t uid, uint8_t *key, uint8_t *blockData);

// crypto functions
void mf_crypto1_decrypt(struct Crypto1State *pcs, uint8_t *receivedCmd, int len);
void mf_crypto1_decryptEx(struct Crypto1State *pcs, uint8_t *data_in, int len, uint8_t *data_out);
void mf_crypto1_encrypt(struct Crypto1State *pcs, uint8_t *data, uint16_t len, uint8_t *par);
uint8_t mf_crypto1_encrypt4bit(struct Crypto1State *pcs, uint8_t data);

// Mifare memory structure
uint8_t NumBlocksPerSector(uint8_t sectorNo);
uint8_t FirstBlockOfSector(uint8_t sectorNo);
bool IsSectorTrailer(uint8_t blockNo);
uint8_t SectorTrailer(uint8_t blockNo);

// emulator functions
void emlClearMem(void);
void emlSetMem(uint8_t *data, int blockNum, int blocksCount);
void emlGetMem(uint8_t *data, int blockNum, int blocksCount);
void emlGetMemBt(uint8_t *data, int bytePtr, int byteCount);
uint64_t emlGetKey(int sectorNum, int keyType);
int emlGetValBl(uint32_t *blReg, uint8_t *blBlock, int blockNum);
int emlSetValBl(uint32_t blReg, uint8_t blBlock, int blockNum);
int emlCheckValBl(int blockNum);

// mifare check keys
typedef uint8_t TKeyIndex[2][40];
int MifareChkBlockKey(uint8_t *uid, uint32_t *cuid, uint8_t *cascade_levels, uint64_t ui64Key, uint8_t blockNo, uint8_t keyType, uint8_t debugLevel);
int MifareChkBlockKeys(uint8_t *keys, uint8_t keyCount, uint8_t blockNo, uint8_t keyType, uint8_t debugLevel);
int MifareMultisectorChk(uint8_t *keys, uint8_t keyCount, uint8_t SectorCount, uint8_t keyType, uint8_t debugLevel, TKeyIndex *keyIndex);

#endif
