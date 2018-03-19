//-----------------------------------------------------------------------------
// Jonathan Westhues, Mar 2006
// Edits by Gerhard de Koning Gans, Sep 2007
//
// This code is licensed to you under the terms of the GNU GPL, version 2 or,
// at your option, any later version. See the LICENSE.txt file for the text of
// the license.
//-----------------------------------------------------------------------------
// Definitions for all the types of commands that may be sent over USB; our
// own protocol.
//-----------------------------------------------------------------------------

#ifndef __USB_CMD_H
#define __USB_CMD_H
#ifdef _MSC_VER
typedef DWORD uint32_t;
typedef BYTE uint8_t;
#define PACKED
// stuff
#else
#include <stdint.h>
#include <stdbool.h>
#define PACKED __attribute__((packed))
#endif

typedef struct {
	uint32_t	cmd;
	uint32_t	arg[3];
	union {
		uint8_t		asBytes[48];
		uint32_t	asDwords[12];
	} d;
} PACKED UsbCommand;

// For the bootloader
#define CMD_DEVICE_INFO                                                   0x0000
#define CMD_SETUP_WRITE                                                   0x0001
#define CMD_FINISH_WRITE                                                  0x0003
#define CMD_HARDWARE_RESET                                                0x0004
#define CMD_START_FLASH                                                   0x0005
#define CMD_NACK                                                          0x00fe
#define CMD_ACK                                                           0x00ff

// For general mucking around
#define CMD_DEBUG_PRINT_STRING                                            0x0100
#define CMD_DEBUG_PRINT_INTEGERS                                          0x0101
#define CMD_DEBUG_PRINT_BYTES                                             0x0102
#define CMD_LCD_RESET                                                     0x0103
#define CMD_LCD                                                           0x0104
#define CMD_BUFF_CLEAR                                                    0x0105
#define CMD_READ_MEM                                                      0x0106
#define CMD_VERSION                                                       0x0107

// For low-frequency tags
#define CMD_READ_TI_TYPE                                                  0x0202
#define CMD_WRITE_TI_TYPE                                                 0x0203
#define CMD_DOWNLOADED_RAW_BITS_TI_TYPE                                   0x0204
#define CMD_ACQUIRE_RAW_ADC_SAMPLES_125K                                  0x0205
#define CMD_MOD_THEN_ACQUIRE_RAW_ADC_SAMPLES_125K                         0x0206
#define CMD_DOWNLOAD_RAW_ADC_SAMPLES_125K                                 0x0207
#define CMD_DOWNLOADED_RAW_ADC_SAMPLES_125K                               0x0208
#define CMD_DOWNLOADED_SIM_SAMPLES_125K                                   0x0209
#define CMD_SIMULATE_TAG_125K                                             0x020A
#define CMD_HID_DEMOD_FSK                                                 0x020B
#define CMD_HID_SIM_TAG                                                   0x020C
#define CMD_SET_LF_DIVISOR                                                0x020D
#define CMD_LF_SIMULATE_BIDIR                                             0x020E
#define CMD_SET_ADC_MUX                                                   0x020F
#define CMD_HID_CLONE_TAG                                                 0x0210
#define CMD_EM410X_WRITE_TAG                                              0x0211
#define CMD_INDALA_CLONE_TAG                                              0x0212
// for 224 bits UID
#define CMD_INDALA_CLONE_TAG_L                                            0x0213
#define CMD_T55XX_READ_BLOCK                                              0x0214
#define CMD_T55XX_WRITE_BLOCK                                             0x0215
#define CMD_T55XX_RESET_READ                                              0x0216
#define CMD_PCF7931_READ                                                  0x0217
#define CMD_EM4X_READ_WORD                                                0x0218
#define CMD_EM4X_WRITE_WORD                                               0x0219
#define CMD_IO_DEMOD_FSK                                                  0x021A
#define CMD_IO_CLONE_TAG                                                  0x021B
#define CMD_EM410X_DEMOD                                                  0x021c
#define CMD_SET_LF_SAMPLING_CONFIG                                        0x021d
#define CMD_FSK_SIM_TAG                                                   0x021E
#define CMD_ASK_SIM_TAG                                                   0x021F
#define CMD_PSK_SIM_TAG                                                   0x0220
#define CMD_AWID_DEMOD_FSK                                                0x0221
#define CMD_VIKING_CLONE_TAG                                              0x0223
#define CMD_T55XX_WAKEUP                                                  0x0224
#define CMD_COTAG                                                         0x0225

/* CMD_SET_ADC_MUX: ext1 is 0 for lopkd, 1 for loraw, 2 for hipkd, 3 for hiraw */

// For the 13.56 MHz tags
#define CMD_ACQUIRE_RAW_ADC_SAMPLES_ISO_15693                             0x0300
#define CMD_READ_SRI512_TAG                                               0x0303
#define CMD_READ_SRIX4K_TAG                                               0x0304
#define CMD_READER_ISO_15693                                              0x0310
#define CMD_SIMTAG_ISO_15693                                              0x0311
#define CMD_RECORD_RAW_ADC_SAMPLES_ISO_15693                              0x0312
#define CMD_ISO_15693_COMMAND                                             0x0313
#define CMD_ISO_15693_COMMAND_DONE                                        0x0314
#define CMD_ISO_15693_FIND_AFI                                            0x0315
#define CMD_ISO_15693_DEBUG                                               0x0316

// For Hitag2 transponders
#define CMD_SNOOP_HITAG                                                   0x0370
#define CMD_SIMULATE_HITAG                                                0x0371
#define CMD_READER_HITAG                                                  0x0372

#define CMD_SIMULATE_HITAG_S                                              0x0368
#define CMD_TEST_HITAGS_TRACES						  0x0367
#define CMD_READ_HITAG_S						  0x0373
#define CMD_WR_HITAG_S							  0x0375
#define CMD_EMU_HITAG_S						          0x0376

#define CMD_SIMULATE_TAG_ISO_14443B                                       0x0381
#define CMD_SNOOP_ISO_14443B                                              0x0382
#define CMD_SNOOP_ISO_14443a                                              0x0383
#define CMD_SIMULATE_TAG_ISO_14443a                                       0x0384
#define CMD_READER_ISO_14443a                                             0x0385
#define CMD_SIMULATE_TAG_LEGIC_RF                                         0x0387
#define CMD_READER_LEGIC_RF                                               0x0388
#define CMD_WRITER_LEGIC_RF                                               0x0389
#define CMD_EPA_PACE_COLLECT_NONCE                                        0x038A

#define CMD_ICLASS_READCHECK                                              0x038F
#define CMD_ICLASS_CLONE                                                  0x0390
#define CMD_ICLASS_DUMP                                                   0x0391
#define CMD_SNOOP_ICLASS                                                  0x0392
#define CMD_SIMULATE_TAG_ICLASS                                           0x0393
#define CMD_READER_ICLASS                                                 0x0394
#define CMD_READER_ICLASS_REPLAY                                          0x0395
#define CMD_ICLASS_READBLOCK                                              0x0396
#define CMD_ICLASS_WRITEBLOCK                                             0x0397
#define CMD_ICLASS_EML_MEMSET                                             0x0398
#define CMD_ICLASS_AUTHENTICATION                                         0x0399

// For measurements of the antenna tuning
#define CMD_MEASURE_ANTENNA_TUNING                                        0x0400
#define CMD_MEASURE_ANTENNA_TUNING_HF                                     0x0401
#define CMD_MEASURED_ANTENNA_TUNING                                       0x0410
#define CMD_LISTEN_READER_FIELD                                           0x0420

// For direct FPGA control
#define CMD_FPGA_MAJOR_MODE_OFF                                           0x0500

// For mifare commands
#define CMD_MIFARE_SET_DBGMODE                                            0x0600
#define CMD_MIFARE_EML_MEMCLR                                             0x0601
#define CMD_MIFARE_EML_MEMSET                                             0x0602
#define CMD_MIFARE_EML_MEMGET                                             0x0603
#define CMD_MIFARE_EML_CARDLOAD                                           0x0604
#define CMD_MIFARE_EML_CSETBLOCK                                          0x0605
#define CMD_MIFARE_EML_CGETBLOCK                                          0x0606

#define CMD_SIMULATE_MIFARE_CARD                                          0x0610

#define CMD_READER_MIFARE                                                 0x0611
#define CMD_MIFARE_NESTED                                                 0x0612

#define CMD_MIFARE_READBL                                                 0x0620
#define CMD_MIFARE_READSC                                                 0x0621
#define CMD_MIFARE_WRITEBL                                                0x0622
#define CMD_MIFARE_CHKKEYS                                                0x0623

#define CMD_MIFARE_SNIFFER                                                0x0630

#define CMD_UNKNOWN                                                       0xFFFF

// CMD_DEVICE_INFO response packet has flags in arg[0], flag definitions:
/* Whether a bootloader that understands the common_area is present */
#define DEVICE_INFO_FLAG_BOOTROM_PRESENT         	(1<<0)

/* Whether a osimage that understands the common_area is present */
#define DEVICE_INFO_FLAG_OSIMAGE_PRESENT         	(1<<1)

/* Set if the bootloader is currently executing */
#define DEVICE_INFO_FLAG_CURRENT_MODE_BOOTROM    	(1<<2)

/* Set if the OS is currently executing */
#define DEVICE_INFO_FLAG_CURRENT_MODE_OS         	(1<<3)

/* Set if this device understands the extend start flash command */
#define DEVICE_INFO_FLAG_UNDERSTANDS_START_FLASH 	(1<<4)

/* CMD_START_FLASH may have three arguments: start of area to flash,
   end of area to flash, optional magic.
   The bootrom will not allow to overwrite itself unless this magic
   is given as third parameter */

#define START_FLASH_MAGIC 0x54494f44 // 'DOIT'

#endif
