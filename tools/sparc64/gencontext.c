#include <stdio.h>

typedef long long __u64;
typedef __u64 ipl_t;
typedef __u64 __address;

#define __sparc64_TYPES_H__
#define __ALIGN_H__

#include "../../arch/sparc64/include/stack.h"
#include "../../arch/sparc64/include/context.h"

#define FILENAME "../../arch/sparc64/include/context_offset.h"

int main(void)
{
	FILE *f;
	struct context *pctx = NULL;
	

	f = fopen(FILENAME,"w");
	if (!f) {
		perror(FILENAME);
		return 1;
	}

	fprintf(f, "/* This file is automatically generated by %s. */\n", __FILE__);	

	fprintf(f,"/* struct context */\n");
	fprintf(f,"#define OFFSET_O1  0x%x\n",((int)&pctx->o1) - (int )pctx);
	fprintf(f,"#define OFFSET_O2  0x%x\n",((int)&pctx->o2) - (int )pctx);
	fprintf(f,"#define OFFSET_O3  0x%x\n",((int)&pctx->o3) - (int )pctx);
	fprintf(f,"#define OFFSET_O4  0x%x\n",((int)&pctx->o4) - (int )pctx);
	fprintf(f,"#define OFFSET_O5  0x%x\n",((int)&pctx->o5) - (int )pctx);
	fprintf(f,"#define OFFSET_SP  0x%x\n",((int)&pctx->sp) - (int )pctx);
	fprintf(f,"#define OFFSET_PC  0x%x\n",((int)&pctx->pc) - (int )pctx);

	fclose(f);

	return 0;
}
