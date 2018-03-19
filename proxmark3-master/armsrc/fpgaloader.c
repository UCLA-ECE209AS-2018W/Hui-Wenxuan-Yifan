//-----------------------------------------------------------------------------
// Jonathan Westhues, April 2006
// iZsh <izsh at fail0verflow.com>, 2014
//
// This code is licensed to you under the terms of the GNU GPL, version 2 or,
// at your option, any later version. See the LICENSE.txt file for the text of
// the license.
//-----------------------------------------------------------------------------
// Routines to load the FPGA image, and then to configure the FPGA's major
// mode once it is configured.
//-----------------------------------------------------------------------------

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "fpgaloader.h"
#include "proxmark3.h"
#include "util.h"
#include "string.h"
#include "BigBuf.h"
#include "zlib.h"

extern void Dbprintf(const char *fmt, ...);

// remember which version of the bitstream we have already downloaded to the FPGA
static int downloaded_bitstream = FPGA_BITSTREAM_ERR;

// this is where the bitstreams are located in memory:
extern uint8_t _binary_obj_fpga_all_bit_z_start, _binary_obj_fpga_all_bit_z_end;

static uint8_t *fpga_image_ptr = NULL;
static uint32_t uncompressed_bytes_cnt;

static const uint8_t _bitparse_fixed_header[] = {0x00, 0x09, 0x0f, 0xf0, 0x0f, 0xf0, 0x0f, 0xf0, 0x0f, 0xf0, 0x00, 0x00, 0x01};
#define FPGA_BITSTREAM_FIXED_HEADER_SIZE	sizeof(_bitparse_fixed_header)
#define OUTPUT_BUFFER_LEN 		80
#define FPGA_INTERLEAVE_SIZE 	288

//-----------------------------------------------------------------------------
// Set up the Serial Peripheral Interface as master
// Used to write the FPGA config word
// May also be used to write to other SPI attached devices like an LCD
//-----------------------------------------------------------------------------
void SetupSpi(int mode)
{
	// PA10 -> SPI_NCS2 chip select (LCD)
	// PA11 -> SPI_NCS0 chip select (FPGA)
	// PA12 -> SPI_MISO Master-In Slave-Out
	// PA13 -> SPI_MOSI Master-Out Slave-In
	// PA14 -> SPI_SPCK Serial Clock

	// Disable PIO control of the following pins, allows use by the SPI peripheral
	AT91C_BASE_PIOA->PIO_PDR =
		GPIO_NCS0	|
		GPIO_NCS2 	|
		GPIO_MISO	|
		GPIO_MOSI	|
		GPIO_SPCK;

	AT91C_BASE_PIOA->PIO_ASR =
		GPIO_NCS0	|
		GPIO_MISO	|
		GPIO_MOSI	|
		GPIO_SPCK;

	AT91C_BASE_PIOA->PIO_BSR = GPIO_NCS2;

	//enable the SPI Peripheral clock
	AT91C_BASE_PMC->PMC_PCER = (1<<AT91C_ID_SPI);
	// Enable SPI
	AT91C_BASE_SPI->SPI_CR = AT91C_SPI_SPIEN;

	switch (mode) {
		case SPI_FPGA_MODE:
			AT91C_BASE_SPI->SPI_MR =
				( 0 << 24)	|	// Delay between chip selects (take default: 6 MCK periods)
				(14 << 16)	|	// Peripheral Chip Select (selects FPGA SPI_NCS0 or PA11)
				( 0 << 7)	|	// Local Loopback Disabled
				( 1 << 4)	|	// Mode Fault Detection disabled
				( 0 << 2)	|	// Chip selects connected directly to peripheral
				( 0 << 1) 	|	// Fixed Peripheral Select
				( 1 << 0);		// Master Mode
			AT91C_BASE_SPI->SPI_CSR[0] =
				( 1 << 24)	|	// Delay between Consecutive Transfers (32 MCK periods)
				( 1 << 16)	|	// Delay Before SPCK (1 MCK period)
				( 6 << 8)	|	// Serial Clock Baud Rate (baudrate = MCK/6 = 24Mhz/6 = 4M baud
				( 8 << 4)	|	// Bits per Transfer (16 bits)
				( 0 << 3)	|	// Chip Select inactive after transfer
				( 1 << 1)	|	// Clock Phase data captured on leading edge, changes on following edge
				( 0 << 0);		// Clock Polarity inactive state is logic 0
			break;
		case SPI_LCD_MODE:
			AT91C_BASE_SPI->SPI_MR =
				( 0 << 24)	|	// Delay between chip selects (take default: 6 MCK periods)
				(11 << 16)	|	// Peripheral Chip Select (selects LCD SPI_NCS2 or PA10)
				( 0 << 7)	|	// Local Loopback Disabled
				( 1 << 4)	|	// Mode Fault Detection disabled
				( 0 << 2)	|	// Chip selects connected directly to peripheral
				( 0 << 1) 	|	// Fixed Peripheral Select
				( 1 << 0);		// Master Mode
			AT91C_BASE_SPI->SPI_CSR[2] =
				( 1 << 24)	|	// Delay between Consecutive Transfers (32 MCK periods)
				( 1 << 16)	|	// Delay Before SPCK (1 MCK period)
				( 6 << 8)	|	// Serial Clock Baud Rate (baudrate = MCK/6 = 24Mhz/6 = 4M baud
				( 1 << 4)	|	// Bits per Transfer (9 bits)
				( 0 << 3)	|	// Chip Select inactive after transfer
				( 1 << 1)	|	// Clock Phase data captured on leading edge, changes on following edge
				( 0 << 0);		// Clock Polarity inactive state is logic 0
			break;
		default:				// Disable SPI
			AT91C_BASE_SPI->SPI_CR = AT91C_SPI_SPIDIS;
			break;
	}
}

//-----------------------------------------------------------------------------
// Set up the synchronous serial port, with the one set of options that we
// always use when we are talking to the FPGA. Both RX and TX are enabled.
//-----------------------------------------------------------------------------
void FpgaSetupSsc(void)
{
	// First configure the GPIOs, and get ourselves a clock.
	AT91C_BASE_PIOA->PIO_ASR =
		GPIO_SSC_FRAME	|
		GPIO_SSC_DIN	|
		GPIO_SSC_DOUT	|
		GPIO_SSC_CLK;
	AT91C_BASE_PIOA->PIO_PDR = GPIO_SSC_DOUT;

	AT91C_BASE_PMC->PMC_PCER = (1 << AT91C_ID_SSC);

	// Now set up the SSC proper, starting from a known state.
	AT91C_BASE_SSC->SSC_CR = AT91C_SSC_SWRST;

	// RX clock comes from TX clock, RX starts when TX starts, data changes
	// on RX clock rising edge, sampled on falling edge
	AT91C_BASE_SSC->SSC_RCMR = SSC_CLOCK_MODE_SELECT(1) | SSC_CLOCK_MODE_START(1);

	// 8 bits per transfer, no loopback, MSB first, 1 transfer per sync
	// pulse, no output sync
	AT91C_BASE_SSC->SSC_RFMR = SSC_FRAME_MODE_BITS_IN_WORD(8) |	AT91C_SSC_MSBF | SSC_FRAME_MODE_WORDS_PER_TRANSFER(0);

	// clock comes from TK pin, no clock output, outputs change on falling
	// edge of TK, sample on rising edge of TK, start on positive-going edge of sync
	AT91C_BASE_SSC->SSC_TCMR = SSC_CLOCK_MODE_SELECT(2) |	SSC_CLOCK_MODE_START(5);

	// tx framing is the same as the rx framing
	AT91C_BASE_SSC->SSC_TFMR = AT91C_BASE_SSC->SSC_RFMR;

	AT91C_BASE_SSC->SSC_CR = AT91C_SSC_RXEN | AT91C_SSC_TXEN;
}

//-----------------------------------------------------------------------------
// Set up DMA to receive samples from the FPGA. We will use the PDC, with
// a single buffer as a circular buffer (so that we just chain back to
// ourselves, not to another buffer). The stuff to manipulate those buffers
// is in apps.h, because it should be inlined, for speed.
//-----------------------------------------------------------------------------
bool FpgaSetupSscDma(uint8_t *buf, int len)
{
	if (buf == NULL) return false;

	AT91C_BASE_PDC_SSC->PDC_PTCR = AT91C_PDC_RXTDIS;	// Disable DMA Transfer
	AT91C_BASE_PDC_SSC->PDC_RPR = (uint32_t) buf;		// transfer to this memory address
	AT91C_BASE_PDC_SSC->PDC_RCR = len;					// transfer this many bytes
	AT91C_BASE_PDC_SSC->PDC_RNPR = (uint32_t) buf;		// next transfer to same memory address
	AT91C_BASE_PDC_SSC->PDC_RNCR = len;					// ... with same number of bytes
	AT91C_BASE_PDC_SSC->PDC_PTCR = AT91C_PDC_RXTEN;		// go!

	return true;
}


//----------------------------------------------------------------------------
// Uncompress (inflate) the FPGA data. Returns one decompressed byte with
// each call. 
//----------------------------------------------------------------------------
static int get_from_fpga_combined_stream(z_streamp compressed_fpga_stream, uint8_t *output_buffer)
{
	if (fpga_image_ptr == compressed_fpga_stream->next_out) {	// need more data
		compressed_fpga_stream->next_out = output_buffer;
		compressed_fpga_stream->avail_out = OUTPUT_BUFFER_LEN;
		fpga_image_ptr = output_buffer;
		int res = inflate(compressed_fpga_stream, Z_SYNC_FLUSH);
		if (res != Z_OK)
			Dbprintf("inflate returned: %d, %s", res, compressed_fpga_stream->msg);

		if (res < 0)
			return res;
	}

	uncompressed_bytes_cnt++;
	
	return *fpga_image_ptr++;
}

//----------------------------------------------------------------------------
// Undo the interleaving of several FPGA config files. FPGA config files
// are combined into one big file:
// 288 bytes from FPGA file 1, followed by 288 bytes from FGPA file 2, etc.
//----------------------------------------------------------------------------
static int get_from_fpga_stream(int bitstream_version, z_streamp compressed_fpga_stream, uint8_t *output_buffer)
{
	while((uncompressed_bytes_cnt / FPGA_INTERLEAVE_SIZE) % FPGA_BITSTREAM_MAX != (bitstream_version - 1)) {
		// skip undesired data belonging to other bitstream_versions
		get_from_fpga_combined_stream(compressed_fpga_stream, output_buffer);
	}

	return get_from_fpga_combined_stream(compressed_fpga_stream, output_buffer);
	
}


static voidpf fpga_inflate_malloc(voidpf opaque, uInt items, uInt size)
{
	return BigBuf_malloc(items*size);
}


static void fpga_inflate_free(voidpf opaque, voidpf address)
{
	BigBuf_free(); BigBuf_Clear_ext(false);
}


//----------------------------------------------------------------------------
// Initialize decompression of the respective (HF or LF) FPGA stream 
//----------------------------------------------------------------------------
static bool reset_fpga_stream(int bitstream_version, z_streamp compressed_fpga_stream, uint8_t *output_buffer)
{
	uint8_t header[FPGA_BITSTREAM_FIXED_HEADER_SIZE];
	
	uncompressed_bytes_cnt = 0;
	
	// initialize z_stream structure for inflate:
	compressed_fpga_stream->next_in = &_binary_obj_fpga_all_bit_z_start;
	compressed_fpga_stream->avail_in = &_binary_obj_fpga_all_bit_z_start - &_binary_obj_fpga_all_bit_z_end;
	compressed_fpga_stream->next_out = output_buffer;
	compressed_fpga_stream->avail_out = OUTPUT_BUFFER_LEN;
	compressed_fpga_stream->zalloc = &fpga_inflate_malloc;
	compressed_fpga_stream->zfree = &fpga_inflate_free;

	inflateInit2(compressed_fpga_stream, 0);

	fpga_image_ptr = output_buffer;

	for (uint16_t i = 0; i < FPGA_BITSTREAM_FIXED_HEADER_SIZE; i++) {
		header[i] = get_from_fpga_stream(bitstream_version, compressed_fpga_stream, output_buffer);
	}
	
	// Check for a valid .bit file (starts with _bitparse_fixed_header)
	if(memcmp(_bitparse_fixed_header, header, FPGA_BITSTREAM_FIXED_HEADER_SIZE) == 0) {
		return true;
	} else {
		return false;
	}
}


static void DownloadFPGA_byte(unsigned char w)
{
#define SEND_BIT(x) { if(w & (1<<x) ) HIGH(GPIO_FPGA_DIN); else LOW(GPIO_FPGA_DIN); HIGH(GPIO_FPGA_CCLK); LOW(GPIO_FPGA_CCLK); }
	SEND_BIT(7);
	SEND_BIT(6);
	SEND_BIT(5);
	SEND_BIT(4);
	SEND_BIT(3);
	SEND_BIT(2);
	SEND_BIT(1);
	SEND_BIT(0);
}

// Download the fpga image starting at current stream position with length FpgaImageLen bytes
static void DownloadFPGA(int bitstream_version, int FpgaImageLen, z_streamp compressed_fpga_stream, uint8_t *output_buffer)
{

	//Dbprintf("DownloadFPGA(len: %d)", FpgaImageLen);
	
	int i=0;

	AT91C_BASE_PIOA->PIO_OER = GPIO_FPGA_ON;
	AT91C_BASE_PIOA->PIO_PER = GPIO_FPGA_ON;
	HIGH(GPIO_FPGA_ON);		// ensure everything is powered on

	SpinDelay(50);

	LED_D_ON();

	// These pins are inputs
    AT91C_BASE_PIOA->PIO_ODR =
    	GPIO_FPGA_NINIT |
    	GPIO_FPGA_DONE;
	// PIO controls the following pins
    AT91C_BASE_PIOA->PIO_PER =
    	GPIO_FPGA_NINIT |
    	GPIO_FPGA_DONE;
	// Enable pull-ups
	AT91C_BASE_PIOA->PIO_PPUER =
		GPIO_FPGA_NINIT |
		GPIO_FPGA_DONE;

	// setup initial logic state
	HIGH(GPIO_FPGA_NPROGRAM);
	LOW(GPIO_FPGA_CCLK);
	LOW(GPIO_FPGA_DIN);
	// These pins are outputs
	AT91C_BASE_PIOA->PIO_OER =
		GPIO_FPGA_NPROGRAM	|
		GPIO_FPGA_CCLK		|
		GPIO_FPGA_DIN;

	// enter FPGA configuration mode
	LOW(GPIO_FPGA_NPROGRAM);
	SpinDelay(50);
	HIGH(GPIO_FPGA_NPROGRAM);

	i=100000;
	// wait for FPGA ready to accept data signal
	while ((i) && ( !(AT91C_BASE_PIOA->PIO_PDSR & GPIO_FPGA_NINIT ) ) ) {
		i--;
	}

	// crude error indicator, leave both red LEDs on and return
	if (i==0){
		LED_C_ON();
		LED_D_ON();
		return;
	}

	for(i = 0; i < FpgaImageLen; i++) {
		int b = get_from_fpga_stream(bitstream_version, compressed_fpga_stream, output_buffer);
		if (b < 0) {
			Dbprintf("Error %d during FpgaDownload", b);
			break;
		}
		DownloadFPGA_byte(b);
	}
	
	// continue to clock FPGA until ready signal goes high
	i=100000;
	while ( (i--) && ( !(AT91C_BASE_PIOA->PIO_PDSR & GPIO_FPGA_DONE ) ) ) {
		HIGH(GPIO_FPGA_CCLK);
		LOW(GPIO_FPGA_CCLK);
	}
	// crude error indicator, leave both red LEDs on and return
	if (i==0){
		LED_C_ON();
		LED_D_ON();
		return;
	}
	LED_D_OFF();
}


/* Simple Xilinx .bit parser. The file starts with the fixed opaque byte sequence
 * 00 09 0f f0 0f f0 0f f0 0f f0 00 00 01
 * After that the format is 1 byte section type (ASCII character), 2 byte length
 * (big endian), <length> bytes content. Except for section 'e' which has 4 bytes
 * length.
 */
static int bitparse_find_section(int bitstream_version, char section_name, unsigned int *section_length, z_streamp compressed_fpga_stream, uint8_t *output_buffer)
{
	int result = 0;
	#define MAX_FPGA_BIT_STREAM_HEADER_SEARCH 100  // maximum number of bytes to search for the requested section
	uint16_t numbytes = 0;
	while(numbytes < MAX_FPGA_BIT_STREAM_HEADER_SEARCH) {
		char current_name = get_from_fpga_stream(bitstream_version, compressed_fpga_stream, output_buffer);
		numbytes++;
		unsigned int current_length = 0;
		if(current_name < 'a' || current_name > 'e') {
			/* Strange section name, abort */
			break;
		}
		current_length = 0;
		switch(current_name) {
		case 'e':
			/* Four byte length field */
			current_length += get_from_fpga_stream(bitstream_version, compressed_fpga_stream, output_buffer) << 24;
			current_length += get_from_fpga_stream(bitstream_version, compressed_fpga_stream, output_buffer) << 16;
			numbytes += 2;
		default: /* Fall through, two byte length field */
			current_length += get_from_fpga_stream(bitstream_version, compressed_fpga_stream, output_buffer) << 8;
			current_length += get_from_fpga_stream(bitstream_version, compressed_fpga_stream, output_buffer) << 0;
			numbytes += 2;
		}

		if(current_name != 'e' && current_length > 255) {
			/* Maybe a parse error */
			break;
		}

		if(current_name == section_name) {
			/* Found it */
			*section_length = current_length;
			result = 1;
			break;
		}

		for (uint16_t i = 0; i < current_length && numbytes < MAX_FPGA_BIT_STREAM_HEADER_SEARCH; i++) {
			get_from_fpga_stream(bitstream_version, compressed_fpga_stream, output_buffer);
			numbytes++;
		}
	}

	return result;
}


//----------------------------------------------------------------------------
// Check which FPGA image is currently loaded (if any). If necessary 
// decompress and load the correct (HF or LF) image to the FPGA
//----------------------------------------------------------------------------
void FpgaDownloadAndGo(int bitstream_version)
{
	z_stream compressed_fpga_stream;
	uint8_t output_buffer[OUTPUT_BUFFER_LEN] = {0x00};
	
	// check whether or not the bitstream is already loaded
	if (downloaded_bitstream == bitstream_version)
		return;

	// make sure that we have enough memory to decompress
	BigBuf_free(); BigBuf_Clear_ext(false);	
	
	if (!reset_fpga_stream(bitstream_version, &compressed_fpga_stream, output_buffer)) {
		return;
	}

	unsigned int bitstream_length;
	if(bitparse_find_section(bitstream_version, 'e', &bitstream_length, &compressed_fpga_stream, output_buffer)) {
		DownloadFPGA(bitstream_version, bitstream_length, &compressed_fpga_stream, output_buffer);
		downloaded_bitstream = bitstream_version;
	}

	inflateEnd(&compressed_fpga_stream);
	
	// turn off antenna
	FpgaWriteConfWord(FPGA_MAJOR_MODE_OFF);
	
	// free eventually allocated BigBuf memory
	BigBuf_free(); BigBuf_Clear_ext(false);	
}	


//-----------------------------------------------------------------------------
// Gather version information from FPGA image. Needs to decompress the begin 
// of the respective (HF or LF) image.
// Note: decompression makes use of (i.e. overwrites) BigBuf[]. It is therefore
// advisable to call this only once and store the results for later use.
//-----------------------------------------------------------------------------
void FpgaGatherVersion(int bitstream_version, char *dst, int len)
{
	unsigned int fpga_info_len;
	char tempstr[40] = {0x00};
	z_stream compressed_fpga_stream;
	uint8_t output_buffer[OUTPUT_BUFFER_LEN] = {0x00};
	
	dst[0] = '\0';

	// ensure that we can allocate enough memory for decompression:
	BigBuf_free(); BigBuf_Clear_ext(false);

	if (!reset_fpga_stream(bitstream_version, &compressed_fpga_stream, output_buffer))
		return;

	if(bitparse_find_section(bitstream_version, 'a', &fpga_info_len, &compressed_fpga_stream, output_buffer)) {
		for (uint16_t i = 0; i < fpga_info_len; i++) {
			char c = (char)get_from_fpga_stream(bitstream_version, &compressed_fpga_stream, output_buffer);
			if (i < sizeof(tempstr)) {
				tempstr[i] = c;
			}
		}
		if (!memcmp("fpga_lf", tempstr, 7))
			strncat(dst, "LF ", len-1);
		else if (!memcmp("fpga_hf", tempstr, 7))
			strncat(dst, "HF ", len-1);
	}
	strncat(dst, "FPGA image built", len-1);
	if(bitparse_find_section(bitstream_version, 'b', &fpga_info_len, &compressed_fpga_stream, output_buffer)) {
		strncat(dst, " for ", len-1);
		for (uint16_t i = 0; i < fpga_info_len; i++) {
			char c = (char)get_from_fpga_stream(bitstream_version, &compressed_fpga_stream, output_buffer);
			if (i < sizeof(tempstr)) {
				tempstr[i] = c;
			}
		}
		strncat(dst, tempstr, len-1);
	}
	if(bitparse_find_section(bitstream_version, 'c', &fpga_info_len, &compressed_fpga_stream, output_buffer)) {
		strncat(dst, " on ", len-1);
		for (uint16_t i = 0; i < fpga_info_len; i++) {
			char c = (char)get_from_fpga_stream(bitstream_version, &compressed_fpga_stream, output_buffer);
			if (i < sizeof(tempstr)) {
				tempstr[i] = c;
			}
		}
		strncat(dst, tempstr, len-1);
	}
	if(bitparse_find_section(bitstream_version, 'd', &fpga_info_len, &compressed_fpga_stream, output_buffer)) {
		strncat(dst, " at ", len-1);
		for (uint16_t i = 0; i < fpga_info_len; i++) {
			char c = (char)get_from_fpga_stream(bitstream_version, &compressed_fpga_stream, output_buffer);
			if (i < sizeof(tempstr)) {
				tempstr[i] = c;
			}
		}
		strncat(dst, tempstr, len-1);
	}
	
	strncat(dst, "\n", len-1);

	inflateEnd(&compressed_fpga_stream);
}


//-----------------------------------------------------------------------------
// Send a 16 bit command/data pair to the FPGA.
// The bit format is:  C3 C2 C1 C0 D11 D10 D9 D8 D7 D6 D5 D4 D3 D2 D1 D0
// where C is the 4 bit command and D is the 12 bit data
//-----------------------------------------------------------------------------
void FpgaSendCommand(uint16_t cmd, uint16_t v)
{
	SetupSpi(SPI_FPGA_MODE);
	while ((AT91C_BASE_SPI->SPI_SR & AT91C_SPI_TXEMPTY) == 0);		// wait for the transfer to complete
	AT91C_BASE_SPI->SPI_TDR = AT91C_SPI_LASTXFER | cmd | v;		// send the data
}
//-----------------------------------------------------------------------------
// Write the FPGA setup word (that determines what mode the logic is in, read
// vs. clone vs. etc.). This is now a special case of FpgaSendCommand() to
// avoid changing this function's occurence everywhere in the source code.
//-----------------------------------------------------------------------------
void FpgaWriteConfWord(uint8_t v)
{
	FpgaSendCommand(FPGA_CMD_SET_CONFREG, v);
}

//-----------------------------------------------------------------------------
// Set up the CMOS switches that mux the ADC: four switches, independently
// closable, but should only close one at a time. Not an FPGA thing, but
// the samples from the ADC always flow through the FPGA.
//-----------------------------------------------------------------------------
void SetAdcMuxFor(uint32_t whichGpio)
{
	AT91C_BASE_PIOA->PIO_OER =
		GPIO_MUXSEL_HIPKD |
		GPIO_MUXSEL_LOPKD |
		GPIO_MUXSEL_LORAW |
		GPIO_MUXSEL_HIRAW;

	AT91C_BASE_PIOA->PIO_PER =
		GPIO_MUXSEL_HIPKD |
		GPIO_MUXSEL_LOPKD |
		GPIO_MUXSEL_LORAW |
		GPIO_MUXSEL_HIRAW;

	LOW(GPIO_MUXSEL_HIPKD);
	LOW(GPIO_MUXSEL_HIRAW);
	LOW(GPIO_MUXSEL_LORAW);
	LOW(GPIO_MUXSEL_LOPKD);

	HIGH(whichGpio);
}

void Fpga_print_status(void) {
	Dbprintf("Fgpa");
	switch(downloaded_bitstream) {
		case FPGA_BITSTREAM_HF: Dbprintf("  mode....................HF"); break;
		case FPGA_BITSTREAM_LF: Dbprintf("  mode....................LF"); break;
		default:		Dbprintf("  mode....................%d", downloaded_bitstream); break;
	}
}

int FpgaGetCurrent() {
	return downloaded_bitstream;
}
