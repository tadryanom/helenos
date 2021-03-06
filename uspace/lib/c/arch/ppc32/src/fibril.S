#
# Copyright (c) 2006 Martin Decky
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
# - Redistributions of source code must retain the above copyright
#   notice, this list of conditions and the following disclaimer.
# - Redistributions in binary form must reproduce the above copyright
#   notice, this list of conditions and the following disclaimer in the
#   documentation and/or other materials provided with the distribution.
# - The name of the author may not be used to endorse or promote products
#   derived from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
# OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
# IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
# NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
# THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#

.text

#include <abi/asmtool.h>
#include <libarch/regname.h>
#include <libarch/fibril_context.h>

FUNCTION_BEGIN(context_save)
	stw sp, CONTEXT_OFFSET_SP(r3)
	stw r2, CONTEXT_OFFSET_TLS(r3)
	stw r13, CONTEXT_OFFSET_R13(r3)
	stw r14, CONTEXT_OFFSET_R14(r3)
	stw r15, CONTEXT_OFFSET_R15(r3)
	stw r16, CONTEXT_OFFSET_R16(r3)
	stw r17, CONTEXT_OFFSET_R17(r3)
	stw r18, CONTEXT_OFFSET_R18(r3)
	stw r19, CONTEXT_OFFSET_R19(r3)
	stw r20, CONTEXT_OFFSET_R20(r3)
	stw r21, CONTEXT_OFFSET_R21(r3)
	stw r22, CONTEXT_OFFSET_R22(r3)
	stw r23, CONTEXT_OFFSET_R23(r3)
	stw r24, CONTEXT_OFFSET_R24(r3)
	stw r25, CONTEXT_OFFSET_R25(r3)
	stw r26, CONTEXT_OFFSET_R26(r3)
	stw r27, CONTEXT_OFFSET_R27(r3)
	stw r28, CONTEXT_OFFSET_R28(r3)
	stw r29, CONTEXT_OFFSET_R29(r3)
	stw r30, CONTEXT_OFFSET_R30(r3)
	stw r31, CONTEXT_OFFSET_R31(r3)
	
	mflr r4
	stw r4, CONTEXT_OFFSET_PC(r3)
	
	mfcr r4
	stw r4, CONTEXT_OFFSET_CR(r3)
	
	# context_save returns 1
	li r3, 1
	blr
FUNCTION_END(context_save)

FUNCTION_BEGIN(context_restore)
	lwz sp, CONTEXT_OFFSET_SP(r3)
	lwz r2, CONTEXT_OFFSET_TLS(r3)
	lwz r13, CONTEXT_OFFSET_R13(r3)
	lwz r14, CONTEXT_OFFSET_R14(r3)
	lwz r15, CONTEXT_OFFSET_R15(r3)
	lwz r16, CONTEXT_OFFSET_R16(r3)
	lwz r17, CONTEXT_OFFSET_R17(r3)
	lwz r18, CONTEXT_OFFSET_R18(r3)
	lwz r19, CONTEXT_OFFSET_R19(r3)
	lwz r20, CONTEXT_OFFSET_R20(r3)
	lwz r21, CONTEXT_OFFSET_R21(r3)
	lwz r22, CONTEXT_OFFSET_R22(r3)
	lwz r23, CONTEXT_OFFSET_R23(r3)
	lwz r24, CONTEXT_OFFSET_R24(r3)
	lwz r25, CONTEXT_OFFSET_R25(r3)
	lwz r26, CONTEXT_OFFSET_R26(r3)
	lwz r27, CONTEXT_OFFSET_R27(r3)
	lwz r28, CONTEXT_OFFSET_R28(r3)
	lwz r29, CONTEXT_OFFSET_R29(r3)
	lwz r30, CONTEXT_OFFSET_R30(r3)
	lwz r31, CONTEXT_OFFSET_R31(r3)
	
	lwz r4, CONTEXT_OFFSET_CR(r3)
	mtcr r4
	
	lwz r4, CONTEXT_OFFSET_PC(r3)
	mtlr r4
	
	# context_restore returns 0
	li r3, 0
	blr
FUNCTION_END(context_restore)
