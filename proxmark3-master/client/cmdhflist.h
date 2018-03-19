//-----------------------------------------------------------------------------
// Copyright (C) Merlok - 2017
//
// This code is licensed to you under the terms of the GNU GPL, version 2 or,
// at your option, any later version. See the LICENSE.txt file for the text of
// the license.
//-----------------------------------------------------------------------------
// Command: hf mf list. It shows data from arm buffer.
//-----------------------------------------------------------------------------
#ifndef CMDHFLIST_H
#define CMDHFLIST_H

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
	uint32_t uid;       // UID
	uint32_t nt;        // tag challenge
	uint32_t nt_enc;    // encrypted tag challenge
	uint8_t nt_enc_par; // encrypted tag challenge parity
	uint32_t nr_enc;    // encrypted reader challenge
	uint32_t ar_enc;    // encrypted reader response
	uint8_t ar_enc_par; // encrypted reader response parity
	uint32_t at_enc;    // encrypted tag response
	uint8_t at_enc_par; // encrypted tag response parity
	bool first_auth;    // is first authentication
	uint32_t ks2;		// ar ^ ar_enc
	uint32_t ks3;       // at ^ at_enc
} TAuthData;
extern void ClearAuthData();

extern uint8_t iso14443A_CRC_check(bool isResponse, uint8_t* data, uint8_t len);
extern uint8_t mifare_CRC_check(bool isResponse, uint8_t* data, uint8_t len);
extern void annotateIclass(char *exp, size_t size, uint8_t* cmd, uint8_t cmdsize);
extern void annotateIso15693(char *exp, size_t size, uint8_t* cmd, uint8_t cmdsize);
extern void annotateTopaz(char *exp, size_t size, uint8_t* cmd, uint8_t cmdsize);
extern void annotateIso14443b(char *exp, size_t size, uint8_t* cmd, uint8_t cmdsize);
extern void annotateIso14443a(char *exp, size_t size, uint8_t* cmd, uint8_t cmdsize);
extern void annotateMifare(char *exp, size_t size, uint8_t* cmd, uint8_t cmdsize, uint8_t* parity, uint8_t paritysize, bool isResponse);
extern bool DecodeMifareData(uint8_t *cmd, uint8_t cmdsize, uint8_t *parity, bool isResponse, uint8_t *mfData, size_t *mfDataLen);
extern bool NTParityChk(TAuthData *ad, uint32_t ntx);
extern bool NestedCheckKey(uint64_t key, TAuthData *ad, uint8_t *cmd, uint8_t cmdsize, uint8_t *parity);
extern bool CheckCrypto1Parity(uint8_t *cmd_enc, uint8_t cmdsize, uint8_t *cmd, uint8_t *parity_enc);
extern uint64_t GetCrypto1ProbableKey(TAuthData *ad);

#endif // CMDHFLIST
