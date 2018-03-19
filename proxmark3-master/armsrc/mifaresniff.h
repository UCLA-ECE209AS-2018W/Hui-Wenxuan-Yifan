//-----------------------------------------------------------------------------
// Merlok - June 2012
//
// This code is licensed to you under the terms of the GNU GPL, version 2 or,
// at your option, any later version. See the LICENSE.txt file for the text of
// the license.
//-----------------------------------------------------------------------------
// Routines to support mifare classic sniffer.
//-----------------------------------------------------------------------------

#ifndef __MIFARESNIFF_H
#define __MIFARESNIFF_H

#include <stdint.h>
#include <stdbool.h>
#include "util.h"

#define SNF_INIT				0
#define SNF_NO_FIELD			1
#define SNF_WUPREQ				2
#define SNF_ATQA				3
#define SNF_ANTICOL1			4
#define SNF_UID1				5
#define SNF_ANTICOL2			6
#define SNF_UID2				7
#define SNF_SAK					8
#define SNF_CARD_IDLE			9
#define SNF_CARD_CMD			10
#define SNF_CARD_RESP			11
#define SNF_MAGIC_WUPC2			12

#define SNF_UID_4				0
#define SNF_UID_7				0

bool MfSniffInit(void);
bool RAMFUNC MfSniffLogic(const uint8_t *data, uint16_t len, uint8_t *parity, uint16_t bitCnt, bool reader);
bool RAMFUNC MfSniffSend(uint16_t maxTimeoutMs);
bool intMfSniffSend();
bool MfSniffEnd(void);

#endif
