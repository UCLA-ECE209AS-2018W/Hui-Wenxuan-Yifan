//-----------------------------------------------------------------------------
// This code is licensed to you under the terms of the GNU GPL, version 2 or,
// at your option, any later version. See the LICENSE.txt file for the text of
// the license.
//-----------------------------------------------------------------------------
// LEFIC's obfuscation function
//-----------------------------------------------------------------------------

#ifndef __LEGIC_PRNG_H
#define __LEGIC_PRNG_H

#include <stdint.h>
extern void legic_prng_init(uint8_t init);
extern void legic_prng_forward(int count);
extern int  legic_prng_count();
extern uint8_t legic_prng_get_bit();

#endif

