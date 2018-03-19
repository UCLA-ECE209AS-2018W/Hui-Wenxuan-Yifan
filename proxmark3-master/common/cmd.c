/*
 * Proxmark send and receive commands
 *
 * Copyright (c) 2012, Roel Verdult
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holders nor the
 * names of its contributors may be used to endorse or promote products
 * derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file cmd.c
 * @brief
 */

#include "cmd.h"
#include "string.h"
#include "proxmark3.h"

bool cmd_receive(UsbCommand* cmd) {
 
  // Check if there is a usb packet available
  if (!usb_poll()) return false;
  
  // Try to retrieve the available command frame
  size_t rxlen = usb_read((byte_t*)cmd,sizeof(UsbCommand));

  // Check if the transfer was complete
  if (rxlen != sizeof(UsbCommand)) return false;
  
  // Received command successfully
  return true;
}

bool cmd_send(uint32_t cmd, uint32_t arg0, uint32_t arg1, uint32_t arg2, void* data, size_t len) {
  UsbCommand txcmd;

  for (size_t i=0; i<sizeof(UsbCommand); i++) {
    ((byte_t*)&txcmd)[i] = 0x00;
  }
  
  // Compose the outgoing command frame
  txcmd.cmd = cmd;
  txcmd.arg[0] = arg0;
  txcmd.arg[1] = arg1;	
  txcmd.arg[2] = arg2;

  // Add the (optional) content to the frame, with a maximum size of USB_CMD_DATA_SIZE
  if (data && len) {
    len = MIN(len,USB_CMD_DATA_SIZE);
    for (size_t i=0; i<len; i++) {
      txcmd.d.asBytes[i] = ((byte_t*)data)[i];
    }
  }
  
  // Send frame and make sure all bytes are transmitted
  if (usb_write((byte_t*)&txcmd,sizeof(UsbCommand)) != 0) return false;
  
  return true;
}


