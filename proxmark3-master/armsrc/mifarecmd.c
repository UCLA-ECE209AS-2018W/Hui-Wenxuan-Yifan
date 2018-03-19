//-----------------------------------------------------------------------------
// Merlok - June 2011, 2012
// Gerhard de Koning Gans - May 2008
// Hagen Fritsch - June 2010
// Midnitesnake - Dec 2013
// Andy Davies  - Apr 2014
// Iceman - May 2014
//
// This code is licensed to you under the terms of the GNU GPL, version 2 or,
// at your option, any later version. See the LICENSE.txt file for the text of
// the license.
//-----------------------------------------------------------------------------
// Routines to support ISO 14443 type A.
//-----------------------------------------------------------------------------

#include "mifarecmd.h"

#include "apps.h"
#include "util.h"
#include "parity.h"
#include "crc.h"

#define HARDNESTED_AUTHENTICATION_TIMEOUT 848			// card times out 1ms after wrong authentication (according to NXP documentation)
#define HARDNESTED_PRE_AUTHENTICATION_LEADTIME 400		// some (non standard) cards need a pause after select before they are ready for first authentication 

// the block number for the ISO14443-4 PCB
static uint8_t pcb_blocknum = 0;
// Deselect card by sending a s-block. the crc is precalced for speed
static  uint8_t deselect_cmd[] = {0xc2,0xe0,0xb4};

//-----------------------------------------------------------------------------
// Select, Authenticate, Read a MIFARE tag.
// read block
//-----------------------------------------------------------------------------
void MifareReadBlock(uint8_t arg0, uint8_t arg1, uint8_t arg2, uint8_t *datain)
{
  // params
	uint8_t blockNo = arg0;
	uint8_t keyType = arg1;
	uint64_t ui64Key = 0;
	ui64Key = bytes_to_num(datain, 6);

	// variables
	byte_t isOK = 0;
	byte_t dataoutbuf[16];
	uint8_t uid[10];
	uint32_t cuid;
	struct Crypto1State mpcs = {0, 0};
	struct Crypto1State *pcs;
	pcs = &mpcs;

	iso14443a_setup(FPGA_HF_ISO14443A_READER_LISTEN);

	clear_trace();

	LED_A_ON();
	LED_B_OFF();
	LED_C_OFF();

	while (true) {
		if(!iso14443a_select_card(uid, NULL, &cuid, true, 0, true)) {
			if (MF_DBGLEVEL >= 1)	Dbprintf("Can't select card");
			break;
		};

		if(mifare_classic_auth(pcs, cuid, blockNo, keyType, ui64Key, AUTH_FIRST)) {
			if (MF_DBGLEVEL >= 1)	Dbprintf("Auth error");
			break;
		};

		if(mifare_classic_readblock(pcs, cuid, blockNo, dataoutbuf)) {
			if (MF_DBGLEVEL >= 1)	Dbprintf("Read block error");
			break;
		};

		if(mifare_classic_halt(pcs, cuid)) {
			if (MF_DBGLEVEL >= 1)	Dbprintf("Halt error");
			break;
		};

		isOK = 1;
		break;
	}

	//  ----------------------------- crypto1 destroy
	crypto1_destroy(pcs);

	if (MF_DBGLEVEL >= 2)	DbpString("READ BLOCK FINISHED");

	LED_B_ON();
	cmd_send(CMD_ACK,isOK,0,0,dataoutbuf,16);
	LED_B_OFF();

	FpgaWriteConfWord(FPGA_MAJOR_MODE_OFF);
	LEDsoff();
}

void MifareUC_Auth(uint8_t arg0, uint8_t *keybytes){

	bool turnOffField = (arg0 == 1);

	LED_A_ON(); LED_B_OFF(); LED_C_OFF();

	iso14443a_setup(FPGA_HF_ISO14443A_READER_LISTEN);

	clear_trace();

	if(!iso14443a_select_card(NULL, NULL, NULL, true, 0, true)) {
		if (MF_DBGLEVEL >= MF_DBG_ERROR) Dbprintf("Can't select card");
		OnError(0);
		return;
	};

	if(!mifare_ultra_auth(keybytes)){
		if (MF_DBGLEVEL >= MF_DBG_ERROR) Dbprintf("Authentication failed");
		OnError(1);
		return;
	}

	if (turnOffField) {
		FpgaWriteConfWord(FPGA_MAJOR_MODE_OFF);
		LEDsoff();
	}
	cmd_send(CMD_ACK,1,0,0,0,0);
}

// Arg0 = BlockNo,
// Arg1 = UsePwd bool
// datain = PWD bytes,
void MifareUReadBlock(uint8_t arg0, uint8_t arg1, uint8_t *datain)
{
	uint8_t blockNo = arg0;
	byte_t dataout[16] = {0x00};
	bool useKey = (arg1 == 1); //UL_C
	bool usePwd = (arg1 == 2); //UL_EV1/NTAG

	LEDsoff();
	LED_A_ON();
	iso14443a_setup(FPGA_HF_ISO14443A_READER_LISTEN);

	clear_trace();

	int len = iso14443a_select_card(NULL, NULL, NULL, true, 0, true);
	if(!len) {
		if (MF_DBGLEVEL >= MF_DBG_ERROR) Dbprintf("Can't select card (RC:%02X)",len);
		OnError(1);
		return;
	}

	// UL-C authentication
	if ( useKey ) {
		uint8_t key[16] = {0x00};
		memcpy(key, datain, sizeof(key) );

		if ( !mifare_ultra_auth(key) ) {
			OnError(1);
			return;
		}
	}

	// UL-EV1 / NTAG authentication
	if ( usePwd ) {
		uint8_t pwd[4] = {0x00};
		memcpy(pwd, datain, 4);
		uint8_t pack[4] = {0,0,0,0};
		if (!mifare_ul_ev1_auth(pwd, pack)) {
			OnError(1);
			return;
		}
	}

	if( mifare_ultra_readblock(blockNo, dataout) ) {
		if (MF_DBGLEVEL >= MF_DBG_ERROR) Dbprintf("Read block error");
		OnError(2);
		return;
	}

	if( mifare_ultra_halt() ) {
		if (MF_DBGLEVEL >= MF_DBG_ERROR) Dbprintf("Halt error");
		OnError(3);
		return;
	}

	cmd_send(CMD_ACK,1,0,0,dataout,16);
	FpgaWriteConfWord(FPGA_MAJOR_MODE_OFF);
	LEDsoff();
}

//-----------------------------------------------------------------------------
// Select, Authenticate, Read a MIFARE tag.
// read sector (data = 4 x 16 bytes = 64 bytes, or 16 x 16 bytes = 256 bytes)
//-----------------------------------------------------------------------------
void MifareReadSector(uint8_t arg0, uint8_t arg1, uint8_t arg2, uint8_t *datain)
{
  // params
	uint8_t sectorNo = arg0;
	uint8_t keyType = arg1;
	uint64_t ui64Key = 0;
	ui64Key = bytes_to_num(datain, 6);

	// variables
	byte_t isOK = 0;
	byte_t dataoutbuf[16 * 16];
	uint8_t uid[10];
	uint32_t cuid;
	struct Crypto1State mpcs = {0, 0};
	struct Crypto1State *pcs;
	pcs = &mpcs;

	iso14443a_setup(FPGA_HF_ISO14443A_READER_LISTEN);

	clear_trace();

	LED_A_ON();
	LED_B_OFF();
	LED_C_OFF();

	isOK = 1;
	if(!iso14443a_select_card(uid, NULL, &cuid, true, 0, true)) {
		isOK = 0;
		if (MF_DBGLEVEL >= 1)	Dbprintf("Can't select card");
	}


	if(isOK && mifare_classic_auth(pcs, cuid, FirstBlockOfSector(sectorNo), keyType, ui64Key, AUTH_FIRST)) {
		isOK = 0;
		if (MF_DBGLEVEL >= 1)	Dbprintf("Auth error");
	}

	for (uint8_t blockNo = 0; isOK && blockNo < NumBlocksPerSector(sectorNo); blockNo++) {
		if(mifare_classic_readblock(pcs, cuid, FirstBlockOfSector(sectorNo) + blockNo, dataoutbuf + 16 * blockNo)) {
			isOK = 0;
			if (MF_DBGLEVEL >= 1)	Dbprintf("Read sector %2d block %2d error", sectorNo, blockNo);
			break;
		}
	}

	if(mifare_classic_halt(pcs, cuid)) {
		if (MF_DBGLEVEL >= 1)	Dbprintf("Halt error");
	}

	//  ----------------------------- crypto1 destroy
	crypto1_destroy(pcs);

	if (MF_DBGLEVEL >= 2) DbpString("READ SECTOR FINISHED");

	LED_B_ON();
	cmd_send(CMD_ACK,isOK,0,0,dataoutbuf,16*NumBlocksPerSector(sectorNo));
	LED_B_OFF();

	// Thats it...
	FpgaWriteConfWord(FPGA_MAJOR_MODE_OFF);
	LEDsoff();
}

// arg0 = blockNo (start)
// arg1 = Pages (number of blocks)
// arg2 = useKey
// datain = KEY bytes
void MifareUReadCard(uint8_t arg0, uint16_t arg1, uint8_t arg2, uint8_t *datain)
{
	LEDsoff();
	LED_A_ON();
	iso14443a_setup(FPGA_HF_ISO14443A_READER_LISTEN);

	// free eventually allocated BigBuf memory
	BigBuf_free();
	clear_trace();

	// params
	uint8_t blockNo = arg0;
	uint16_t blocks = arg1;
	bool useKey = (arg2 == 1); //UL_C
	bool usePwd = (arg2 == 2); //UL_EV1/NTAG
	uint32_t countblocks = 0;
	uint8_t *dataout = BigBuf_malloc(CARD_MEMORY_SIZE);
	if (dataout == NULL){
		Dbprintf("out of memory");
		OnError(1);
		return;
	}

	int len = iso14443a_select_card(NULL, NULL, NULL, true, 0, true);
	if (!len) {
		if (MF_DBGLEVEL >= MF_DBG_ERROR) Dbprintf("Can't select card (RC:%d)",len);
		OnError(1);
		return;
	}

	// UL-C authentication
	if ( useKey ) {
		uint8_t key[16] = {0x00};
		memcpy(key, datain, sizeof(key) );

		if ( !mifare_ultra_auth(key) ) {
			OnError(1);
			return;
		}
	}

	// UL-EV1 / NTAG authentication
	if (usePwd) {
		uint8_t pwd[4] = {0x00};
		memcpy(pwd, datain, sizeof(pwd));
		uint8_t pack[4] = {0,0,0,0};

		if (!mifare_ul_ev1_auth(pwd, pack)){
			OnError(1);
			return;
		}
	}

	for (int i = 0; i < blocks; i++){
		if ((i*4) + 4 >= CARD_MEMORY_SIZE) {
			Dbprintf("Data exceeds buffer!!");
			break;
		}

		len = mifare_ultra_readblock(blockNo + i, dataout + 4 * i);

		if (len) {
			if (MF_DBGLEVEL >= MF_DBG_ERROR) Dbprintf("Read block %d error",i);
			// if no blocks read - error out
			if (i==0){
				OnError(2);
				return;
			} else {
				//stop at last successful read block and return what we got
				break;
			}
		} else {
			countblocks++;
		}
	}

	len = mifare_ultra_halt();
	if (len) {
		if (MF_DBGLEVEL >= MF_DBG_ERROR) Dbprintf("Halt error");
		OnError(3);
		return;
	}

	if (MF_DBGLEVEL >= MF_DBG_EXTENDED) Dbprintf("Blocks read %d", countblocks);

	countblocks *= 4;

	cmd_send(CMD_ACK, 1, countblocks, BigBuf_max_traceLen(), 0, 0);
	FpgaWriteConfWord(FPGA_MAJOR_MODE_OFF);
	LEDsoff();
	BigBuf_free();
}

//-----------------------------------------------------------------------------
// Select, Authenticate, Write a MIFARE tag.
// read block
//-----------------------------------------------------------------------------
void MifareWriteBlock(uint8_t arg0, uint8_t arg1, uint8_t arg2, uint8_t *datain)
{
	// params
	uint8_t blockNo = arg0;
	uint8_t keyType = arg1;
	uint64_t ui64Key = 0;
	byte_t blockdata[16];

	ui64Key = bytes_to_num(datain, 6);
	memcpy(blockdata, datain + 10, 16);

	// variables
	byte_t isOK = 0;
	uint8_t uid[10];
	uint32_t cuid;
	struct Crypto1State mpcs = {0, 0};
	struct Crypto1State *pcs;
	pcs = &mpcs;

	iso14443a_setup(FPGA_HF_ISO14443A_READER_LISTEN);

	clear_trace();

	LED_A_ON();
	LED_B_OFF();
	LED_C_OFF();

	while (true) {
			if(!iso14443a_select_card(uid, NULL, &cuid, true, 0, true)) {
			if (MF_DBGLEVEL >= 1)	Dbprintf("Can't select card");
			break;
		};

		if(mifare_classic_auth(pcs, cuid, blockNo, keyType, ui64Key, AUTH_FIRST)) {
			if (MF_DBGLEVEL >= 1)	Dbprintf("Auth error");
			break;
		};

		if(mifare_classic_writeblock(pcs, cuid, blockNo, blockdata)) {
			if (MF_DBGLEVEL >= 1)	Dbprintf("Write block error");
			break;
		};

		if(mifare_classic_halt(pcs, cuid)) {
			if (MF_DBGLEVEL >= 1)	Dbprintf("Halt error");
			break;
		};

		isOK = 1;
		break;
	}

	//  ----------------------------- crypto1 destroy
	crypto1_destroy(pcs);

	if (MF_DBGLEVEL >= 2)	DbpString("WRITE BLOCK FINISHED");

	LED_B_ON();
	cmd_send(CMD_ACK,isOK,0,0,0,0);
	LED_B_OFF();


	// Thats it...
	FpgaWriteConfWord(FPGA_MAJOR_MODE_OFF);
	LEDsoff();
}

/* // Command not needed but left for future testing
void MifareUWriteBlockCompat(uint8_t arg0, uint8_t *datain)
{
	uint8_t blockNo = arg0;
	byte_t blockdata[16] = {0x00};

	memcpy(blockdata, datain, 16);

	uint8_t uid[10] = {0x00};

	LED_A_ON(); LED_B_OFF(); LED_C_OFF();

	clear_trace();
	iso14443a_setup(FPGA_HF_ISO14443A_READER_LISTEN);

	if(!iso14443a_select_card(uid, NULL, NULL, true, 0)) {
		if (MF_DBGLEVEL >= 1)   Dbprintf("Can't select card");
		OnError(0);
		return;
	};

	if(mifare_ultra_writeblock_compat(blockNo, blockdata)) {
		if (MF_DBGLEVEL >= 1)   Dbprintf("Write block error");
		OnError(0);
		return;	};

	if(mifare_ultra_halt()) {
		if (MF_DBGLEVEL >= 1)   Dbprintf("Halt error");
		OnError(0);
		return;
	};

	if (MF_DBGLEVEL >= 2)   DbpString("WRITE BLOCK FINISHED");

	cmd_send(CMD_ACK,1,0,0,0,0);
	FpgaWriteConfWord(FPGA_MAJOR_MODE_OFF);
	LEDsoff();
}
*/

// Arg0   : Block to write to.
// Arg1   : 0 = use no authentication.
//          1 = use 0x1A authentication.
//          2 = use 0x1B authentication.
// datain : 4 first bytes is data to be written.
//        : 4/16 next bytes is authentication key.
void MifareUWriteBlock(uint8_t arg0, uint8_t arg1, uint8_t *datain)
{
	uint8_t blockNo = arg0;
	bool useKey = (arg1 == 1); //UL_C
	bool usePwd = (arg1 == 2); //UL_EV1/NTAG
	byte_t blockdata[4] = {0x00};

	memcpy(blockdata, datain,4);

	LEDsoff();
	LED_A_ON();
	iso14443a_setup(FPGA_HF_ISO14443A_READER_LISTEN);

	clear_trace();

	if(!iso14443a_select_card(NULL, NULL, NULL, true, 0, true)) {
		if (MF_DBGLEVEL >= 1) Dbprintf("Can't select card");
		OnError(0);
		return;
	};

	// UL-C authentication
	if ( useKey ) {
		uint8_t key[16] = {0x00};
		memcpy(key, datain+4, sizeof(key) );

		if ( !mifare_ultra_auth(key) ) {
			OnError(1);
			return;
		}
	}

	// UL-EV1 / NTAG authentication
	if (usePwd) {
		uint8_t pwd[4] = {0x00};
		memcpy(pwd, datain+4, 4);
		uint8_t pack[4] = {0,0,0,0};
		if (!mifare_ul_ev1_auth(pwd, pack)) {
			OnError(1);
			return;
		}
	}

	if(mifare_ultra_writeblock(blockNo, blockdata)) {
		if (MF_DBGLEVEL >= 1) Dbprintf("Write block error");
		OnError(0);
		return;
	};

	if(mifare_ultra_halt()) {
		if (MF_DBGLEVEL >= 1) Dbprintf("Halt error");
		OnError(0);
		return;
	};

	if (MF_DBGLEVEL >= 2) DbpString("WRITE BLOCK FINISHED");

	cmd_send(CMD_ACK,1,0,0,0,0);
	FpgaWriteConfWord(FPGA_MAJOR_MODE_OFF);
	LEDsoff();
}

void MifareUSetPwd(uint8_t arg0, uint8_t *datain){

	uint8_t pwd[16] = {0x00};
	byte_t blockdata[4] = {0x00};

	memcpy(pwd, datain, 16);

	LED_A_ON(); LED_B_OFF(); LED_C_OFF();
	iso14443a_setup(FPGA_HF_ISO14443A_READER_LISTEN);

	clear_trace();

	if(!iso14443a_select_card(NULL, NULL, NULL, true, 0, true)) {
		if (MF_DBGLEVEL >= 1) Dbprintf("Can't select card");
		OnError(0);
		return;
	};

	blockdata[0] = pwd[7];
	blockdata[1] = pwd[6];
	blockdata[2] = pwd[5];
	blockdata[3] = pwd[4];
	if(mifare_ultra_writeblock( 44, blockdata)) {
		if (MF_DBGLEVEL >= 1) Dbprintf("Write block error");
		OnError(44);
		return;
	};

	blockdata[0] = pwd[3];
	blockdata[1] = pwd[2];
	blockdata[2] = pwd[1];
	blockdata[3] = pwd[0];
	if(mifare_ultra_writeblock( 45, blockdata)) {
		if (MF_DBGLEVEL >= 1) Dbprintf("Write block error");
		OnError(45);
		return;
	};

	blockdata[0] = pwd[15];
	blockdata[1] = pwd[14];
	blockdata[2] = pwd[13];
	blockdata[3] = pwd[12];
	if(mifare_ultra_writeblock( 46, blockdata)) {
		if (MF_DBGLEVEL >= 1) Dbprintf("Write block error");
		OnError(46);
		return;
	};

	blockdata[0] = pwd[11];
	blockdata[1] = pwd[10];
	blockdata[2] = pwd[9];
	blockdata[3] = pwd[8];
	if(mifare_ultra_writeblock( 47, blockdata)) {
		if (MF_DBGLEVEL >= 1) Dbprintf("Write block error");
		OnError(47);
		return;
	};

	if(mifare_ultra_halt()) {
		if (MF_DBGLEVEL >= 1) Dbprintf("Halt error");
		OnError(0);
		return;
	};

	cmd_send(CMD_ACK,1,0,0,0,0);
	FpgaWriteConfWord(FPGA_MAJOR_MODE_OFF);
	LEDsoff();
}

// Return 1 if the nonce is invalid else return 0
int valid_nonce(uint32_t Nt, uint32_t NtEnc, uint32_t Ks1, uint8_t *parity) {
	return ((oddparity8((Nt >> 24) & 0xFF) == ((parity[0]) ^ oddparity8((NtEnc >> 24) & 0xFF) ^ BIT(Ks1,16))) & \
	(oddparity8((Nt >> 16) & 0xFF) == ((parity[1]) ^ oddparity8((NtEnc >> 16) & 0xFF) ^ BIT(Ks1,8))) & \
	(oddparity8((Nt >> 8) & 0xFF) == ((parity[2]) ^ oddparity8((NtEnc >> 8) & 0xFF) ^ BIT(Ks1,0)))) ? 1 : 0;
}


//-----------------------------------------------------------------------------
// acquire encrypted nonces in order to perform the attack described in
// Carlo Meijer, Roel Verdult, "Ciphertext-only Cryptanalysis on Hardened
// Mifare Classic Cards" in Proceedings of the 22nd ACM SIGSAC Conference on
// Computer and Communications Security, 2015
//-----------------------------------------------------------------------------
void MifareAcquireEncryptedNonces(uint32_t arg0, uint32_t arg1, uint32_t flags, uint8_t *datain)
{
	uint64_t ui64Key = 0;
	uint8_t uid[10];
	uint32_t cuid;
	uint8_t cascade_levels = 0;
	struct Crypto1State mpcs = {0, 0};
	struct Crypto1State *pcs;
	pcs = &mpcs;
	uint8_t receivedAnswer[MAX_MIFARE_FRAME_SIZE];
	int16_t isOK = 0;
	uint8_t par_enc[1];
	uint8_t nt_par_enc = 0;
	uint8_t buf[USB_CMD_DATA_SIZE];
	uint32_t timeout;

	uint8_t blockNo = arg0 & 0xff;
	uint8_t keyType = (arg0 >> 8) & 0xff;
	uint8_t targetBlockNo = arg1 & 0xff;
	uint8_t targetKeyType = (arg1 >> 8) & 0xff;
	ui64Key = bytes_to_num(datain, 6);
	bool initialize = flags & 0x0001;
	bool slow = flags & 0x0002;
	bool field_off = flags & 0x0004;

	LED_A_ON();
	LED_C_OFF();

	if (initialize) {
		iso14443a_setup(FPGA_HF_ISO14443A_READER_LISTEN);
		clear_trace();
		set_tracing(true);
	}

	LED_C_ON();

	uint16_t num_nonces = 0;
	bool have_uid = false;
	for (uint16_t i = 0; i <= USB_CMD_DATA_SIZE - 9; ) {

		// Test if the action was cancelled
		if(BUTTON_PRESS()) {
			isOK = 2;
			field_off = true;
			break;
		}

		if (!have_uid) { // need a full select cycle to get the uid first
			iso14a_card_select_t card_info;
			if(!iso14443a_select_card(uid, &card_info, &cuid, true, 0, true)) {
				if (MF_DBGLEVEL >= 1)	Dbprintf("AcquireNonces: Can't select card (ALL)");
				continue;
			}
			switch (card_info.uidlen) {
				case 4 : cascade_levels = 1; break;
				case 7 : cascade_levels = 2; break;
				case 10: cascade_levels = 3; break;
				default: break;
			}
			have_uid = true;
		} else { // no need for anticollision. We can directly select the card
			if(!iso14443a_select_card(uid, NULL, NULL, false, cascade_levels, true)) {
				if (MF_DBGLEVEL >= 1)	Dbprintf("AcquireNonces: Can't select card (UID)");
				continue;
			}
		}

		if (slow) {
			timeout = GetCountSspClk() + HARDNESTED_PRE_AUTHENTICATION_LEADTIME;
			while(GetCountSspClk() < timeout);
		}

		uint32_t nt1;
		if (mifare_classic_authex(pcs, cuid, blockNo, keyType, ui64Key, AUTH_FIRST, &nt1, NULL)) {
			if (MF_DBGLEVEL >= 1)	Dbprintf("AcquireNonces: Auth1 error");
			continue;
		}

		// nested authentication
		uint16_t len = mifare_sendcmd_short(pcs, AUTH_NESTED, 0x60 + (targetKeyType & 0x01), targetBlockNo, receivedAnswer, par_enc, NULL);
		if (len != 4) {
			if (MF_DBGLEVEL >= 1)	Dbprintf("AcquireNonces: Auth2 error len=%d", len);
			continue;
		}

		// send an incomplete dummy response in order to trigger the card's authentication failure timeout
		uint8_t dummy_answer[1] = {0};
		ReaderTransmit(dummy_answer, 1, NULL);

		timeout = GetCountSspClk() + HARDNESTED_AUTHENTICATION_TIMEOUT;
		
		num_nonces++;
		if (num_nonces % 2) {
			memcpy(buf+i, receivedAnswer, 4);
			nt_par_enc = par_enc[0] & 0xf0;
		} else {
			nt_par_enc |= par_enc[0] >> 4;
			memcpy(buf+i+4, receivedAnswer, 4);
			memcpy(buf+i+8, &nt_par_enc, 1);
			i += 9;
		}

		// wait for the card to become ready again
		while(GetCountSspClk() < timeout);

	}

	LED_C_OFF();

	crypto1_destroy(pcs);

	LED_B_ON();
	cmd_send(CMD_ACK, isOK, cuid, num_nonces, buf, sizeof(buf));
	LED_B_OFF();

	if (MF_DBGLEVEL >= 3)	DbpString("AcquireEncryptedNonces finished");

	if (field_off) {
		FpgaWriteConfWord(FPGA_MAJOR_MODE_OFF);
		LEDsoff();
	}
}


//-----------------------------------------------------------------------------
// MIFARE nested authentication.
//
//-----------------------------------------------------------------------------
void MifareNested(uint32_t arg0, uint32_t arg1, uint32_t calibrate, uint8_t *datain)
{
	// params
	uint8_t blockNo = arg0 & 0xff;
	uint8_t keyType = (arg0 >> 8) & 0xff;
	uint8_t targetBlockNo = arg1 & 0xff;
	uint8_t targetKeyType = (arg1 >> 8) & 0xff;
	uint64_t ui64Key = 0;

	ui64Key = bytes_to_num(datain, 6);

	// variables
	uint16_t rtr, i, j, len;
	uint16_t davg;
	static uint16_t dmin, dmax;
	uint8_t uid[10];
	uint32_t cuid, nt1, nt2, nttmp, nttest, ks1;
	uint8_t par[1];
	uint32_t target_nt[2], target_ks[2];

	uint8_t par_array[4];
	uint16_t ncount = 0;
	struct Crypto1State mpcs = {0, 0};
	struct Crypto1State *pcs;
	pcs = &mpcs;
	uint8_t receivedAnswer[MAX_MIFARE_FRAME_SIZE];

	uint32_t auth1_time, auth2_time;
	static uint16_t delta_time;

	LED_A_ON();
	LED_C_OFF();
	iso14443a_setup(FPGA_HF_ISO14443A_READER_LISTEN);

	// free eventually allocated BigBuf memory
	BigBuf_free();

	if (calibrate) clear_trace();
	set_tracing(true);

	// statistics on nonce distance
	int16_t isOK = 0;
	#define NESTED_MAX_TRIES 12
	uint16_t unsuccessfull_tries = 0;
	if (calibrate) {	// for first call only. Otherwise reuse previous calibration
		LED_B_ON();
		WDT_HIT();

		davg = dmax = 0;
		dmin = 2000;
		delta_time = 0;

		for (rtr = 0; rtr < 17; rtr++) {

			// Test if the action was cancelled
			if(BUTTON_PRESS()) {
				isOK = -2;
				break;
			}

			// prepare next select. No need to power down the card.
			if(mifare_classic_halt(pcs, cuid)) {
				if (MF_DBGLEVEL >= 1)	Dbprintf("Nested: Halt error");
				rtr--;
				continue;
			}

			if(!iso14443a_select_card(uid, NULL, &cuid, true, 0, true)) {
				if (MF_DBGLEVEL >= 1)	Dbprintf("Nested: Can't select card");
				rtr--;
				continue;
			};

			auth1_time = 0;
			if(mifare_classic_authex(pcs, cuid, blockNo, keyType, ui64Key, AUTH_FIRST, &nt1, &auth1_time)) {
				if (MF_DBGLEVEL >= 1)	Dbprintf("Nested: Auth1 error");
				rtr--;
				continue;
			};

			if (delta_time) {
				auth2_time = auth1_time + delta_time;
			} else {
				auth2_time = 0;
			}
			if(mifare_classic_authex(pcs, cuid, blockNo, keyType, ui64Key, AUTH_NESTED, &nt2, &auth2_time)) {
				if (MF_DBGLEVEL >= 1)	Dbprintf("Nested: Auth2 error");
				rtr--;
				continue;
			};

			nttmp = prng_successor(nt1, 100);				//NXP Mifare is typical around 840,but for some unlicensed/compatible mifare card this can be 160
			for (i = 101; i < 1200; i++) {
				nttmp = prng_successor(nttmp, 1);
				if (nttmp == nt2) break;
			}

			if (i != 1200) {
				if (rtr != 0) {
					davg += i;
					dmin = MIN(dmin, i);
					dmax = MAX(dmax, i);
				}
				else {
					delta_time = auth2_time - auth1_time + 32;  // allow some slack for proper timing
				}
				if (MF_DBGLEVEL >= 3) Dbprintf("Nested: calibrating... ntdist=%d", i);
			} else {
				unsuccessfull_tries++;
				if (unsuccessfull_tries > NESTED_MAX_TRIES) {	// card isn't vulnerable to nested attack (random numbers are not predictable)
					isOK = -3;
				}
			}
		}

		davg = (davg + (rtr - 1)/2) / (rtr - 1);

		if (MF_DBGLEVEL >= 3) Dbprintf("rtr=%d isOK=%d min=%d max=%d avg=%d, delta_time=%d", rtr, isOK, dmin, dmax, davg, delta_time);

		dmin = davg - 2;
		dmax = davg + 2;

		LED_B_OFF();

	}
	//  -------------------------------------------------------------------------------------------------

	LED_C_ON();

	//  get crypted nonces for target sector
	for(i=0; i < 2 && !isOK; i++) { // look for exactly two different nonces

		target_nt[i] = 0;
		while(target_nt[i] == 0) { // continue until we have an unambiguous nonce

			// prepare next select. No need to power down the card.
			if(mifare_classic_halt(pcs, cuid)) {
				if (MF_DBGLEVEL >= 1)	Dbprintf("Nested: Halt error");
				continue;
			}

			if(!iso14443a_select_card(uid, NULL, &cuid, true, 0, true)) {
				if (MF_DBGLEVEL >= 1)	Dbprintf("Nested: Can't select card");
				continue;
			};

			auth1_time = 0;
			if(mifare_classic_authex(pcs, cuid, blockNo, keyType, ui64Key, AUTH_FIRST, &nt1, &auth1_time)) {
				if (MF_DBGLEVEL >= 1)	Dbprintf("Nested: Auth1 error");
				continue;
			};

			// nested authentication
			auth2_time = auth1_time + delta_time;
			len = mifare_sendcmd_short(pcs, AUTH_NESTED, 0x60 + (targetKeyType & 0x01), targetBlockNo, receivedAnswer, par, &auth2_time);
			if (len != 4) {
				if (MF_DBGLEVEL >= 1)	Dbprintf("Nested: Auth2 error len=%d", len);
				continue;
			};

			nt2 = bytes_to_num(receivedAnswer, 4);
			if (MF_DBGLEVEL >= 3) Dbprintf("Nonce#%d: Testing nt1=%08x nt2enc=%08x nt2par=%02x", i+1, nt1, nt2, par[0]);

			// Parity validity check
			for (j = 0; j < 4; j++) {
				par_array[j] = (oddparity8(receivedAnswer[j]) != ((par[0] >> (7-j)) & 0x01));
			}

			ncount = 0;
			nttest = prng_successor(nt1, dmin - 1);
			for (j = dmin; j < dmax + 1; j++) {
				nttest = prng_successor(nttest, 1);
				ks1 = nt2 ^ nttest;

				if (valid_nonce(nttest, nt2, ks1, par_array)){
					if (ncount > 0) { 		// we are only interested in disambiguous nonces, try again
						if (MF_DBGLEVEL >= 3) Dbprintf("Nonce#%d: dismissed (ambigous), ntdist=%d", i+1, j);
						target_nt[i] = 0;
						break;
					}
					target_nt[i] = nttest;
					target_ks[i] = ks1;
					ncount++;
					if (i == 1 && target_nt[1] == target_nt[0]) { // we need two different nonces
						target_nt[i] = 0;
						if (MF_DBGLEVEL >= 3) Dbprintf("Nonce#2: dismissed (= nonce#1), ntdist=%d", j);
						break;
					}
					if (MF_DBGLEVEL >= 3) Dbprintf("Nonce#%d: valid, ntdist=%d", i+1, j);
				}
			}
			if (target_nt[i] == 0 && j == dmax+1 && MF_DBGLEVEL >= 3) Dbprintf("Nonce#%d: dismissed (all invalid)", i+1);
		}
	}

	LED_C_OFF();

	//  ----------------------------- crypto1 destroy
	crypto1_destroy(pcs);

	byte_t buf[4 + 4 * 4];
	memcpy(buf, &cuid, 4);
	memcpy(buf+4, &target_nt[0], 4);
	memcpy(buf+8, &target_ks[0], 4);
	memcpy(buf+12, &target_nt[1], 4);
	memcpy(buf+16, &target_ks[1], 4);

	LED_B_ON();
	cmd_send(CMD_ACK, isOK, 0, targetBlockNo + (targetKeyType * 0x100), buf, sizeof(buf));
	LED_B_OFF();

	if (MF_DBGLEVEL >= 3)	DbpString("NESTED FINISHED");

	FpgaWriteConfWord(FPGA_MAJOR_MODE_OFF);
	LEDsoff();
}

//-----------------------------------------------------------------------------
// MIFARE check keys. key count up to 85.
//
//-----------------------------------------------------------------------------
void MifareChkKeys(uint16_t arg0, uint16_t arg1, uint8_t arg2, uint8_t *datain)
{
	uint8_t blockNo = arg0 & 0xff;
	uint8_t keyType = (arg0 >> 8) & 0xff;
	bool clearTrace = arg1 & 0x01;
	bool multisectorCheck = arg1 & 0x02;
	uint8_t set14aTimeout = (arg1 >> 8) & 0xff;
	uint8_t keyCount = arg2;

	// clear debug level
	int OLD_MF_DBGLEVEL = MF_DBGLEVEL;
	MF_DBGLEVEL = MF_DBG_NONE;

	LED_A_ON();
	LED_B_OFF();
	LED_C_OFF();
	iso14443a_setup(FPGA_HF_ISO14443A_READER_LISTEN);

	if (clearTrace) clear_trace();
	set_tracing(true);

	if (set14aTimeout){
		iso14a_set_timeout(set14aTimeout * 10); // timeout: ms = x/106  35-minimum, 50-OK 106-recommended 500-safe
	}
	
	if (multisectorCheck) {
		TKeyIndex keyIndex = {{0}};
		uint8_t sectorCnt = blockNo;
		int res = MifareMultisectorChk(datain, keyCount, sectorCnt, keyType, OLD_MF_DBGLEVEL, &keyIndex);

		LED_B_ON();
		if (res >= 0) {
			cmd_send(CMD_ACK, 1, 0, 0, keyIndex, 80);
		} else {
			cmd_send(CMD_ACK, 0, 0, 0, NULL, 0);
		}
		LED_B_OFF();
	} else {	
		int res = MifareChkBlockKeys(datain, keyCount, blockNo, keyType, OLD_MF_DBGLEVEL);
		
		LED_B_ON();
		if (res > 0) {
			cmd_send(CMD_ACK, 1, 0, 0, datain + (res - 1) * 6, 6);
		} else {
			cmd_send(CMD_ACK, 0, 0, 0, NULL, 0);
		}
		LED_B_OFF();
	}

	FpgaWriteConfWord(FPGA_MAJOR_MODE_OFF);
	LEDsoff();

	// restore debug level
	MF_DBGLEVEL = OLD_MF_DBGLEVEL;
}

//-----------------------------------------------------------------------------
// MIFARE commands set debug level
//
//-----------------------------------------------------------------------------
void MifareSetDbgLvl(uint32_t arg0, uint32_t arg1, uint32_t arg2, uint8_t *datain){
	MF_DBGLEVEL = arg0;
	Dbprintf("Debug level: %d", MF_DBGLEVEL);
}

//-----------------------------------------------------------------------------
// Work with emulator memory
//
// Note: we call FpgaDownloadAndGo(FPGA_BITSTREAM_HF) here although FPGA is not
// involved in dealing with emulator memory. But if it is called later, it might
// destroy the Emulator Memory.
//-----------------------------------------------------------------------------

void MifareEMemClr(uint32_t arg0, uint32_t arg1, uint32_t arg2, uint8_t *datain){
	FpgaDownloadAndGo(FPGA_BITSTREAM_HF);
	emlClearMem();
}

void MifareEMemSet(uint32_t arg0, uint32_t arg1, uint32_t arg2, uint8_t *datain){
	FpgaDownloadAndGo(FPGA_BITSTREAM_HF);
	emlSetMem(datain, arg0, arg1); // data, block num, blocks count
}

void MifareEMemGet(uint32_t arg0, uint32_t arg1, uint32_t arg2, uint8_t *datain){
	FpgaDownloadAndGo(FPGA_BITSTREAM_HF);
	byte_t buf[USB_CMD_DATA_SIZE];
	emlGetMem(buf, arg0, arg1); // data, block num, blocks count (max 4)

	LED_B_ON();
	cmd_send(CMD_ACK,arg0,arg1,0,buf,USB_CMD_DATA_SIZE);
	LED_B_OFF();
}

//-----------------------------------------------------------------------------
// Load a card into the emulator memory
//
//-----------------------------------------------------------------------------
void MifareECardLoad(uint32_t arg0, uint32_t arg1, uint32_t arg2, uint8_t *datain){
	uint8_t numSectors = arg0;
	uint8_t keyType = arg1;
	uint64_t ui64Key = 0;
	uint32_t cuid;
	struct Crypto1State mpcs = {0, 0};
	struct Crypto1State *pcs;
	pcs = &mpcs;

	// variables
	byte_t dataoutbuf[16];
	byte_t dataoutbuf2[16];
	uint8_t uid[10];

	LED_A_ON();
	LED_B_OFF();
	LED_C_OFF();
	iso14443a_setup(FPGA_HF_ISO14443A_READER_LISTEN);

	clear_trace();
	set_tracing(false);

	bool isOK = true;

	if(!iso14443a_select_card(uid, NULL, &cuid, true, 0, true)) {
		isOK = false;
		if (MF_DBGLEVEL >= 1)	Dbprintf("Can't select card");
	}

	for (uint8_t sectorNo = 0; isOK && sectorNo < numSectors; sectorNo++) {
		ui64Key = emlGetKey(sectorNo, keyType);
		if (sectorNo == 0){
			if(isOK && mifare_classic_auth(pcs, cuid, FirstBlockOfSector(sectorNo), keyType, ui64Key, AUTH_FIRST)) {
				isOK = false;
				if (MF_DBGLEVEL >= 1)	Dbprintf("Sector[%2d]. Auth error", sectorNo);
				break;
			}
		} else {
			if(isOK && mifare_classic_auth(pcs, cuid, FirstBlockOfSector(sectorNo), keyType, ui64Key, AUTH_NESTED)) {
				isOK = false;
				if (MF_DBGLEVEL >= 1)	Dbprintf("Sector[%2d]. Auth nested error", sectorNo);
				break;
			}
		}

		for (uint8_t blockNo = 0; isOK && blockNo < NumBlocksPerSector(sectorNo); blockNo++) {
			if(isOK && mifare_classic_readblock(pcs, cuid, FirstBlockOfSector(sectorNo) + blockNo, dataoutbuf)) {
				isOK = false;
				if (MF_DBGLEVEL >= 1)	Dbprintf("Error reading sector %2d block %2d", sectorNo, blockNo);
				break;
			};
			if (isOK) {
				if (blockNo < NumBlocksPerSector(sectorNo) - 1) {
					emlSetMem(dataoutbuf, FirstBlockOfSector(sectorNo) + blockNo, 1);
				} else {	// sector trailer, keep the keys, set only the AC
					emlGetMem(dataoutbuf2, FirstBlockOfSector(sectorNo) + blockNo, 1);
					memcpy(&dataoutbuf2[6], &dataoutbuf[6], 4);
					emlSetMem(dataoutbuf2,  FirstBlockOfSector(sectorNo) + blockNo, 1);
				}
			}
		}

	}

	if(mifare_classic_halt(pcs, cuid)) {
		if (MF_DBGLEVEL >= 1)	Dbprintf("Halt error");
	};

	//  ----------------------------- crypto1 destroy
	crypto1_destroy(pcs);

	FpgaWriteConfWord(FPGA_MAJOR_MODE_OFF);
	LEDsoff();

	if (MF_DBGLEVEL >= 2) DbpString("EMUL FILL SECTORS FINISHED");

}


//-----------------------------------------------------------------------------
// Work with "magic Chinese" card (email him: ouyangweidaxian@live.cn)
//
//-----------------------------------------------------------------------------

static bool isBlockTrailer(int blockN) {
	if (blockN >= 0 && blockN < 128) {
		return ((blockN & 0x03) == 0x03);
	}
	if (blockN >= 128 && blockN <= 256) {
		return ((blockN & 0x0F) == 0x0F);
	}
	return FALSE;
}

void MifareCWipe(uint32_t arg0, uint32_t arg1, uint32_t arg2, uint8_t *datain){
	// var
	byte_t isOK = 0;
	uint32_t numBlocks = arg0;
	// cmdParams:
	// bit 0 - wipe gen1a
	// bit 1 - fill card with default data
	// bit 2 - gen1a = 0, gen1b = 1
	uint8_t cmdParams = arg1;
	bool needWipe = cmdParams & 0x01;
	bool needFill = cmdParams & 0x02;
	bool gen1b = cmdParams & 0x04;
	
	uint8_t receivedAnswer[MAX_MIFARE_FRAME_SIZE];
	uint8_t receivedAnswerPar[MAX_MIFARE_PARITY_SIZE];
	
	uint8_t block0[16] = {0x01, 0x02, 0x03, 0x04, 0x04, 0x08, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xBE, 0xAF};
	uint8_t block1[16] = {0x00};
	uint8_t blockK[16] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x08, 0x77, 0x8F, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
	uint8_t d_block[18] = {0x00};
	
	// card commands
	uint8_t wupC1[]       = { 0x40 };
	uint8_t wupC2[]       = { 0x43 };
	uint8_t wipeC[]       = { 0x41 };
	
	// iso14443 setup
	LED_A_ON();
	LED_B_OFF();
	LED_C_OFF();
	iso14443a_setup(FPGA_HF_ISO14443A_READER_LISTEN);

	// tracing
	clear_trace();
	set_tracing(true);
		
	while (true){
		// wipe
		if (needWipe){
			ReaderTransmitBitsPar(wupC1,7,0, NULL);
			if(!ReaderReceive(receivedAnswer, receivedAnswerPar) || (receivedAnswer[0] != 0x0a)) {
				if (MF_DBGLEVEL >= 1)	Dbprintf("wupC1 error");
				break;
			};

			ReaderTransmit(wipeC, sizeof(wipeC), NULL);
			if(!ReaderReceive(receivedAnswer, receivedAnswerPar) || (receivedAnswer[0] != 0x0a)) {
				if (MF_DBGLEVEL >= 1)	Dbprintf("wipeC error");
				break;
			};

			if(mifare_classic_halt(NULL, 0)) {
				if (MF_DBGLEVEL > 2)	Dbprintf("Halt error");
			};
		};
	
		// put default data
		if (needFill){
			// select commands
			ReaderTransmitBitsPar(wupC1, 7, 0, NULL);

			// gen1b magic tag : do no issue wupC2 and don't expect 0x0a response after SELECT_UID (after getting UID from chip in 'hf mf csetuid' command)
			if (!gen1b) { 

				if(!ReaderReceive(receivedAnswer, receivedAnswerPar) || (receivedAnswer[0] != 0x0a)) {
					if (MF_DBGLEVEL >= 1)	Dbprintf("wupC1 error");
					break;
				};

				ReaderTransmit(wupC2, sizeof(wupC2), NULL);
				if(!ReaderReceive(receivedAnswer, receivedAnswerPar) || (receivedAnswer[0] != 0x0a)) {
					if (MF_DBGLEVEL >= 1)	Dbprintf("wupC2 error");
					break;
				};
			}

			// send blocks command
			for (int blockNo = 0; blockNo < numBlocks; blockNo++) {
				if ((mifare_sendcmd_short(NULL, 0, 0xA0, blockNo, receivedAnswer, receivedAnswerPar, NULL) != 1) || (receivedAnswer[0] != 0x0a)) {
					if (MF_DBGLEVEL >= 1)	Dbprintf("write block send command error");
					break;
				};
				
				// check type of block and add crc
				if (!isBlockTrailer(blockNo)){
					memcpy(d_block, block1, 16);
				} else {
					memcpy(d_block, blockK, 16);
				}
				if (blockNo == 0) {
					memcpy(d_block, block0, 16);
				}
				AppendCrc14443a(d_block, 16);

				// send write command
				ReaderTransmit(d_block, sizeof(d_block), NULL);
				if ((ReaderReceive(receivedAnswer, receivedAnswerPar) != 1) || (receivedAnswer[0] != 0x0a)) {
					if (MF_DBGLEVEL >= 1)	Dbprintf("write block send data error");
					break;
				};
			}
			
			// halt
			// do no issue halt command for gen1b 
			if (!gen1b) {
				if (mifare_classic_halt(NULL, 0)) {
					if (MF_DBGLEVEL > 2)	Dbprintf("Halt error");
						break;
				}
			}
		}
		break;
	}	

	// send USB response
	LED_B_ON();
	cmd_send(CMD_ACK,isOK,0,0,NULL,0);
	LED_B_OFF();
	
	// reset fpga
	FpgaWriteConfWord(FPGA_MAJOR_MODE_OFF);
	LEDsoff();
		
	return;
}

void MifareCSetBlock(uint32_t arg0, uint32_t arg1, uint32_t arg2, uint8_t *datain){

  // params
	uint8_t needWipe = arg0;
	// bit 0 - need get UID
	// bit 1 - need wupC
	// bit 2 - need HALT after sequence
	// bit 3 - need init FPGA and field before sequence
	// bit 4 - need reset FPGA and LED
	// bit 6 - gen1b backdoor type
	uint8_t workFlags = arg1;
	uint8_t blockNo = arg2;

	// card commands
	uint8_t wupC1[]       = { 0x40 };
	uint8_t wupC2[]       = { 0x43 };
	uint8_t wipeC[]       = { 0x41 };

	// variables
	byte_t isOK = 0;
	uint8_t uid[10] = {0x00};
	uint8_t d_block[18] = {0x00};
	uint32_t cuid;

	uint8_t receivedAnswer[MAX_MIFARE_FRAME_SIZE];
	uint8_t receivedAnswerPar[MAX_MIFARE_PARITY_SIZE];

	// reset FPGA and LED
	if (workFlags & 0x08) {
		LED_A_ON();
		LED_B_OFF();
		LED_C_OFF();
		iso14443a_setup(FPGA_HF_ISO14443A_READER_LISTEN);

		clear_trace();
		set_tracing(true);
	}

	while (true) {

		// get UID from chip
		if (workFlags & 0x01) {
			if(!iso14443a_select_card(uid, NULL, &cuid, true, 0, true)) {
				if (MF_DBGLEVEL >= 1)	Dbprintf("Can't select card");
				// Continue, if we set wrong UID or wrong UID checksum or some ATQA or SAK we will can't select card. But we need to write block 0 to make card work.
				//break;
				};

				if(mifare_classic_halt(NULL, cuid)) {
					if (MF_DBGLEVEL > 2)	Dbprintf("Halt error");
					// Continue, some magic tags misbehavies and send an answer to it.
					// break;
				};
		};

		// reset chip
		// Wipe command don't work with gen1b
		if (needWipe && !(workFlags & 0x40)){
			ReaderTransmitBitsPar(wupC1,7,0, NULL);
			if(!ReaderReceive(receivedAnswer, receivedAnswerPar) || (receivedAnswer[0] != 0x0a)) {
				if (MF_DBGLEVEL >= 1)	Dbprintf("wupC1 error");
				break;
			};

			ReaderTransmit(wipeC, sizeof(wipeC), NULL);
			if(!ReaderReceive(receivedAnswer, receivedAnswerPar) || (receivedAnswer[0] != 0x0a)) {
				if (MF_DBGLEVEL >= 1)	Dbprintf("wipeC error");
				break;
			};

			if(mifare_classic_halt(NULL, 0)) {
				if (MF_DBGLEVEL > 2)	Dbprintf("Halt error");
				// Continue, some magic tags misbehavies and send an answer to it.
                        	// break;
			};
		};

		// write block
		if (workFlags & 0x02) {
			ReaderTransmitBitsPar(wupC1,7,0, NULL);

			// gen1b magic tag : do no issue wupC2 and don't expect 0x0a response after SELECT_UID (after getting UID from chip in 'hf mf csetuid' command)
			if (!(workFlags & 0x40)) {

				if(!ReaderReceive(receivedAnswer, receivedAnswerPar) || (receivedAnswer[0] != 0x0a)) {
					if (MF_DBGLEVEL >= 1)	Dbprintf("wupC1 error");
					break;
				};

				ReaderTransmit(wupC2, sizeof(wupC2), NULL);
				if(!ReaderReceive(receivedAnswer, receivedAnswerPar) || (receivedAnswer[0] != 0x0a)) {
					if (MF_DBGLEVEL >= 1)	Dbprintf("wupC2 error");
					break;
				};
			}
		}

		if ((mifare_sendcmd_short(NULL, 0, 0xA0, blockNo, receivedAnswer, receivedAnswerPar, NULL) != 1) || (receivedAnswer[0] != 0x0a)) {
			if (MF_DBGLEVEL >= 1)	Dbprintf("write block send command error");
			break;
		};

		memcpy(d_block, datain, 16);
		AppendCrc14443a(d_block, 16);

		ReaderTransmit(d_block, sizeof(d_block), NULL);
		if ((ReaderReceive(receivedAnswer, receivedAnswerPar) != 1) || (receivedAnswer[0] != 0x0a)) {
			if (MF_DBGLEVEL >= 1)	Dbprintf("write block send data error");
			break;
		};

		if (workFlags & 0x04) {
			// do no issue halt command for gen1b magic tag (#db# halt error. response len: 1)
			if (!(workFlags & 0x40)) {
				if (mifare_classic_halt(NULL, 0)) {
					if (MF_DBGLEVEL > 2)	Dbprintf("Halt error");
					// Continue, some magic tags misbehavies and send an answer to it.
					// break;
				}
			}
		}

		isOK = 1;
		break;
	}

	LED_B_ON();
	cmd_send(CMD_ACK,isOK,0,0,uid,4);
	LED_B_OFF();

	if ((workFlags & 0x10) || (!isOK)) {
		FpgaWriteConfWord(FPGA_MAJOR_MODE_OFF);
		LEDsoff();
	}
}


void MifareCGetBlock(uint32_t arg0, uint32_t arg1, uint32_t arg2, uint8_t *datain){

	// params
	// bit 1 - need wupC
	// bit 2 - need HALT after sequence
	// bit 3 - need init FPGA and field before sequence
	// bit 4 - need reset FPGA and LED
	// bit 5 - need to set datain instead of issuing USB reply (called via ARM for StandAloneMode14a)
	// bit 6 - gen1b backdoor type
	uint8_t workFlags = arg0;
	uint8_t blockNo = arg2;

	// card commands
	uint8_t wupC1[]       = { 0x40 };
	uint8_t wupC2[]       = { 0x43 };

	// variables
	byte_t isOK = 0;
	uint8_t data[18] = {0x00};
	uint32_t cuid = 0;

	uint8_t receivedAnswer[MAX_MIFARE_FRAME_SIZE];
	uint8_t receivedAnswerPar[MAX_MIFARE_PARITY_SIZE];

	if (workFlags & 0x08) {
		LED_A_ON();
		LED_B_OFF();
		LED_C_OFF();
		iso14443a_setup(FPGA_HF_ISO14443A_READER_LISTEN);

		clear_trace();
		set_tracing(true);
	}

	while (true) {
		if (workFlags & 0x02) {
			ReaderTransmitBitsPar(wupC1,7,0, NULL);
			if(!ReaderReceive(receivedAnswer, receivedAnswerPar) || (receivedAnswer[0] != 0x0a)) {
				if (MF_DBGLEVEL >= 1)	Dbprintf("wupC1 error");
				break;
		};
		// do no issue for gen1b magic tag
		if (!(workFlags & 0x40)) {
			ReaderTransmit(wupC2, sizeof(wupC2), NULL);
			if(!ReaderReceive(receivedAnswer, receivedAnswerPar) || (receivedAnswer[0] != 0x0a)) {
				if (MF_DBGLEVEL >= 1)	Dbprintf("wupC2 error");
				break;
			};
		}
	}

		// read block
		if ((mifare_sendcmd_short(NULL, 0, 0x30, blockNo, receivedAnswer, receivedAnswerPar, NULL) != 18)) {
			if (MF_DBGLEVEL >= 1)	Dbprintf("read block send command error");
			break;
		};
		memcpy(data, receivedAnswer, 18);

		if (workFlags & 0x04) {
			// do no issue halt command for gen1b magic tag (#db# halt error. response len: 1)
			if (!(workFlags & 0x40)) {
				if (mifare_classic_halt(NULL, cuid)) {
					if (MF_DBGLEVEL > 1)	Dbprintf("Halt error");
					// Continue, some magic tags misbehavies and send an answer to it.
					//		break;
				}
			}
		}

		isOK = 1;
		break;
	}

	LED_B_ON();
	if (workFlags & 0x20) {
		if (isOK)
			memcpy(datain, data, 18);
	}
	else
		cmd_send(CMD_ACK,isOK,0,0,data,18);
	LED_B_OFF();

	if ((workFlags & 0x10) || (!isOK)) {
		FpgaWriteConfWord(FPGA_MAJOR_MODE_OFF);
		LEDsoff();
	}
}

void MifareCIdent(){

	// card commands
	uint8_t wupC1[]       = { 0x40 };
	uint8_t wupC2[]       = { 0x43 };

	// variables
	byte_t isOK = 0;

	uint8_t receivedAnswer[MAX_MIFARE_FRAME_SIZE];
	uint8_t receivedAnswerPar[MAX_MIFARE_PARITY_SIZE];
	
	LED_A_ON();
	LED_B_OFF();
	LED_C_OFF();
	iso14443a_setup(FPGA_HF_ISO14443A_READER_LISTEN);

	clear_trace();
	set_tracing(true);	

	ReaderTransmitBitsPar(wupC1,7,0, NULL);
	if(ReaderReceive(receivedAnswer, receivedAnswerPar) && (receivedAnswer[0] == 0x0a)) {
		isOK = 2;

		ReaderTransmit(wupC2, sizeof(wupC2), NULL);
		if(ReaderReceive(receivedAnswer, receivedAnswerPar) && (receivedAnswer[0] == 0x0a)) {
			isOK = 1;
		};
	};

	// From iceman1001: removed the if,  since some magic tags misbehavies and send an answer to it.
	mifare_classic_halt(NULL, 0);
	
	LED_B_ON();
	cmd_send(CMD_ACK,isOK,0,0,0,0);
	LED_B_OFF();

	FpgaWriteConfWord(FPGA_MAJOR_MODE_OFF);
	LEDsoff();	
}

//
// DESFIRE
//

void Mifare_DES_Auth1(uint8_t arg0, uint8_t *datain){

	byte_t dataout[11] = {0x00};
	uint8_t uid[10] = {0x00};
	uint32_t cuid;

	iso14443a_setup(FPGA_HF_ISO14443A_READER_LISTEN);
	clear_trace();

	int len = iso14443a_select_card(uid, NULL, &cuid, true, 0, true);
	if(!len) {
		if (MF_DBGLEVEL >= MF_DBG_ERROR) Dbprintf("Can't select card");
		OnError(1);
		return;
	};

	if(mifare_desfire_des_auth1(cuid, dataout)){
		if (MF_DBGLEVEL >= MF_DBG_ERROR) Dbprintf("Authentication part1: Fail.");
		OnError(4);
		return;
	}

	if (MF_DBGLEVEL >= MF_DBG_EXTENDED) DbpString("AUTH 1 FINISHED");
    cmd_send(CMD_ACK,1,cuid,0,dataout, sizeof(dataout));
}

void Mifare_DES_Auth2(uint32_t arg0, uint8_t *datain){

	uint32_t cuid = arg0;
	uint8_t key[16] = {0x00};
	byte_t isOK = 0;
	byte_t dataout[12] = {0x00};

	memcpy(key, datain, 16);

	isOK = mifare_desfire_des_auth2(cuid, key, dataout);

	if( isOK) {
		if (MF_DBGLEVEL >= MF_DBG_EXTENDED) Dbprintf("Authentication part2: Failed");
		OnError(4);
		return;
	}

	if (MF_DBGLEVEL >= MF_DBG_EXTENDED) DbpString("AUTH 2 FINISHED");

	cmd_send(CMD_ACK, isOK, 0, 0, dataout, sizeof(dataout));
	FpgaWriteConfWord(FPGA_MAJOR_MODE_OFF);
	LEDsoff();
}

void OnSuccess(){
	pcb_blocknum = 0;
	ReaderTransmit(deselect_cmd, 3 , NULL);
	FpgaWriteConfWord(FPGA_MAJOR_MODE_OFF);
	LEDsoff();
}

void OnError(uint8_t reason){
	pcb_blocknum = 0;
	ReaderTransmit(deselect_cmd, 3 , NULL);
	FpgaWriteConfWord(FPGA_MAJOR_MODE_OFF);
	cmd_send(CMD_ACK,0,reason,0,0,0);
	LEDsoff();
}
