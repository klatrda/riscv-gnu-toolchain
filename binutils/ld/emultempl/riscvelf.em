# This shell script emits a C file. -*- C -*-
#   Copyright 2004, 2006, 2007, 2008 Free Software Foundation, Inc.
#
# This file is part of the GNU Binutils.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston,
# MA 02110-1301, USA.

fragment <<EOF

#include "ldmain.h"
#include "ldctor.h"
#include "elf/riscv.h"
#include "elfxx-riscv.h"

#define _WITH_PULP_CHIP_INFO_FUNCT_
#include "../../src/gcc/gcc/config/riscv/riscv-opts.h"

static int TRACE = 0;

static int Warn_Chip_Info = 0;
static int Error_Chip_Info = 0;

static struct Pulp_Target_Chip Pulp_Chip = {PULP_CHIP_NONE, PULP_NONE, -1, -1, -1, -1, -1};
static struct Pulp_Target_Chip DefChipInfo = {PULP_CHIP_NONE, PULP_NONE, 0, 1, 1024*256, 64*1024, 0};

static void
riscv_elf_after_open(void)

{
  	gld${EMULATION_NAME}_after_open ();
}

#define PULPINFO_NAME "Pulp_Info"
#define PULPINFO_NAMESZ 10
#define PULPINFO_TYPE 1

static int MergeChipInfo(struct Pulp_Target_Chip *Cur, struct Pulp_Target_Chip *Ref, int Final)

{
	int Ok = 1;
	if (Ref->chip == PULP_CHIP_NONE) Ref->chip = Cur->chip;
	else if (Ref->chip != Cur->chip && Cur->chip != PULP_CHIP_NONE) {
		if (Warn_Chip_Info || Error_Chip_Info)
			einfo(_("Pulp Chip Info: Chip Type: Can't mix %s with %s\n"), PulpChipImage(Ref->chip), PulpChipImage(Cur->chip));
		Ok=0;
	}
	if (Ref->processor == PULP_NONE) Ref->processor = Cur->processor;
	else if (Ref->processor != Cur->processor && Cur->processor != PULP_NONE) {
		if (Pulp_Check_Processor_Compatibility(Cur->processor, Ref->processor) == 0) {
			if (Warn_Chip_Info || Error_Chip_Info)
				einfo(_("Pulp Chip Info: Processor Type: Can't mix %s with %s\n"),
					PulpProcessorImage(Ref->processor), PulpProcessorImage(Cur->processor));
			Ok=0;
		}
	}
	if (Ref->Pulp_PE == -1) Ref->Pulp_PE = Cur->Pulp_PE;
	else if (Ref->Pulp_PE != Cur->Pulp_PE && Cur->Pulp_PE != -1) {
		if (Warn_Chip_Info || Error_Chip_Info)
			einfo(_("Pulp Chip Info: Number of PEs: Can't mix %d with %d\n"), Ref->Pulp_PE, Cur->Pulp_PE);
		Ok=0;
	}
	if (Ref->Pulp_FC == -1) Ref->Pulp_FC = Cur->Pulp_FC;
	else if (Ref->Pulp_FC != Cur->Pulp_FC && Cur->Pulp_FC != -1) {
		if (Warn_Chip_Info || Error_Chip_Info)
			einfo(_("Pulp Chip Info: Has FC: Can't mix %d with %d\n"), Ref->Pulp_FC, Cur->Pulp_FC);
		Ok=0;
	}
	if (Final) {
		if (Cur->Pulp_L2_Size != -1) Ref->Pulp_L2_Size = Cur->Pulp_L2_Size;
	} else if (Ref->Pulp_L2_Size == -1) Ref->Pulp_L2_Size = Cur->Pulp_L2_Size;

	if (Final) {
		if (Cur->Pulp_L1_Cluster_Size != -1) Ref->Pulp_L1_Cluster_Size = Cur->Pulp_L1_Cluster_Size;
	} else if (Ref->Pulp_L1_Cluster_Size == -1) Ref->Pulp_L1_Cluster_Size = Cur->Pulp_L1_Cluster_Size;

	if (Final) {
		if (Cur->Pulp_L1_FC_Size != -1) Ref->Pulp_L1_FC_Size = Cur->Pulp_L1_FC_Size;
	} else if (Ref->Pulp_L1_FC_Size == -1) Ref->Pulp_L1_FC_Size = Cur->Pulp_L1_FC_Size;
	return Ok;
}

static void
riscv_elf_before_allocation (void)
{
	char CharBuff[1024];
	bfd *b, *first_b = NULL;
	bfd *ImportBfd;
	struct bfd_section *s, *first_s = NULL;
	struct Pulp_Target_Chip ChipInfo = {PULP_CHIP_NONE, PULP_NONE, -1, -1, -1, -1, -1};
	struct Pulp_Target_Chip CurChipInfo;
	int Error = 0;
	int NoMerge = 0;
	unsigned int SecNameSize, SecRelocSize, NImport=0, ExportSize;
	char *ExportNames = NULL;



	if (TRACE) fprintf(stderr, "Linker Passed Config: %s\n", PulpChipInfoImage(&Pulp_Chip, CharBuff));

	for (b = link_info.input_bfds; b; b = b->link.next) {
		if (!first_b) first_b = b;
		s = bfd_get_section_by_name (b, ".Pulp_Chip.Info");
		if (s) {
			long size = s->size;
			char *Pt, *buf = xmalloc (size);
			bfd_get_section_contents (b, s, buf, 0, size);
			if (size>0) {
				Pt = buf + 12 + PULPINFO_NAMESZ;
				if (ExtractChipInfo(Pt, &CurChipInfo) == 0) {
					if (Warn_Chip_Info || Error_Chip_Info)
						einfo(_("Incorrect .Pulp_Chip.Info section found in %s\n"), b->filename);
					if (Error_Chip_Info) Error++;
				}

				if (TRACE)
					fprintf(stderr, "Found Chip Info Section in In BFD %s: %s\n",
						b->filename, PulpChipInfoImage(&CurChipInfo, CharBuff));

				if (MergeChipInfo(&CurChipInfo, &ChipInfo, 0) == 0) {
					if (Warn_Chip_Info || Error_Chip_Info)
						einfo(_("Can't merge .Pulp_Chip.Info section in %s with current\n"), b->filename);
					if (Error_Chip_Info) Error++;
					NoMerge=1;
				}
			} else {
				Pt = buf;
				if (Warn_Chip_Info || Error_Chip_Info)
					einfo(_("Found Empty Chip Info Section in In BFD %s: %s"), b->filename, Pt);
				if (Error_Chip_Info) Error++;
			}
			free (buf);
			if (!first_s) first_s = s;
			s->size = 0;
			s->flags |= SEC_EXCLUDE;
		} else {
			if (Warn_Chip_Info||Error_Chip_Info) einfo(_("No Chip Info Section in In BFD %s"), b->filename);
			if (Error_Chip_Info) Error++;
		}
	}
	if (TRACE) fprintf(stderr, "Merged Config, before applying linker one's: %s\n", PulpChipInfoImage(&ChipInfo, CharBuff));

	if (MergeChipInfo(&Pulp_Chip, &ChipInfo, 1) == 0) {
		if (Warn_Chip_Info||Error_Chip_Info) einfo(_("Can't merge .Pulp_Chip.Info from sections with linker passed infos\n"));
		if (Error_Chip_Info) Error++;
		NoMerge=1;
	}

	if ((Pulp_Chip.chip != PULP_CHIP_NONE) && NoMerge) {
		einfo(_("-mchip given to linker but can't merge .Pulp_Chip.Info from sections with linker passed infos\n"));
		Error++;
	}

	if (TRACE) fprintf(stderr, "Merged Config, Final: %s\n", PulpChipInfoImage(&ChipInfo, CharBuff));

	s = first_s;
	if (!s && first_b) {
		s = bfd_make_section_with_flags (first_b, ".Pulp_Chip.Info", SEC_HAS_CONTENTS | SEC_READONLY);
		if (!s) {
			einfo(_("%F Failed to create output .Pulp_Chip.Info section\n"));
		}
	}
	if (Error) {
		einfo(_("%F Linker aborted due to .Pulp_Chip.Info unresolvable conflicts\n"));
	}
	if (s) {
		int size;
		char *data;

		s->flags &= ~SEC_EXCLUDE;
		s->flags |= SEC_IN_MEMORY;

		data = xmalloc(512);
		data = PulpChipInfoImage(&ChipInfo, data);

		size = strlen(data) + 1;
		do data[size++] = 0; while ((size & 3) != 0);

		s->size = 12 + PULPINFO_NAMESZ + size;
		s->contents = xmalloc(s->size);
		bfd_put_32 (s->owner, PULPINFO_NAMESZ, s->contents + 0);
		bfd_put_32 (s->owner, size, s->contents + 4);
		bfd_put_32 (s->owner, PULPINFO_TYPE, s->contents + 8);
		memcpy (s->contents + 12, PULPINFO_NAME, PULPINFO_NAMESZ);
		memcpy (s->contents + 12 + PULPINFO_NAMESZ, data, size);
		free (data);
	}

	{
	        struct bfd_link_hash_entry *h = NULL;
	
	        h = bfd_link_hash_lookup (link_info.hash, "pulp__PE", TRUE, FALSE, TRUE);
	        lang_update_definedness ("pulp__PE", h);
	        if (NoMerge) h->u.def.value = DefChipInfo.Pulp_PE; else h->u.def.value = ChipInfo.Pulp_PE;
	        h->type = bfd_link_hash_defined; h->u.def.section = abs_output_section->bfd_section;
	
	        h = bfd_link_hash_lookup (link_info.hash, "pulp__FC", TRUE, FALSE, TRUE);
	        lang_update_definedness ("pulp__FC", h);
	        if (NoMerge) h->u.def.value = DefChipInfo.Pulp_FC; else h->u.def.value = ChipInfo.Pulp_FC;
	        h->type = bfd_link_hash_defined; h->u.def.section = abs_output_section->bfd_section;
	
	        h = bfd_link_hash_lookup (link_info.hash, "pulp__L2", TRUE, FALSE, TRUE);
	        lang_update_definedness ("pulp__L2", h);
	        if (NoMerge) h->u.def.value = DefChipInfo.Pulp_L2_Size; else h->u.def.value = ChipInfo.Pulp_L2_Size;
	        h->type = bfd_link_hash_defined; h->u.def.section = abs_output_section->bfd_section;
	
	        h = bfd_link_hash_lookup (link_info.hash, "pulp__L1CL", TRUE, FALSE, TRUE);
	        lang_update_definedness ("pulp__L1CL", h);
	        if (NoMerge) h->u.def.value = DefChipInfo.Pulp_L1_Cluster_Size; else h->u.def.value = ChipInfo.Pulp_L1_Cluster_Size;
	        h->type = bfd_link_hash_defined; h->u.def.section = abs_output_section->bfd_section;
	
	        h = bfd_link_hash_lookup (link_info.hash, "pulp__L1FC", TRUE, FALSE, TRUE);
	        lang_update_definedness ("pulp__L1FC", h);
	        if (NoMerge) h->u.def.value = DefChipInfo.Pulp_L1_FC_Size; else h->u.def.value = ChipInfo.Pulp_L1_FC_Size;
	        h->type = bfd_link_hash_defined; h->u.def.section = abs_output_section->bfd_section;
	
	}


	ExportSize = 0;
	for (b = link_info.input_bfds; b; b = b->link.next) {
		if (s = bfd_get_section_by_name (b, ".pulp.export")) {
			int i;
			char *Content = xmalloc(s->size);
			char *Name;

			if (TRACE) fprintf(stderr, "Adding %d bytes to .pulp.export, Total: %d\n", s->size, s->size+ExportSize);
			ExportSize += (int) s->size;

			bfd_get_section_contents (b, s, Content, 0, s->size);
			Name = Content;
			for (i = 0; i<s->size; i++) {
				if (Content[i] == 0) {
					InsertExportEntry(Name);
					Name = Content + i + 1;
				}
			}
			free(Content);
		}
	}
	ExportSize = ExportSectionSize(NULL);
	if (ExportSize) {
		if (TRACE) fprintf(stderr, "Final Size of .pulp.export: %d\n", ExportSize);
		first_s = NULL;
		for (b = link_info.input_bfds; b; b = b->link.next) {
			if (s = bfd_get_section_by_name (b, ".pulp.export")) {
				if (!first_s) {
					s->flags &= ~(SEC_EXCLUDE|SEC_ALLOC); s->flags |= (SEC_IN_MEMORY|SEC_READONLY);
					s->size = ExportSize;
        				s->contents = xmalloc(s->size);
					first_s = s;
				} else {
					s->size = 0; s->flags |= SEC_EXCLUDE;
				}
			}
		}
	}

	PulpImportSectionsSize(0, &SecNameSize, &SecRelocSize, &NImport, TRUE);

	if (TRACE) fprintf(stderr, "ELF Before Alloc: Import Name Size: %d, Import Reloc Size: %d, Imports: %d\n",
				   SecNameSize, SecRelocSize, NImport);

	first_s = NULL;
	for (b = link_info.input_bfds; b; b = b->link.next) {
		s = bfd_get_section_by_name (b, ".pulp.import.names");
		if (s) {
			if (!first_s) first_s = s;
			s->size = 0; s->flags |= SEC_EXCLUDE;
		}
	}
	s = first_s;
	if (s) {
		s->flags &= ~(SEC_EXCLUDE|SEC_ALLOC); s->flags |= (SEC_IN_MEMORY|SEC_READONLY);
		s->size = SecNameSize;
        	s->contents = xmalloc(s->size);
		if (TRACE) fprintf(stderr, ".pulp.import.names Found\n");
	} else if (TRACE) fprintf(stderr, ".pulp.import.names NOT Found\n");

	first_s = NULL;
	for (b = link_info.input_bfds; b; b = b->link.next) {
		s = bfd_get_section_by_name (b, ".pulp.import.relocs");
		if (s) {
			if (!first_s) first_s = s;
			s->size = 0; s->flags |= SEC_EXCLUDE;
		}
	}
	s = first_s;
	if (s) {
		s->flags &= ~(SEC_EXCLUDE|SEC_ALLOC); s->flags |= (SEC_IN_MEMORY|SEC_READONLY);
		s->size = SecRelocSize;
        	s->contents = xmalloc(s->size);
		if (TRACE) fprintf(stderr, ".pulp.import.relocs Found\n");
	} else if (TRACE) fprintf(stderr, ".pulp.import.relocs NOT Found\n");


  	gld${EMULATION_NAME}_before_allocation ();

  	if (link_info.discard == discard_sec_merge) link_info.discard = discard_l;

  	/* We always need at least some relaxation to handle code alignment.  */
  	if (RELAXATION_DISABLED_BY_USER)
    		TARGET_ENABLE_RELAXATION;
  	else
    		ENABLE_RELAXATION;

  	link_info.relax_pass = 2;
}

static void
gld${EMULATION_NAME}_after_allocation (void)
{
  	int need_layout = 0;
	struct bfd_section *s;
	bfd *b = NULL;

  	/* Don't attempt to discard unused .eh_frame sections until the final link,
     	as we can't reliably tell if they're used until after relaxation.  */
  	if (!link_info.relocatable) {
      		need_layout = bfd_elf_discard_info (link_info.output_bfd, &link_info);
      		if (need_layout < 0) {
	  		einfo ("%X%P: .eh_frame/.stab edit: %E\n");
	  		return;
		}
    	}
	for (b = link_info.input_bfds; b; b = b->link.next) {
		s = bfd_get_section_by_name (b, ".pulp.import");
		if (s) s->flags |= SEC_EXCLUDE;
	}
  	gld${EMULATION_NAME}_map_segments (need_layout);
	PulpRegisterSymbolEntry(entry_symbol, entry_from_cmdline);
}

static void
gld${EMULATION_NAME}_finish (void)
{

	if (TRACE) {
		struct bfd_section *s;

		s = bfd_get_section_by_name (link_info.output_bfd, ".Pulp_Chip.Info");
		if (s) fprintf(stderr, "Found Chip Info in out bfd: Size=%d, EntSize=%d, Contents:%s\n",
				       (int) s->size, (int) s->entsize, (s->contents)?"Yes":"No" );
	}

	if (! link_info.relocatable) {
  	}
  	finish_default ();
}
static void ParsePulpArch(const char *arg)

{
  char *uppercase = xstrdup (arg);
  char *p = uppercase;
  const char *all_subsets = "IMAFDC";
  int i;

  for (i = 0; uppercase[i]; i++) uppercase[i] = TOUPPER (uppercase[i]);

  if (strncmp (p, "RV32", 4) == 0) p += 4;
  else if (strncmp (p, "RV64", 4) == 0) p += 4;
  else if (strncmp (p, "RV", 2) == 0) p += 2;

  switch (*p) {
      case 'I':
        break;
      case 'G':
        p++;
        /* Fall through.  */
      case '\0':
        break;
      default:
        einfo(_("%F I must be the first ISA subset name specified (got %c)"), *p);
    }
  while (*p) {
      if (*p == 'X') {
	  int Len;
          char *subset = xstrdup (p), *q = subset;
          while (*++q != '\0' && *q != '_') ;
          *q = '\0';
	  switch (PulpDecodeCpu(p+1, &Len)) {
		case PULP_V0:
                  	if (Pulp_Chip.processor == PULP_NONE || Pulp_Chip.processor == PULP_V0) Pulp_Chip.processor = PULP_V0;
                  	else einfo(_("%F -Xpulpv0: pulp architecture is already defined as %s"), PulpProcessorImage(Pulp_Chip.processor));
			break;
		case PULP_V1:
                  	if (Pulp_Chip.processor == PULP_NONE || Pulp_Chip.processor == PULP_V1) Pulp_Chip.processor = PULP_V1;
                  	else einfo(_("%F -Xpulpv1: pulp architecture is already defined as %s"), PulpProcessorImage(Pulp_Chip.processor));
			break;
		case PULP_V2:
                  	if (Pulp_Chip.processor == PULP_NONE || Pulp_Chip.processor == PULP_V2) Pulp_Chip.processor = PULP_V2;
                  	else einfo(_("%F -Xpulpv2: pulp architecture is already defined as %s"), PulpProcessorImage(Pulp_Chip.processor));
			break;
		case PULP_V3:
                  	if (Pulp_Chip.processor == PULP_NONE || Pulp_Chip.processor == PULP_V3) Pulp_Chip.processor = PULP_V3;
                  	else einfo(_("%F -Xpulpv3: pulp architecture is already defined as %s"), PulpProcessorImage(Pulp_Chip.processor));
			break;
		case PULP_RISCV:
                  	if (Pulp_Chip.processor == PULP_NONE || Pulp_Chip.processor == PULP_RISCV) Pulp_Chip.processor = PULP_RISCV;
                  	else einfo(_("%F -Xriscv: pulp architecture is already defined as %s"), PulpProcessorImage(Pulp_Chip.processor));
			break;
		case PULP_NONE:
			if (Len==0) {
                  		einfo(_ ("%F -march=%s: unsupported ISA substring %s"), arg, p); return;
			}
			break;
		default:
			break;
          }
          p += strlen (subset);
          free (subset);
        } else if (*p == '_') {
          p++;
        } else if ((all_subsets = strchr (all_subsets, *p)) != NULL) {
          all_subsets++;
          p++;
        }
      else
        einfo(_("%F unsupported ISA subset %c"), *p);
    }
    if (Pulp_Chip.processor == PULP_NONE) Pulp_Chip.processor = PULP_RISCV;
}

static void ParsePulpChip(const char *arg)

{
  char *uppercase = xstrdup (arg);
  char *p = uppercase;
  int i;

  for (i = 0; uppercase[i]; i++) uppercase[i] = TOUPPER (uppercase[i]);

  if (strncmp (p, "PULPINO", 7) == 0) {
        ParsePulpArch ("IXpulpv1");
        UpdatePulpChip(&Pulp_Chip, &Pulp_Defined_Chips[PULP_CHIP_PULPINO]);
  } else if (strncmp (p, "HONEY", 5) == 0) {
        ParsePulpArch ("IXpulpv0");
        UpdatePulpChip(&Pulp_Chip, &Pulp_Defined_Chips[PULP_CHIP_HONEY]);
  } else {
        einfo(_("%F Unsupported pulp chip %s"), arg);
  }
}

EOF

PARSE_AND_LIST_PROLOGUE='
#define OPTION_CHIP		301
#define OPTION_PROCESSOR	302
#define OPTION_PE		303
#define OPTION_FC		304
#define OPTION_L2		305
#define OPTION_L1CL		306
#define OPTION_L1FC		307
#define OPTION_WARN_CHIP_INFO	308
#define OPTION_ERROR_CHIP_INFO	309
#define OPTION_COMP_LINK	310
#define OPTION_DUMP_IE_SECT	311
'
PARSE_AND_LIST_LONGOPTS='
  { "mchip", required_argument, NULL, OPTION_CHIP},
  { "march", required_argument, NULL, OPTION_PROCESSOR},
  { "mPE", required_argument, NULL, OPTION_PE},
  { "mFC", required_argument, NULL, OPTION_FC},
  { "mL2", required_argument, NULL, OPTION_L2},
  { "mL1Cl", required_argument, NULL, OPTION_L1CL},
  { "mL1Fc", required_argument, NULL, OPTION_L1FC},
  { "mWci", no_argument, NULL, OPTION_WARN_CHIP_INFO},
  { "mEci", no_argument, NULL, OPTION_ERROR_CHIP_INFO},
  { "mComp", no_argument, NULL, OPTION_COMP_LINK},
  { "mDIE", required_argument, NULL, OPTION_DUMP_IE_SECT},
'

PARSE_AND_LIST_OPTIONS='
  fprintf (file, _("  -mchip=<name>       Set targeted Pulp chip to <name>\n"));
  fprintf (file, _("  -march=<name>       Set targeted Pulp processor to ISA <name>\n"));
  fprintf (file, _("  -PE=<value>         Set Pulp number of cluster processors to <value>\n"));
  fprintf (file, _("  -FC=<value>         If value != 0 targeted Pulp chip has a fabric controller\n"));
  fprintf (file, _("  -L2=<value>         Set targeted Pulp chip L2 memory size to <value>\n"));
  fprintf (file, _("  -mL1Cl=<value>      Set targeted Pulp chip L1 cluster memory size to <value>\n"));
  fprintf (file, _("  -mL1Fc=<value>      Set targeted Pulp chip L1 fabric controler memry size to <value>\n"));
  fprintf (file, _("  -mWci               Emit warning when no chip info is found in a bfd or when non mergeable chip info sections are detected\n"));
  fprintf (file, _("  -mEci               Emit warning and abort when no chip info is found in a bfd or when non mergeable chip info sections are detected\n"));
  fprintf (file, _("  -mComp              Link a component, export section contains offset relative to segment and not absolute addresses\n"));
  fprintf (file, _("  -mDIE=<value>       Dump import/export sections. 1: Dump only, 2: Sections in C only, 3: Both\n"));
'

PARSE_AND_LIST_ARGS_CASES='
   case OPTION_CHIP:
     ParsePulpChip(optarg);
     break;
   case OPTION_PROCESSOR:
     ParsePulpArch(optarg);
     break;
   case OPTION_PE:
     Pulp_Chip.Pulp_PE = atoi(optarg);
     break;
   case OPTION_FC:
     Pulp_Chip.Pulp_FC = atoi(optarg);
     break;
   case OPTION_L2:
     Pulp_Chip.Pulp_L2_Size = atoi(optarg);
     break;
   case OPTION_L1CL:
     Pulp_Chip.Pulp_L1_Cluster_Size = atoi(optarg);
     break;
   case OPTION_L1FC:
     Pulp_Chip.Pulp_L1_FC_Size = atoi(optarg);
     break;
   case OPTION_WARN_CHIP_INFO:
     Warn_Chip_Info = 1;
     break;
   case OPTION_ERROR_CHIP_INFO:
     Error_Chip_Info = 1;
     break;
   case OPTION_COMP_LINK:
     ComponentMode = TRUE;
     break;
   case OPTION_DUMP_IE_SECT:
     DumpImportExportSections = atoi(optarg);
     break;
'

LDEMUL_AFTER_OPEN=riscv_elf_after_open
LDEMUL_BEFORE_ALLOCATION=riscv_elf_before_allocation
LDEMUL_AFTER_ALLOCATION=gld${EMULATION_NAME}_after_allocation
LDEMUL_FINISH=gld${EMULATION_NAME}_finish

