#include "proxmark3.h"
#include "apps.h"
#include "BigBuf.h"
#include "util.h"
#include "usb_cdc.h"	// for usb_poll_validate_length

static void RAMFUNC optimizedSnoop(void);

static void RAMFUNC optimizedSnoop(void)
{
	int n = BigBuf_max_traceLen() / sizeof(uint16_t); // take all memory

	uint16_t *dest = (uint16_t *)BigBuf_get_addr();
	uint16_t *destend = dest + n;

	AT91C_BASE_SSC->SSC_RFMR = SSC_FRAME_MODE_BITS_IN_WORD(16); // Setting Frame mode, 16 bits per word
	// Reading data loop
	while(dest <= destend)
	{
		if(AT91C_BASE_SSC->SSC_SR & AT91C_SSC_RXRDY)
		{
			*dest = (uint16_t)(AT91C_BASE_SSC->SSC_RHR);
			dest++;
		}
	}
	//Resetting Frame mode (First set in fpgaloader.c)
	AT91C_BASE_SSC->SSC_RFMR = SSC_FRAME_MODE_BITS_IN_WORD(8) | AT91C_SSC_MSBF | SSC_FRAME_MODE_WORDS_PER_TRANSFER(0);
}

void HfSnoop(int samplesToSkip, int triggersToSkip)
{
	BigBuf_free(); BigBuf_Clear();
	
	Dbprintf("Skipping first %d sample pairs, Skipping %d triggers.\n", samplesToSkip, triggersToSkip);
	int trigger_cnt;
	LED_D_ON();
	// Select correct configs
	FpgaDownloadAndGo(FPGA_BITSTREAM_HF);
	// Set up the synchronous serial port
	FpgaSetupSsc();
	// connect Demodulated Signal to ADC:
	SetAdcMuxFor(GPIO_MUXSEL_HIPKD);
	FpgaWriteConfWord(FPGA_MAJOR_MODE_HF_SNOOP);
	SpinDelay(100);

	AT91C_BASE_SSC->SSC_RFMR = SSC_FRAME_MODE_BITS_IN_WORD(16); // Setting Frame Mode For better performance on high speed data transfer.

	trigger_cnt = 0;
	uint16_t r = 0;
	while(!BUTTON_PRESS() && !usb_poll_validate_length()) {
		WDT_HIT();
		if(AT91C_BASE_SSC->SSC_SR & (AT91C_SSC_RXRDY)) {
			r = (uint16_t)AT91C_BASE_SSC->SSC_RHR;
			r = MAX(r & 0xff, r >> 8); 
			if (r >= 240) {
				if (++trigger_cnt > triggersToSkip)
					break;
			} 
		}
	}

	if(!BUTTON_PRESS()) {
		int waitcount = samplesToSkip; // lets wait 40000 ticks of pck0
		while(waitcount != 0) {
			if(AT91C_BASE_SSC->SSC_SR & (AT91C_SSC_RXRDY))
				waitcount--;
		}
		optimizedSnoop();
		Dbprintf("Trigger kicked! Value: %d, Dumping Samples Hispeed now.", r);
	}

	DbpString("HF Snoop end");
	FpgaWriteConfWord(FPGA_MAJOR_MODE_OFF);
	LED_D_OFF();
}

