//-----------------------------------------------------------------------------
// Copyright (C) 2011 Merlok
//
// This code is licensed to you under the terms of the GNU GPL, version 2 or,
// at your option, any later version. See the LICENSE.txt file for the text of
// the license.
//-----------------------------------------------------------------------------
// High frequency MIFARE commands
//-----------------------------------------------------------------------------

#ifndef CMDHFMF_H__
#define CMDHFMF_H__

#include "mifaredefault.h"

extern int CmdHFMF(const char *Cmd);

extern int CmdHF14AMfDbg(const char* cmd);
extern int CmdHF14AMfRdBl(const char* cmd);
extern int CmdHF14AMfURdBl(const char* cmd);
extern int CmdHF14AMfRdSc(const char* cmd);
extern int CmdHF14SMfURdCard(const char* cmd);
extern int CmdHF14AMfDump(const char* cmd);
extern int CmdHF14AMfRestore(const char* cmd);
extern int CmdHF14AMfWrBl(const char* cmd);
extern int CmdHF14AMfUWrBl(const char* cmd);
extern int CmdHF14AMfChk(const char* cmd);
extern int CmdHF14AMifare(const char* cmd);
extern int CmdHF14AMfNested(const char* cmd);
extern int CmdHF14AMfSniff(const char* cmd);
extern int CmdHF14AMf1kSim(const char* cmd);
extern int CmdHF14AMfEClear(const char* cmd);
extern int CmdHF14AMfEGet(const char* cmd);
extern int CmdHF14AMfESet(const char* cmd);
extern int CmdHF14AMfELoad(const char* cmd);
extern int CmdHF14AMfESave(const char* cmd);
extern int CmdHF14AMfECFill(const char* cmd);
extern int CmdHF14AMfEKeyPrn(const char* cmd);
extern int CmdHF14AMfCWipe(const char* cmd);
extern int CmdHF14AMfCSetUID(const char* cmd);
extern int CmdHF14AMfCSetBlk(const char* cmd);
extern int CmdHF14AMfCGetBlk(const char* cmd);
extern int CmdHF14AMfCGetSc(const char* cmd);
extern int CmdHF14AMfCLoad(const char* cmd);
extern int CmdHF14AMfCSave(const char* cmd);

#endif
