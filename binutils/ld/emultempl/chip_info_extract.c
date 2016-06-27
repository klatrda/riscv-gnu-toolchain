#include "build-binutils-newlib/bfd/bfd.h"
#define _WITH_PULP_CHIP_INFO_FUNCT_
#include "gcc/gcc/config/riscv/riscv-opts.h"
#include <stdlib.h>

#define PULPINFO_NAME "Pulp_Info"
#define PULPINFO_NAMESZ 10
#define PULPINFO_TYPE 1



/*
   abfd: 	the elf file in which to search for the .Pulp_Chip.Info section
   ChipInfo:	Pointer to a Pulp_Target_Chip structure

   returns 1 on success, 0 on failure
*/

int PulpExtractChipInfo(bfd *Abfd, struct Pulp_Target_Chip *ChipInfo)

{
	struct bfd_section *s = bfd_get_section_by_name (Abfd, ".Pulp_Chip.Info");

	if (s) {
		long size = s->size;
		char *buf = malloc (size);
                bfd_get_section_contents (b, s, buf, 0, size);
		if ((size>0) && (ExtractChipInfo((buf + 12 + PULPINFO_NAMESZ), ChipInfo) != 0)) {
			free(buf);
			return 1;
		}
	}
	free(buf);
	return 0;
}

