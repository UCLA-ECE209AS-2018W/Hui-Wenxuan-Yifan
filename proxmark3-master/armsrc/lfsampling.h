#ifndef LFSAMPLING_H
#define LFSAMPLING_H

/**
* acquisition of Cotag LF signal. Similar to other LF,  since the Cotag has such long datarate RF/384
* and is Manchester?,  we directly gather the manchester data into bigbuff
**/
void doCotagAcquisition(size_t sample_size);
uint32_t doCotagAcquisitionManchester(void);

/**
* Initializes the FPGA for reader-mode (field on), and acquires the samples.
* @return number of bits sampled
**/
uint32_t SampleLF(bool silent, int sample_size);

/**
* Initializes the FPGA for snoop-mode (field off), and acquires the samples.
* @return number of bits sampled
**/
uint32_t SnoopLF();

// adds sample size to default options
uint32_t DoPartialAcquisition(int trigger_threshold, bool silent, int sample_size, int cancel_after);

/**
 * @brief Does sample acquisition, ignoring the config values set in the sample_config.
 * This method is typically used by tag-specific readers who just wants to read the samples
 * the normal way
 * @param trigger_threshold
 * @param silent
 * @return number of bits sampled
 */
uint32_t DoAcquisition_default(int trigger_threshold, bool silent);
/**
 * @brief Does sample acquisition, using the config values set in the sample_config.
 * @param trigger_threshold
 * @param silent
 * @return number of bits sampled
 */

uint32_t DoAcquisition_config(bool silent, int sample_size);

/**
* Setup the FPGA to listen for samples. This method downloads the FPGA bitstream
* if not already loaded, sets divisor and starts up the antenna.
* @param divisor : 1, 88> 255 or negative ==> 134.8 KHz
* 				   0 or 95 ==> 125 KHz
*
**/
void LFSetupFPGAForADC(int divisor, bool lf_field);

/**
 * Called from the USB-handler to set the sampling configuration
 * The sampling config is used for std reading and snooping.
 *
 * Other functions may read samples and ignore the sampling config,
 * such as functions to read the UID from a prox tag or similar.
 *
 * Values set to '0' implies no change (except for averaging)
 * @brief setSamplingConfig
 * @param sc
 */
void setSamplingConfig(sample_config *sc);

sample_config * getSamplingConfig();

void printConfig();


#endif // LFSAMPLING_H
