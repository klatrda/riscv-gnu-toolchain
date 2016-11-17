/* Subroutines used for code generation for RISC-V.
   Copyright (C) 2011-2014 Free Software Foundation, Inc.
   Contributed by Andrew Waterman (waterman@cs.berkeley.edu) at UC Berkeley.
   Based on MIPS target for GNU compiler.

   PULP family support contributed by Eric Flamand (eflamand@iis.ee.ethz.ch) at ETH-Zurich

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h"
#include "regs.h"
#include "hard-reg-set.h"
#include "insn-config.h"
#include "conditions.h"
#include "insn-attr.h"
#include "recog.h"
#include "output.h"
#include "hash-set.h"
#include "machmode.h"
#include "vec.h"
#include "double-int.h"
#include "input.h"
#include "alias.h"
#include "symtab.h"
#include "wide-int.h"
#include "inchash.h"
#include "tree.h"
#include "fold-const.h"
#include "varasm.h"
#include "stringpool.h"
#include "stor-layout.h"
#include "calls.h"
#include "function.h"
#include "hashtab.h"
#include "flags.h"
#include "statistics.h"
#include "real.h"
#include "fixed-value.h"
#include "expmed.h"
#include "dojump.h"
#include "explow.h"
#include "emit-rtl.h"
#include "stmt.h"
#include "expr.h"
#include "insn-codes.h"
#include "optabs.h"
#include "libfuncs.h"
#include "reload.h"
#include "tm_p.h"
#include "ggc.h"
#include "gstab.h"
#include "hash-table.h"
#include "debug.h"
#include "target.h"
#include "hw-doloop.h"
#include "target-def.h"
#include "common/common-target.h"
#include "langhooks.h"
#include "dominance.h"
#include "cfg.h"
#include "cfgrtl.h"
#include "cfganal.h"
#include "lcm.h"
#include "cfgbuild.h"
#include "cfgcleanup.h"
#include "predict.h"
#include "basic-block.h"
#include "sched-int.h"
#include "tree-ssa-alias.h"
#include "internal-fn.h"
#include "gimple-fold.h"
#include "tree-eh.h"
#include "gimple-expr.h"
#include "is-a.h"
#include "gimple.h"
#include "gimplify.h"
#include "bitmap.h"
#include "diagnostic.h"
#include "target-globals.h"
#include "opts.h"
#include "tree-pass.h"
#include "context.h"
#include "hash-map.h"
#include "plugin-api.h"
#include "ipa-ref.h"
#include "cgraph.h"
#include "builtins.h"
#include "rtl-iter.h"
#include <stdint.h>

/* True if X is an UNSPEC wrapper around a SYMBOL_REF or LABEL_REF.  */
#define UNSPEC_ADDRESS_P(X)					\
  (GET_CODE (X) == UNSPEC					\
   && XINT (X, 1) >= UNSPEC_ADDRESS_FIRST			\
   && XINT (X, 1) < UNSPEC_ADDRESS_FIRST + NUM_SYMBOL_TYPES)

/* Extract the symbol or label from UNSPEC wrapper X.  */
#define UNSPEC_ADDRESS(X) \
  XVECEXP (X, 0, 0)

/* Extract the symbol type from UNSPEC wrapper X.  */
#define UNSPEC_ADDRESS_TYPE(X) \
  ((enum riscv_symbol_type) (XINT (X, 1) - UNSPEC_ADDRESS_FIRST))

/* The maximum distance between the top of the stack frame and the
   value sp has when we save and restore registers.  This is set by the
   range  of load/store offsets and must also preserve stack alignment. */
#define RISCV_MAX_FIRST_STACK_STEP (IMM_REACH/2 - 16)

/* True if INSN is a riscv.md pattern or asm statement.  */
#define USEFUL_INSN_P(INSN)						\
  (NONDEBUG_INSN_P (INSN)						\
   && GET_CODE (PATTERN (INSN)) != USE					\
   && GET_CODE (PATTERN (INSN)) != CLOBBER				\
   && GET_CODE (PATTERN (INSN)) != ADDR_VEC				\
   && GET_CODE (PATTERN (INSN)) != ADDR_DIFF_VEC)

/* True if bit BIT is set in VALUE.  */
#define BITSET_P(VALUE, BIT) (((VALUE) & (1 << (BIT))) != 0)

/* Classifies an address.

   ADDRESS_REG
       A natural register + offset address.  The register satisfies
       riscv_valid_base_register_p and the offset is a const_arith_operand.

   ADDRESS_LO_SUM
       A LO_SUM rtx.  The first operand is a valid base register and
       the second operand is a symbolic address.

   ADDRESS_CONST_INT
       A signed 16-bit constant address.

   ADDRESS_SYMBOLIC:
       A constant symbolic address.  */
enum riscv_address_type {
  ADDRESS_REG,
  ADDRESS_LO_SUM,
  ADDRESS_CONST_INT,
  ADDRESS_SYMBOLIC,
  ADDRESS_REG_POST_INC,
  ADDRESS_REG_POST_DEC,
  ADDRESS_REG_POST_MODIFY,
  ADDRESS_REG_REG,
  ADDRESS_TINY_SYMBOL,
  ADDRESS_REG_TINY_SYMBOL
};

enum riscv_code_model riscv_cmodel = TARGET_DEFAULT_CMODEL;

/* for builtin pulpv2, we model vectors as opaque entities. Opaque helps to make the call style mode versatile */
static tree opaque_V4QI_type_node;
static tree opaque_V2HI_type_node;

/* Macros to create an enumeration identifier for a function prototype.  */
#define RISCV_FTYPE_NAME1(A, B) RISCV_##A##_FTYPE_##B
#define RISCV_FTYPE_NAME2(A, B, C) RISCV_##A##_FTYPE_##B##_##C
#define RISCV_FTYPE_NAME3(A, B, C, D) RISCV_##A##_FTYPE_##B##_##C##_##D
#define RISCV_FTYPE_NAME4(A, B, C, D, E) RISCV_##A##_FTYPE_##B##_##C##_##D##_##E
#define RISCV_FTYPE_NAME5(A, B, C, D, E, F) RISCV_##A##_FTYPE_##B##_##C##_##D##_##E##_##F
#define RISCV_FTYPE_NAME6(A, B, C, D, E, F, G) RISCV_##A##_FTYPE_##B##_##C##_##D##_##E##_##F##_##G

/* Classifies the prototype of a built-in function.  */
enum riscv_function_type {
#define DEF_RISCV_FTYPE(NARGS, LIST) RISCV_FTYPE_NAME##NARGS LIST,
#include "config/riscv/riscv-ftypes.def"
#undef DEF_RISCV_FTYPE
  RISCV_MAX_FTYPE_MAX
};

/* Specifies how a built-in function should be converted into rtl.  */
enum riscv_builtin_type {
  /* The function corresponds directly to an .md pattern.  The return
     value is mapped to operand 0 and the arguments are mapped to
     operands 1 and above.  */
  RISCV_BUILTIN_DIRECT,

  /* The function corresponds directly to an .md pattern.  There is no return
     value and the arguments are mapped to operands 0 and above.  */
  RISCV_BUILTIN_DIRECT_NO_TARGET
};

/* Information about a function's frame layout.  */
struct GTY(())  riscv_frame_info {
  /* The size of the frame in bytes.  */
  HOST_WIDE_INT total_size;

  /* Bit X is set if the function saves or restores GPR X.  */
  unsigned int mask;

  /* Likewise FPR X.  */
  unsigned int fmask;

  /* How much the GPR save/restore routines adjust sp (or 0 if unused).  */
  unsigned save_libcall_adjustment;

  /* Offsets of fixed-point and floating-point save areas from frame bottom */
  HOST_WIDE_INT gp_sp_offset;
  HOST_WIDE_INT fp_sp_offset;

  /* Offset of virtual frame pointer from stack pointer/frame bottom */
  HOST_WIDE_INT frame_pointer_offset;

  /* Offset of hard frame pointer from stack pointer/frame bottom */
  HOST_WIDE_INT hard_frame_pointer_offset;

  /* The offset of arg_pointer_rtx from the bottom of the frame.  */
  HOST_WIDE_INT arg_pointer_offset;
  bool is_it;
};

struct GTY(())  machine_function {
  /* The number of extra stack bytes taken up by register varargs.
     This area is allocated by the callee at the very top of the frame.  */
  int varargs_size;

  /* Cached return value of leaf_function_p.  <0 if false, >0 if true.  */
  int is_leaf;

  /* The current frame information, calculated by riscv_compute_frame_info.  */
  struct riscv_frame_info frame;

  int has_hardware_loops; // Notify doloop pass that at least 1 hw loop has been created
  int is_interrupt;
  int is_pure_interrupt;
  int contains_call;

};

/* Information about a single argument.  */
struct riscv_arg_info {
  /* True if the argument is passed in a floating-point register, or
     would have been if we hadn't run out of registers.  */
  bool fpr_p;

  /* The number of words passed in registers, rounded up.  */
  unsigned int reg_words;

  /* For EABI, the offset of the first register from GP_ARG_FIRST or
     FP_ARG_FIRST.  For other ABIs, the offset of the first register from
     the start of the ABI's argument structure (see the CUMULATIVE_ARGS
     comment for details).

     The value is MAX_ARGS_IN_REGISTERS if the argument is passed entirely
     on the stack.  */
  unsigned int reg_offset;

  /* The number of words that must be passed on the stack, rounded up.  */
  unsigned int stack_words;

  /* The offset from the start of the stack overflow area of the argument's
     first stack word.  Only meaningful when STACK_WORDS is nonzero.  */
  unsigned int stack_offset;
};

/* Information about an address described by riscv_address_type.

   ADDRESS_CONST_INT
       No fields are used.

   ADDRESS_REG
       REG is the base register and OFFSET is the constant offset.

   ADDRESS_LO_SUM
       REG and OFFSET are the operands to the LO_SUM and SYMBOL_TYPE
       is the type of symbol it references.

   ADDRESS_SYMBOLIC
       SYMBOL_TYPE is the type of symbol that the address references.  */
struct riscv_address_info {
  enum riscv_address_type type;
  rtx reg;
  rtx offset;
  enum riscv_symbol_type symbol_type;
  enum machine_mode mode;
};

/* One stage in a constant building sequence.  These sequences have
   the form:

	A = VALUE[0]
	A = A CODE[1] VALUE[1]
	A = A CODE[2] VALUE[2]
	...

   where A is an accumulator, each CODE[i] is a binary rtl operation
   and each VALUE[i] is a constant integer.  CODE[0] is undefined.  */
struct riscv_integer_op {
  enum rtx_code code;
  unsigned HOST_WIDE_INT value;
};

/* The largest number of operations needed to load an integer constant.
   The worst case is LUI, ADDI, SLLI, ADDI, SLLI, ADDI, SLLI, ADDI,
   but we may attempt and reject even worse sequences.  */
#define RISCV_MAX_INTEGER_OPS 32

/* Costs of various operations on the different architectures.  */

struct riscv_tune_info
{
  unsigned short fp_add[2];
  unsigned short fp_mul[2];
  unsigned short fp_div[2];
  unsigned short int_mul[2];
  unsigned short int_div[2];
  unsigned short issue_rate;
  unsigned short branch_cost;
  unsigned short fp_to_int_cost;
  unsigned short memory_cost;
};

/* Information about one CPU we know about.  */
struct riscv_cpu_info {
  /* This CPU's canonical name.  */
  const char *name;

  /* The RISC-V ISA and extensions supported by this CPU.  */
  const char *isa;

  /* Tuning parameters for this CPU.  */
  const struct riscv_tune_info *tune_info;
};

/* Global variables for machine-dependent things.  */

/* Which tuning parameters to use.  */
static const struct riscv_tune_info *tune_info;

/* Index [M][R] is true if register R is allowed to hold a value of mode M.  */
bool riscv_hard_regno_mode_ok[(int) MAX_MACHINE_MODE][FIRST_PSEUDO_REGISTER];

/* riscv_lo_relocs[X] is the relocation to use when a symbol of type X
   appears in a LO_SUM.  It can be null if such LO_SUMs aren't valid or
   if they are matched by a special .md file pattern.  */
const char *riscv_lo_relocs[NUM_SYMBOL_TYPES];

/* Likewise for HIGHs.  */
const char *riscv_hi_relocs[NUM_SYMBOL_TYPES];

/* Index R is the smallest register class that contains register R.  */
const enum reg_class riscv_regno_to_class[FIRST_PSEUDO_REGISTER] = {
  GR_REGS,	GR_REGS,	GR_REGS,	GR_REGS,
  GR_REGS,	T_REGS,		T_REGS,		T_REGS,
  GR_REGS,	GR_REGS,	GR_REGS,	GR_REGS,
  GR_REGS,	GR_REGS,	GR_REGS,	GR_REGS,
  GR_REGS,	GR_REGS, 	GR_REGS,	GR_REGS,
  GR_REGS,	GR_REGS,	GR_REGS,	GR_REGS,
  GR_REGS,	GR_REGS,	GR_REGS,	GR_REGS,
  T_REGS,	T_REGS,		T_REGS,		T_REGS,
  FP_REGS,	FP_REGS,	FP_REGS,	FP_REGS,
  FP_REGS,	FP_REGS,	FP_REGS,	FP_REGS,
  FP_REGS,	FP_REGS,	FP_REGS,	FP_REGS,
  FP_REGS,	FP_REGS,	FP_REGS,	FP_REGS,
  FP_REGS,	FP_REGS,	FP_REGS,	FP_REGS,
  FP_REGS,	FP_REGS,	FP_REGS,	FP_REGS,
  FP_REGS,	FP_REGS,	FP_REGS,	FP_REGS,
  FP_REGS,	FP_REGS,	FP_REGS,	FP_REGS,
  FRAME_REGS,	FRAME_REGS,	LC_REGS,	LC_REGS,
  LE_REGS,	LE_REGS,	LS_REGS,	LS_REGS,
  VIT_REGS
};

/* Costs to use when optimizing for size.  */
static const struct riscv_tune_info rocket_tune_info = {
  {COSTS_N_INSNS (4), COSTS_N_INSNS (5)},	/* fp_add */
  {COSTS_N_INSNS (4), COSTS_N_INSNS (5)},	/* fp_mul */
  {COSTS_N_INSNS (20), COSTS_N_INSNS (20)},	/* fp_div */
  {COSTS_N_INSNS (4), COSTS_N_INSNS (4)},	/* int_mul */
  {COSTS_N_INSNS (6), COSTS_N_INSNS (6)},	/* int_div */
  1,						/* issue_rate */
  3,						/* branch_cost */
  COSTS_N_INSNS (2),				/* fp_to_int_cost */
  5						/* memory_cost */
};

/* Costs to use when optimizing for size.  */
static const struct riscv_tune_info optimize_size_tune_info = {
  {COSTS_N_INSNS (1), COSTS_N_INSNS (1)},	/* fp_add */
  {COSTS_N_INSNS (1), COSTS_N_INSNS (1)},	/* fp_mul */
  {COSTS_N_INSNS (1), COSTS_N_INSNS (1)},	/* fp_div */
  {COSTS_N_INSNS (1), COSTS_N_INSNS (1)},	/* int_mul */
  {COSTS_N_INSNS (1), COSTS_N_INSNS (1)},	/* int_div */
  1,						/* issue_rate */
  1,						/* branch_cost */
  COSTS_N_INSNS (1),				/* fp_to_int_cost */
  1						/* memory_cost */
};

/* A table describing all the processors GCC knows about.  */
static const struct riscv_cpu_info riscv_cpu_info_table[] = {
  /* Entries for generic ISAs.  */
  { "rocket", "IMAFD", &rocket_tune_info },
};

/* Return the riscv_cpu_info entry for the given name string.  */

static const struct riscv_cpu_info *
riscv_parse_cpu (const char *cpu_string)
{
  unsigned int i;

  for (i = 0; i < ARRAY_SIZE (riscv_cpu_info_table); i++)
    if (strcmp (riscv_cpu_info_table[i].name, cpu_string) == 0)
      return riscv_cpu_info_table + i;

  error ("unknown cpu `%s'", cpu_string);
  return riscv_cpu_info_table;
}

/* Fill CODES with a sequence of rtl operations to load VALUE.
   Return the number of operations needed.  */

static int
riscv_build_integer_1 (struct riscv_integer_op *codes, HOST_WIDE_INT value,
		       enum machine_mode mode)
{
  HOST_WIDE_INT low_part = CONST_LOW_PART (value);
  int cost = INT_MAX, alt_cost;
  struct riscv_integer_op alt_codes[RISCV_MAX_INTEGER_OPS];

  if (SMALL_OPERAND (value) || LUI_OPERAND (value))
    {
      /* Simply ADDI or LUI */
      codes[0].code = UNKNOWN;
      codes[0].value = value;
      return 1;
    }

  /* End with ADDI */
  if (low_part != 0
      && !(mode == HImode && (int16_t)(value - low_part) != (value - low_part)))
    {
      cost = 1 + riscv_build_integer_1 (codes, value - low_part, mode);
      codes[cost-1].code = PLUS;
      codes[cost-1].value = low_part;
    }

  /* End with XORI */
  if (cost > 2 && (low_part < 0 || mode == HImode))
    {
      alt_cost = 1 + riscv_build_integer_1 (alt_codes, value ^ low_part, mode);
      alt_codes[alt_cost-1].code = XOR;
      alt_codes[alt_cost-1].value = low_part;
      if (alt_cost < cost)
	cost = alt_cost, memcpy (codes, alt_codes, sizeof(alt_codes));
    }

  /* Eliminate trailing zeros and end with SLLI */
  if (cost > 2 && (value & 1) == 0)
    {
      int shift = 0;
      while ((value & 1) == 0)
	shift++, value >>= 1;
      alt_cost = 1 + riscv_build_integer_1 (alt_codes, value, mode);
      alt_codes[alt_cost-1].code = ASHIFT;
      alt_codes[alt_cost-1].value = shift;
      if (alt_cost < cost)
	cost = alt_cost, memcpy (codes, alt_codes, sizeof(alt_codes));
    }

  gcc_assert (cost <= RISCV_MAX_INTEGER_OPS);
  return cost;
}

static int
riscv_build_integer (struct riscv_integer_op *codes, HOST_WIDE_INT value,
		     enum machine_mode mode)
{
  int cost = riscv_build_integer_1 (codes, value, mode);

  /* Eliminate leading zeros and end with SRLI */
  if (value > 0 && cost > 2)
    {
      struct riscv_integer_op alt_codes[RISCV_MAX_INTEGER_OPS];
      int alt_cost, shift = 0;
      HOST_WIDE_INT shifted_val;

      /* Try filling trailing bits with 1s */
      while ((value << shift) >= 0)
	shift++;
      shifted_val = (value << shift) | ((((HOST_WIDE_INT) 1) << shift) - 1);
      alt_cost = 1 + riscv_build_integer_1 (alt_codes, shifted_val, mode);
      alt_codes[alt_cost-1].code = LSHIFTRT;
      alt_codes[alt_cost-1].value = shift;
      if (alt_cost < cost)
	cost = alt_cost, memcpy (codes, alt_codes, sizeof (alt_codes));

      /* Try filling trailing bits with 0s */
      shifted_val = value << shift;
      alt_cost = 1 + riscv_build_integer_1 (alt_codes, shifted_val, mode);
      alt_codes[alt_cost-1].code = LSHIFTRT;
      alt_codes[alt_cost-1].value = shift;
      if (alt_cost < cost)
	cost = alt_cost, memcpy (codes, alt_codes, sizeof (alt_codes));
    }

  return cost;
}

static int
riscv_split_integer_cost (HOST_WIDE_INT val)
{
  int cost;
  int32_t loval = val, hival = (val - (int32_t)val) >> 32;
  struct riscv_integer_op codes[RISCV_MAX_INTEGER_OPS];

  cost = 2 + riscv_build_integer (codes, loval, VOIDmode);
  if (loval != hival)
    cost += riscv_build_integer (codes, hival, VOIDmode);

  return cost;
}

static int
riscv_integer_cost (HOST_WIDE_INT val)
{
  struct riscv_integer_op codes[RISCV_MAX_INTEGER_OPS];
  return MIN (riscv_build_integer (codes, val, VOIDmode),
	      riscv_split_integer_cost (val));
}

/* Try to split a 64b integer into 32b parts, then reassemble. */

static rtx
riscv_split_integer (HOST_WIDE_INT val, enum machine_mode mode)
{
  int32_t loval = val, hival = (val - (int32_t)val) >> 32;
  rtx hi = gen_reg_rtx (mode), lo = gen_reg_rtx (mode);

  riscv_move_integer (hi, hi, hival);
  riscv_move_integer (lo, lo, loval);

  hi = gen_rtx_fmt_ee (ASHIFT, mode, hi, GEN_INT (32));
  hi = force_reg (mode, hi);

  return gen_rtx_fmt_ee (PLUS, mode, hi, lo);
}

/* Return true if X is a thread-local symbol.  */

static bool
riscv_tls_symbol_p (const_rtx x)
{
  return GET_CODE (x) == SYMBOL_REF && SYMBOL_REF_TLS_MODEL (x) != 0;
}

static bool
riscv_symbol_binds_local_p (const_rtx x)
{
  return (SYMBOL_REF_DECL (x)
	  ? targetm.binds_local_p (SYMBOL_REF_DECL (x))
	  : SYMBOL_REF_LOCAL_P (x));
}

/* Return the method that should be used to access SYMBOL_REF or
   LABEL_REF X in context CONTEXT.  */

static enum riscv_symbol_type
riscv_classify_symbol (const_rtx x)
{
  if (riscv_tls_symbol_p (x))
    return SYMBOL_TLS;

  if (GET_CODE (x) == LABEL_REF)
    {
      if (riscv_is_tiny_symbol_p((rtx) x)) return SYMBOL_TINY_ABSOLUTE;
      if (LABEL_REF_NONLOCAL_P (x))
	return SYMBOL_GOT_DISP;
      return SYMBOL_ABSOLUTE;
    }

  gcc_assert (GET_CODE (x) == SYMBOL_REF);

  if (flag_pic && !riscv_symbol_binds_local_p (x))
    return SYMBOL_GOT_DISP;

  if (riscv_is_tiny_symbol_p((rtx) x)) return SYMBOL_TINY_ABSOLUTE; else return SYMBOL_ABSOLUTE;
}

/* Classify the base of symbolic expression X, given that X appears in
   context CONTEXT.  */

static enum riscv_symbol_type
riscv_classify_symbolic_expression (rtx x)
{
  rtx offset;

  split_const (x, &x, &offset);
  if (UNSPEC_ADDRESS_P (x))
    return UNSPEC_ADDRESS_TYPE (x);

  return riscv_classify_symbol (x);
}

/* Return true if X is a symbolic constant that can be used in context
   CONTEXT.  If it is, store the type of the symbol in *SYMBOL_TYPE.  */

bool
riscv_symbolic_constant_p (rtx x, enum riscv_symbol_type *symbol_type)
{
  rtx offset;

  split_const (x, &x, &offset);
  if (UNSPEC_ADDRESS_P (x))
    {
      *symbol_type = UNSPEC_ADDRESS_TYPE (x);
      x = UNSPEC_ADDRESS (x);
    }
  else if (GET_CODE (x) == SYMBOL_REF || GET_CODE (x) == LABEL_REF)
    *symbol_type = riscv_classify_symbol (x);
  else
    return false;

  if (offset == const0_rtx)
    return true;

  /* Check whether a nonzero offset is valid for the underlying
     relocations.  */
  switch (*symbol_type)
    {
    case SYMBOL_ABSOLUTE:
    case SYMBOL_TLS_LE:
      return (int32_t) INTVAL (offset) == INTVAL (offset);

    case SYMBOL_TINY_ABSOLUTE:
	return true;

    default:
      return false;
    }
  gcc_unreachable ();
}

/* Returns the number of instructions necessary to reference a symbol. */

static int riscv_symbol_insns (enum riscv_symbol_type type)
{
  switch (type)
  {
    case SYMBOL_TLS: return 0; /* Depends on the TLS model. */
    case SYMBOL_TINY_ABSOLUTE: return 1; /* the reference itself */
    case SYMBOL_ABSOLUTE: return 2; /* LUI + the reference itself */
    case SYMBOL_TLS_LE: return 3; /* LUI + ADD TP + the reference itself */
    case SYMBOL_GOT_DISP: return 3; /* AUIPC + LD GOT + the reference itself */
    default: gcc_unreachable();
  }
}

/* Implement TARGET_LEGITIMATE_CONSTANT_P.  */

static bool
riscv_legitimate_constant_p (enum machine_mode mode ATTRIBUTE_UNUSED, rtx x)
{
  return riscv_const_insns (x) > 0;
}

/* Implement TARGET_CANNOT_FORCE_CONST_MEM.  */

static bool
riscv_cannot_force_const_mem (enum machine_mode mode ATTRIBUTE_UNUSED, rtx x)
{
  enum riscv_symbol_type type;
  rtx base, offset;

  /* There is no assembler syntax for expressing an address-sized
     high part.  */
  if (GET_CODE (x) == HIGH)
    return true;

  split_const (x, &base, &offset);
  if (riscv_symbolic_constant_p (base, &type))
    {
      /* As an optimization, don't spill symbolic constants that are as
	 cheap to rematerialize as to access in the constant pool.  */
      if (SMALL_INT (offset) && riscv_symbol_insns (type) > 0)
	return true;

      /* As an optimization, avoid needlessly generate dynamic relocations.  */
      if (flag_pic)
	return true;
    }

  /* TLS symbols must be computed by riscv_legitimize_move.  */
  if (tls_referenced_p (x))
    return true;

  return false;
}

/* Return true if register REGNO is a valid base register for mode MODE.
   STRICT_P is true if REG_OK_STRICT is in effect.  */

int
riscv_regno_mode_ok_for_base_p (int regno, enum machine_mode mode ATTRIBUTE_UNUSED,
			       bool strict_p)
{
  if (!HARD_REGISTER_NUM_P (regno))
    {
      if (!strict_p)
	return true;
      regno = reg_renumber[regno];
    }

  /* These fake registers will be eliminated to either the stack or
     hard frame pointer, both of which are usually valid base registers.
     Reload deals with the cases where the eliminated form isn't valid.  */
  if (regno == ARG_POINTER_REGNUM || regno == FRAME_POINTER_REGNUM)
    return true;

  return GP_REG_P (regno);
}

/* Return true if X is a valid base register for mode MODE.
   STRICT_P is true if REG_OK_STRICT is in effect.  */

static bool
riscv_valid_base_register_p (rtx x, enum machine_mode mode, bool strict_p)
{
  if (!strict_p && GET_CODE (x) == SUBREG)
    x = SUBREG_REG (x);

  return (REG_P (x)
	  && riscv_regno_mode_ok_for_base_p (REGNO (x), mode, strict_p));
}

/* Return true if, for every base register BASE_REG, (plus BASE_REG X)
   can address a value of mode MODE.  */

static bool
riscv_valid_offset_p (rtx x, enum machine_mode mode)
{
  /* Check that X is a signed 12-bit number.  */
  if (!const_arith_operand (x, Pmode)) {
    if (riscv_is_tiny_symbol_p(x)) return true;
    return false;
  }

  /* We may need to split multiword moves, so make sure that every word
     is accessible.  */
  if (GET_MODE_SIZE (mode) > UNITS_PER_WORD
      && !SMALL_OPERAND (INTVAL (x) + GET_MODE_SIZE (mode) - UNITS_PER_WORD))
    return false;

  return true;
}

/* Return true if a LO_SUM can address a value of mode MODE when the
   LO_SUM symbol has type SYMBOL_TYPE.  */

static bool
riscv_valid_lo_sum_p (enum riscv_symbol_type symbol_type, enum machine_mode mode)
{
  /* Check that symbols of type SYMBOL_TYPE can be used to access values
     of mode MODE.  */
  if (riscv_symbol_insns (symbol_type) == 0)
    return false;

  /* Check that there is a known low-part relocation.  */
  if (riscv_lo_relocs[symbol_type] == NULL)
    return false;

  /* We may need to split multiword moves, so make sure that each word
     can be accessed without inducing a carry.  This is mainly needed
     for o64, which has historically only guaranteed 64-bit alignment
     for 128-bit types.  */
  if (GET_MODE_SIZE (mode) > UNITS_PER_WORD
      && GET_MODE_BITSIZE (mode) > GET_MODE_ALIGNMENT (mode))
    return false;

  return true;
}

/* Return true if X is a valid address for machine mode MODE.  If it is,
   fill in INFO appropriately.  STRICT_P is true if REG_OK_STRICT is in
   effect.  */

static bool
riscv_classify_address (struct riscv_address_info *info, rtx x,
		       enum machine_mode mode, bool strict_p)
{
  switch (GET_CODE (x))
    {
    case REG:
    case SUBREG:
      info->type = ADDRESS_REG;
      info->reg = x;
      info->offset = const0_rtx;
      info->mode = mode;
      return riscv_valid_base_register_p (info->reg, mode, strict_p);

    case PLUS:
      info->type = ADDRESS_REG;
      info->reg = XEXP (x, 0);
      info->offset = XEXP (x, 1);
      info->mode = mode;
      if (((Pulp_Cpu>=PULP_V0) && !TARGET_MASK_NOINDREGREG && (GET_MODE_SIZE (mode) <= UNITS_PER_WORD) ) &&
          !(TARGET_HARD_FLOAT && (mode == SFmode)) &&
          ((GET_CODE(info->offset) == REG) || (GET_CODE(info->offset) == SUBREG))) {
      		info->type = ADDRESS_REG_REG;
         	return (riscv_valid_base_register_p (info->reg, mode, strict_p)
         	        && riscv_valid_base_register_p (info->offset, mode, strict_p));
      } else {
		if ((GET_CODE(info->offset) == SYMBOL_REF || GET_CODE(info->offset) == LABEL_REF) &&
		    riscv_is_tiny_symbol_p(info->offset)) info->type = ADDRESS_REG_TINY_SYMBOL;

		return (riscv_valid_base_register_p (info->reg, mode, strict_p)
	                && riscv_valid_offset_p (info->offset, mode));
      }

    case LO_SUM:
      info->type = ADDRESS_LO_SUM;
      info->reg = XEXP (x, 0);
      info->offset = XEXP (x, 1);
      info->mode = mode;
      /* We have to trust the creator of the LO_SUM to do something vaguely
	 sane.  Target-independent code that creates a LO_SUM should also
	 create and verify the matching HIGH.  Target-independent code that
	 adds an offset to a LO_SUM must prove that the offset will not
	 induce a carry.  Failure to do either of these things would be
	 a bug, and we are not required to check for it here.  The RISCV
	 backend itself should only create LO_SUMs for valid symbolic
	 constants, with the high part being either a HIGH or a copy
	 of _gp. */
      info->symbol_type
	= riscv_classify_symbolic_expression (info->offset);
      return (riscv_valid_base_register_p (info->reg, mode, strict_p)
	      && riscv_valid_lo_sum_p (info->symbol_type, mode));

    case CONST_INT:
      /* Small-integer addresses don't occur very often, but they
	 are legitimate if $0 is a valid base register.  */
      info->type = ADDRESS_CONST_INT;
      info->mode = mode;
      return SMALL_INT (x);
    case POST_INC:
      if (GET_MODE_SIZE (mode) > UNITS_PER_WORD || (TARGET_HARD_FLOAT && (mode == SFmode))) return false;
      info->type = ADDRESS_REG_POST_INC;
      info->reg = XEXP (x, 0);
      info->offset = XEXP (x, 1);
      info->mode = mode;
      return (riscv_valid_base_register_p (info->reg, mode, strict_p));
    case POST_DEC:
      if (GET_MODE_SIZE (mode) > UNITS_PER_WORD || (TARGET_HARD_FLOAT && (mode == SFmode))) return false;
      info->type = ADDRESS_REG_POST_DEC;
      info->reg = XEXP (x, 0);
      info->offset = XEXP (x, 1);
      info->mode = mode;
      return (riscv_valid_base_register_p (info->reg, mode, strict_p));
    case POST_MODIFY:
      if (GET_MODE_SIZE (mode) > UNITS_PER_WORD || (TARGET_HARD_FLOAT && (mode == SFmode))) return false;
      info->type = ADDRESS_REG_POST_MODIFY;
      info->reg = XEXP (x, 0);
      info->mode = mode;
      if (GET_CODE(XEXP(x, 1)) == PLUS) {
	 if (XEXP (XEXP(x, 1), 0) != info->reg) {
      	 	/* In some cases the combiner can send a post modify built in a non standard way with the offset
	    	   in the left branch while the canonical representation always put it in the right branch */
		if (info->offset == info->reg) {
			/* Swap plus branches */
			rtx Tmp = XEXP (XEXP(x, 1), 1);
			XEXP (XEXP(x, 1), 1) = XEXP (XEXP(x, 1), 0);
			XEXP (XEXP(x, 1), 0) = Tmp;
			
		} else return false;
	 }
         info->offset = XEXP (XEXP(x, 1), 1);
	 if (GET_CODE(info->offset) == CONST_INT) {
                if (!const_arith_operand (info->offset, Pmode)) return FALSE;
         	return (riscv_valid_base_register_p (info->reg, mode, strict_p));
	 } else if ((GET_CODE(info->offset) == REG) || (GET_CODE(info->offset) == SUBREG)) {
         	return (riscv_valid_base_register_p (info->reg, mode, strict_p)
         	       && riscv_valid_base_register_p (info->offset, mode, strict_p));
         }
      }
      return false;

    case LABEL_REF:
    case SYMBOL_REF:
        if (riscv_is_tiny_symbol_p(x)) {
                info->type = ADDRESS_TINY_SYMBOL;
                return true;
        }
        return false;
   case CONST:
        if (GET_CODE (XEXP (x, 0)) == PLUS && GET_CODE (XEXP (XEXP (x, 0), 0)) == SYMBOL_REF && CONST_INT_P (XEXP (XEXP (x, 0), 1)) &&
            riscv_is_tiny_symbol_p(XEXP (XEXP (x, 0), 0))) {
                info->type = ADDRESS_TINY_SYMBOL;
                return true;
        }
        return false;


    default:
      return false;
    }
}

/* Implement TARGET_LEGITIMATE_ADDRESS_P.  */

static bool
riscv_legitimate_address_p (enum machine_mode mode, rtx x, bool strict_p)
{
  struct riscv_address_info addr;

  return riscv_classify_address (&addr, x, mode, strict_p);
}

/* Return the number of instructions needed to load or store a value
   of mode MODE at address X.  Return 0 if X isn't valid for MODE.
   Assume that multiword moves may need to be split into word moves
   if MIGHT_SPLIT_P, otherwise assume that a single load or store is
   enough. */

int
riscv_address_insns (rtx x, enum machine_mode mode, bool might_split_p)
{
  struct riscv_address_info addr;
  int n = 1;

  if (!riscv_classify_address (&addr, x, mode, false))
    return 0;

  /* BLKmode is used for single unaligned loads and stores and should
     not count as a multiword mode. */
  if (mode != BLKmode && might_split_p)
    n += (GET_MODE_SIZE (mode) + UNITS_PER_WORD - 1) / UNITS_PER_WORD;

  if (addr.type == ADDRESS_LO_SUM)
    n += riscv_symbol_insns (addr.symbol_type) - 1;

  return n;
}

/* Return the number of instructions needed to load constant X.
   Return 0 if X isn't a valid constant.  */

int
riscv_const_insns (rtx x)
{
  enum riscv_symbol_type symbol_type;
  rtx offset;

  switch (GET_CODE (x))
    {
    case HIGH:
      if (!riscv_symbolic_constant_p (XEXP (x, 0), &symbol_type)
	  || !riscv_hi_relocs[symbol_type])
	return 0;

      /* This is simply an LUI. */
      return 1;

    case CONST_INT:
      {
	int cost = riscv_integer_cost (INTVAL (x));
	/* Force complicated constants to memory. */
	return cost < 4 ? cost : 0;
      }

    case CONST_VECTOR:
	if ((Pulp_Cpu>=PULP_V2) && !TARGET_MASK_NOVECT && riscv_replicated_const_vector (x, -32, 31)) return 1;
    case CONST_DOUBLE:
      /* Allow zeros for normal mode, where we can use x0.  */
      return x == CONST0_RTX (GET_MODE (x)) ? 1 : 0;

    case CONST:
      /* See if we can refer to X directly.  */
      if (riscv_symbolic_constant_p (x, &symbol_type))
	return riscv_symbol_insns (symbol_type);

      /* Otherwise try splitting the constant into a base and offset.
	 If the offset is a 16-bit value, we can load the base address
	 into a register and then use (D)ADDIU to add in the offset.
	 If the offset is larger, we can load the base and offset
	 into separate registers and add them together with (D)ADDU.
	 However, the latter is only possible before reload; during
	 and after reload, we must have the option of forcing the
	 constant into the pool instead.  */
      split_const (x, &x, &offset);
      if (offset != 0)
	{
	  int n = riscv_const_insns (x);
	  if (n != 0)
	    {
	      if (SMALL_INT (offset))
		return n + 1;
	      else if (!targetm.cannot_force_const_mem (GET_MODE (x), x))
		return n + 1 + riscv_integer_cost (INTVAL (offset));
	    }
	}
      return 0;

    case SYMBOL_REF:
    case LABEL_REF:
      return riscv_symbol_insns (riscv_classify_symbol (x));

    default:
      return 0;
    }
}

/* X is a doubleword constant that can be handled by splitting it into
   two words and loading each word separately.  Return the number of
   instructions required to do this.  */

int
riscv_split_const_insns (rtx x)
{
  unsigned int low, high;

  low = riscv_const_insns (riscv_subword (x, false));
  high = riscv_const_insns (riscv_subword (x, true));
  gcc_assert (low > 0 && high > 0);
  return low + high;
}

/* Return the number of instructions needed to implement INSN,
   given that it loads from or stores to MEM. */

int
riscv_load_store_insns (rtx mem, rtx_insn *insn)
{
  enum machine_mode mode;
  bool might_split_p;
  rtx set;

  gcc_assert (MEM_P (mem));
  mode = GET_MODE (mem);

  /* Try to prove that INSN does not need to be split.  */
  might_split_p = true;
  if (GET_MODE_BITSIZE (mode) == 64)
    {
      set = single_set (insn);
      if (set && !riscv_split_64bit_move_p (SET_DEST (set), SET_SRC (set)))
	might_split_p = false;
    } else might_split_p = false;

  return riscv_address_insns (XEXP (mem, 0), mode, might_split_p);
}

/* Emit a move from SRC to DEST.  Assume that the move expanders can
   handle all moves if !can_create_pseudo_p ().  The distinction is
   important because, unlike emit_move_insn, the move expanders know
   how to force Pmode objects into the constant pool even when the
   constant pool address is not itself legitimate.  */

rtx
riscv_emit_move (rtx dest, rtx src)
{
  return (can_create_pseudo_p ()
	  ? emit_move_insn (dest, src)
	  : emit_move_insn_1 (dest, src));
}

/* Emit an instruction of the form (set TARGET (CODE OP0 OP1)).  */

static void
riscv_emit_binary (enum rtx_code code, rtx target, rtx op0, rtx op1)
{
  emit_insn (gen_rtx_SET (VOIDmode, target,
			  gen_rtx_fmt_ee (code, GET_MODE (target), op0, op1)));
}

/* Compute (CODE OP0 OP1) and store the result in a new register
   of mode MODE.  Return that new register.  */

static rtx
riscv_force_binary (enum machine_mode mode, enum rtx_code code, rtx op0, rtx op1)
{
  rtx reg;

  reg = gen_reg_rtx (mode);
  riscv_emit_binary (code, reg, op0, op1);
  return reg;
}

/* Copy VALUE to a register and return that register.  If new pseudos
   are allowed, copy it into a new register, otherwise use DEST.  */

static rtx
riscv_force_temporary (rtx dest, rtx value)
{
  if (can_create_pseudo_p ())
    return force_reg (Pmode, value);
  else
    {
      riscv_emit_move (dest, value);
      return dest;
    }
}

/* Wrap symbol or label BASE in an UNSPEC address of type SYMBOL_TYPE,
   then add CONST_INT OFFSET to the result.  */

static rtx
riscv_unspec_address_offset (rtx base, rtx offset,
			    enum riscv_symbol_type symbol_type)
{
  base = gen_rtx_UNSPEC (Pmode, gen_rtvec (1, base),
			 UNSPEC_ADDRESS_FIRST + symbol_type);
  if (offset != const0_rtx)
    base = gen_rtx_PLUS (Pmode, base, offset);
  return gen_rtx_CONST (Pmode, base);
}

/* Return an UNSPEC address with underlying address ADDRESS and symbol
   type SYMBOL_TYPE.  */

rtx
riscv_unspec_address (rtx address, enum riscv_symbol_type symbol_type)
{
  rtx base, offset;

  split_const (address, &base, &offset);
  return riscv_unspec_address_offset (base, offset, symbol_type);
}

/* If OP is an UNSPEC address, return the address to which it refers,
   otherwise return OP itself.  */

static rtx
riscv_strip_unspec_address (rtx op)
{
  rtx base, offset;

  split_const (op, &base, &offset);
  if (UNSPEC_ADDRESS_P (base))
    op = plus_constant (Pmode, UNSPEC_ADDRESS (base), INTVAL (offset));
  return op;
}

/* If riscv_unspec_address (ADDR, SYMBOL_TYPE) is a 32-bit value, add the
   high part to BASE and return the result.  Just return BASE otherwise.
   TEMP is as for riscv_force_temporary.

   The returned expression can be used as the first operand to a LO_SUM.  */

static rtx
riscv_unspec_offset_high (rtx temp, rtx addr, enum riscv_symbol_type symbol_type)
{
  addr = gen_rtx_HIGH (Pmode, riscv_unspec_address (addr, symbol_type));
  return riscv_force_temporary (temp, addr);
}

/* Load an entry from the GOT. */
static rtx riscv_got_load_tls_gd(rtx dest, rtx sym)
{
  return (Pmode == DImode ? gen_got_load_tls_gddi(dest, sym) : gen_got_load_tls_gdsi(dest, sym));
}

static rtx riscv_got_load_tls_ie(rtx dest, rtx sym)
{
  return (Pmode == DImode ? gen_got_load_tls_iedi(dest, sym) : gen_got_load_tls_iesi(dest, sym));
}

static rtx riscv_tls_add_tp_le(rtx dest, rtx base, rtx sym)
{
  rtx tp = gen_rtx_REG (Pmode, THREAD_POINTER_REGNUM);
  return (Pmode == DImode ? gen_tls_add_tp_ledi(dest, base, tp, sym) : gen_tls_add_tp_lesi(dest, base, tp, sym));
}

/* If MODE is MAX_MACHINE_MODE, ADDR appears as a move operand, otherwise
   it appears in a MEM of that mode.  Return true if ADDR is a legitimate
   constant in that context and can be split into high and low parts.
   If so, and if LOW_OUT is nonnull, emit the high part and store the
   low part in *LOW_OUT.  Leave *LOW_OUT unchanged otherwise.

   TEMP is as for riscv_force_temporary and is used to load the high
   part into a register.

   When MODE is MAX_MACHINE_MODE, the low part is guaranteed to be
   a legitimize SET_SRC for an .md pattern, otherwise the low part
   is guaranteed to be a legitimate address for mode MODE.  */

bool
riscv_split_symbol (rtx temp, rtx addr, enum machine_mode mode, rtx *low_out)
{
  enum riscv_symbol_type symbol_type;
  rtx high;

  if ((GET_CODE (addr) == HIGH && mode == MAX_MACHINE_MODE)
      || !riscv_symbolic_constant_p (addr, &symbol_type)
      || riscv_symbol_insns (symbol_type) == 0
      || !riscv_hi_relocs[symbol_type])
    return false;

  if (low_out)
    {
      switch (symbol_type)
	{
	case SYMBOL_TINY_ABSOLUTE:
      	  *low_out = gen_rtx_LO_SUM (Pmode, const0_rtx, addr);
	  break;
	case SYMBOL_ABSOLUTE:
	  high = gen_rtx_HIGH (Pmode, copy_rtx (addr));
      	  high = riscv_force_temporary (temp, high);
      	  *low_out = gen_rtx_LO_SUM (Pmode, high, addr);
	  break;
	
	default:
	  gcc_unreachable ();
	}
    }

  return true;
}

/* Return a legitimate address for REG + OFFSET.  TEMP is as for
   riscv_force_temporary; it is only needed when OFFSET is not a
   SMALL_OPERAND.  */

static rtx
riscv_add_offset (rtx temp, rtx reg, HOST_WIDE_INT offset)
{
  if (!SMALL_OPERAND (offset))
    {
      rtx high;

      /* Leave OFFSET as a 16-bit offset and put the excess in HIGH.
         The addition inside the macro CONST_HIGH_PART may cause an
         overflow, so we need to force a sign-extension check.  */
      high = gen_int_mode (CONST_HIGH_PART (offset), Pmode);
      offset = CONST_LOW_PART (offset);
      high = riscv_force_temporary (temp, high);
      reg = riscv_force_temporary (temp, gen_rtx_PLUS (Pmode, high, reg));
    }
  return plus_constant (Pmode, reg, offset);
}

/* The __tls_get_attr symbol.  */
static GTY(()) rtx riscv_tls_symbol;

/* Return an instruction sequence that calls __tls_get_addr.  SYM is
   the TLS symbol we are referencing and TYPE is the symbol type to use
   (either global dynamic or local dynamic).  RESULT is an RTX for the
   return value location.  */

static rtx
riscv_call_tls_get_addr (rtx sym, rtx result)
{
  rtx insn, a0 = gen_rtx_REG (Pmode, GP_ARG_FIRST);

  if (!riscv_tls_symbol)
    riscv_tls_symbol = init_one_libfunc ("__tls_get_addr");

  start_sequence ();
  
  emit_insn (riscv_got_load_tls_gd (a0, sym));
  insn = riscv_expand_call (false, result, riscv_tls_symbol, const0_rtx);
  RTL_CONST_CALL_P (insn) = 1;
  use_reg (&CALL_INSN_FUNCTION_USAGE (insn), a0);
  insn = get_insns ();

  end_sequence ();

  return insn;
}

/* Generate the code to access LOC, a thread-local SYMBOL_REF, and return
   its address.  The return value will be both a valid address and a valid
   SET_SRC (either a REG or a LO_SUM).  */

static rtx
riscv_legitimize_tls_address (rtx loc)
{
  rtx dest, insn, tp, tmp1;
  enum tls_model model = SYMBOL_REF_TLS_MODEL (loc);

  /* Since we support TLS copy relocs, non-PIC TLS accesses may all use LE.  */
  if (!flag_pic)
    model = TLS_MODEL_LOCAL_EXEC;

  switch (model)
    {
    case TLS_MODEL_LOCAL_DYNAMIC:
      /* Rely on section anchors for the optimization that LDM TLS
	 provides.  The anchor's address is loaded with GD TLS. */
    case TLS_MODEL_GLOBAL_DYNAMIC:
      tmp1 = gen_rtx_REG (Pmode, GP_RETURN);
      insn = riscv_call_tls_get_addr (loc, tmp1);
      dest = gen_reg_rtx (Pmode);
      emit_libcall_block (insn, dest, tmp1, loc);
      break;

    case TLS_MODEL_INITIAL_EXEC:
      /* la.tls.ie; tp-relative add */
      tp = gen_rtx_REG (Pmode, THREAD_POINTER_REGNUM);
      tmp1 = gen_reg_rtx (Pmode);
      emit_insn (riscv_got_load_tls_ie (tmp1, loc));
      dest = gen_reg_rtx (Pmode);
      emit_insn (gen_add3_insn (dest, tmp1, tp));
      break;

    case TLS_MODEL_LOCAL_EXEC:
      tmp1 = riscv_unspec_offset_high (NULL, loc, SYMBOL_TLS_LE);
      dest = gen_reg_rtx (Pmode);
      emit_insn (riscv_tls_add_tp_le (dest, tmp1, loc));
      dest = gen_rtx_LO_SUM (Pmode, dest,
			     riscv_unspec_address (loc, SYMBOL_TLS_LE));
      break;

    default:
      gcc_unreachable ();
    }
  return dest;
}

/* If X is not a valid address for mode MODE, force it into a register.  */

static rtx
riscv_force_address (rtx x, enum machine_mode mode)
{
  if (!riscv_legitimate_address_p (mode, x, false))
    x = force_reg (Pmode, x);
  return x;
}

/* This function is used to implement LEGITIMIZE_ADDRESS.  If X can
   be legitimized in a way that the generic machinery might not expect,
   return a new address, otherwise return NULL.  MODE is the mode of
   the memory being accessed.  */

static rtx
riscv_legitimize_address (rtx x, rtx oldx ATTRIBUTE_UNUSED,
			 enum machine_mode mode)
{
  rtx addr;

  if (riscv_tls_symbol_p (x))
    return riscv_legitimize_tls_address (x);

  /* See if the address can split into a high part and a LO_SUM.  */
  if (riscv_split_symbol (NULL, x, mode, &addr))
    return riscv_force_address (addr, mode);

  /* Handle BASE + OFFSET using riscv_add_offset.  */
  if (GET_CODE (x) == PLUS && CONST_INT_P (XEXP (x, 1))
      && INTVAL (XEXP (x, 1)) != 0)
    {
      rtx base = XEXP (x, 0);
      HOST_WIDE_INT offset = INTVAL (XEXP (x, 1));

      if (!riscv_valid_base_register_p (base, mode, false))
	base = copy_to_mode_reg (Pmode, base);
      addr = riscv_add_offset (NULL, base, offset);
      return riscv_force_address (addr, mode);
    }

  return x;
}

/* Load VALUE into DEST.  TEMP is as for riscv_force_temporary.  */

void
riscv_move_integer (rtx temp, rtx dest, HOST_WIDE_INT value)
{
  struct riscv_integer_op codes[RISCV_MAX_INTEGER_OPS];
  enum machine_mode mode;
  int i, num_ops;
  rtx x;

  mode = GET_MODE (dest);
  num_ops = riscv_build_integer (codes, value, mode);

  if (can_create_pseudo_p () && num_ops > 2 /* not a simple constant */
      && num_ops >= riscv_split_integer_cost (value))
    x = riscv_split_integer (value, mode);
  else
    {
      /* Apply each binary operation to X. */
      x = GEN_INT (codes[0].value);

      for (i = 1; i < num_ops; i++)
        {
          if (!can_create_pseudo_p ())
            {
              emit_insn (gen_rtx_SET (VOIDmode, temp, x));
              x = temp;
            }
          else
            x = force_reg (mode, x);

          x = gen_rtx_fmt_ee (codes[i].code, mode, x, GEN_INT (codes[i].value));
        }
    }

  emit_insn (gen_rtx_SET (VOIDmode, dest, x));
}

/* Subroutine of riscv_legitimize_move.  Move constant SRC into register
   DEST given that SRC satisfies immediate_operand but doesn't satisfy
   move_operand.  */

static void
riscv_legitimize_const_move (enum machine_mode mode, rtx dest, rtx src)
{
  rtx base, offset;

  /* Split moves of big integers into smaller pieces.  */
  if (splittable_const_int_operand (src, mode))
    {
      riscv_move_integer (dest, dest, INTVAL (src));
      return;
    }

  /* Split moves of symbolic constants into high/low pairs.  */
  if (riscv_split_symbol (dest, src, MAX_MACHINE_MODE, &src))
    {
      emit_insn (gen_rtx_SET (VOIDmode, dest, src));
      return;
    }

  /* Generate the appropriate access sequences for TLS symbols.  */
  if (riscv_tls_symbol_p (src))
    {
      riscv_emit_move (dest, riscv_legitimize_tls_address (src));
      return;
    }

  /* If we have (const (plus symbol offset)), and that expression cannot
     be forced into memory, load the symbol first and add in the offset.  Also
     prefer to do this even if the constant _can_ be forced into memory, as it
     usually produces better code.  */
  split_const (src, &base, &offset);
  if (offset != const0_rtx
      && (targetm.cannot_force_const_mem (mode, src) || can_create_pseudo_p ()))
    {
      base = riscv_force_temporary (dest, base);
      riscv_emit_move (dest, riscv_add_offset (NULL, base, INTVAL (offset)));
      return;
    }

  src = force_const_mem (mode, src);

  /* When using explicit relocs, constant pool references are sometimes
     not legitimate addresses.  */
  riscv_split_symbol (dest, XEXP (src, 0), mode, &XEXP (src, 0));
  riscv_emit_move (dest, src);
}

/* If (set DEST SRC) is not a valid move instruction, emit an equivalent
   sequence that is valid.  */

bool
riscv_legitimize_move (enum machine_mode mode, rtx dest, rtx src)
{
  if (!register_operand (dest, mode) && !reg_or_0_operand (src, mode))
    {
      riscv_emit_move (dest, force_reg (mode, src));
      return true;
    }

  /* We need to deal with constants that would be legitimate
     immediate_operands but aren't legitimate move_operands.  */
  if (CONSTANT_P (src) && !move_operand (src, mode))
    {
      riscv_legitimize_const_move (mode, dest, src);
      set_unique_reg_note (get_last_insn (), REG_EQUAL, copy_rtx (src));
      return true;
    }
  return false;
}

/* Return true if there is an instruction that implements CODE and accepts
   X as an immediate operand. */

static int
riscv_immediate_operand_p (int code, HOST_WIDE_INT x)
{
  switch (code)
    {
    case ASHIFT:
    case ASHIFTRT:
    case LSHIFTRT:
      /* All shift counts are truncated to a valid constant.  */
      return true;

    case AND:
    case IOR:
    case XOR:
    case PLUS:
    case LT:
    case LTU:
      /* These instructions take 12-bit signed immediates.  */
      return SMALL_OPERAND (x);

    case LE:
      /* We add 1 to the immediate and use SLT.  */
      return SMALL_OPERAND (x + 1);

    case LEU:
      /* Likewise SLTU, but reject the always-true case.  */
      return SMALL_OPERAND (x + 1) && x + 1 != 0;

    case GE:
    case GEU:
      /* We can emulate an immediate of 1 by using GT/GTU against x0. */
      return x == 1;

    default:
      /* By default assume that x0 can be used for 0.  */
      return x == 0;
    }
}

/* Return the cost of binary operation X, given that the instruction
   sequence for a word-sized or smaller operation takes SIGNLE_INSNS
   instructions and that the sequence of a double-word operation takes
   DOUBLE_INSNS instructions.  */

static int
riscv_binary_cost (rtx x, int single_insns, int double_insns)
{
  if (GET_MODE_SIZE (GET_MODE (x)) == UNITS_PER_WORD * 2)
    return COSTS_N_INSNS (double_insns);
  return COSTS_N_INSNS (single_insns);
}

/* Return the cost of sign-extending OP to mode MODE, not including the
   cost of OP itself.  */

static int
riscv_sign_extend_cost (enum machine_mode mode, rtx op)
{
  if (MEM_P (op))
    /* Extended loads are as cheap as unextended ones.  */
    return 0;

  if (TARGET_64BIT && mode == DImode && GET_MODE (op) == SImode)
    /* A sign extension from SImode to DImode in 64-bit mode is free.  */
    return 0;

  /* We need to use a shift left and a shift right.  */
  return COSTS_N_INSNS (2);
}

/* Return the cost of zero-extending OP to mode MODE, not including the
   cost of OP itself.  */

static int
riscv_zero_extend_cost (enum machine_mode mode, rtx op)
{
  if (MEM_P (op))
    /* Extended loads are as cheap as unextended ones.  */
    return 0;

  if ((TARGET_64BIT && mode == DImode && GET_MODE (op) == SImode) ||
      ((mode == DImode || mode == SImode) && GET_MODE (op) == HImode))
    /* We need a shift left by 32 bits and a shift right by 32 bits.  */
    return COSTS_N_INSNS (2);

  /* We can use ANDI.  */
  return COSTS_N_INSNS (1);
}

/* Implement TARGET_RTX_COSTS.  */

static bool
riscv_rtx_costs (rtx x, int code, int outer_code, int opno ATTRIBUTE_UNUSED,
		 int *total, bool speed)
{
  enum machine_mode mode = GET_MODE (x);
  bool float_mode_p = FLOAT_MODE_P (mode);
  int cost;

  switch (code)
    {
    case CONST_INT:
      if (riscv_immediate_operand_p (outer_code, INTVAL (x)))
	{
	  *total = 0;
	  return true;
	}
      /* Fall through.  */

    case SYMBOL_REF:
    case LABEL_REF:
    case CONST_DOUBLE:
    case CONST:
      if (speed)
	*total = 1;
      else if ((cost = riscv_const_insns (x)) > 0)
	*total = COSTS_N_INSNS (cost);
      else /* The instruction will be fetched from the constant pool.  */
	*total = COSTS_N_INSNS (riscv_symbol_insns (SYMBOL_ABSOLUTE));
      return true;

    case MEM:
      /* If the address is legitimate, return the number of
	 instructions it needs.  */
      if ((cost = riscv_address_insns (XEXP (x, 0), mode, true)) > 0)
	{
	  *total = COSTS_N_INSNS (cost + tune_info->memory_cost);
	  return true;
	}
      /* Otherwise use the default handling.  */
      return false;

    case NOT:
      *total = COSTS_N_INSNS (GET_MODE_SIZE (mode) > UNITS_PER_WORD ? 2 : 1);
      return false;

    case AND:
    case IOR:
    case XOR:
      /* Double-word operations use two single-word operations.  */
      *total = riscv_binary_cost (x, 1, 2);
      return false;

    case ASHIFT:
    case ASHIFTRT:
    case LSHIFTRT:
      *total = riscv_binary_cost (x, 1, CONSTANT_P (XEXP (x, 1)) ? 4 : 9);
      return false;

    case ABS:
      *total = COSTS_N_INSNS (float_mode_p ? 1 : 3);
      return false;

    case LO_SUM:
      *total = set_src_cost (XEXP (x, 0), speed);
      return true;

    case LT:
    case LTU:
    case LE:
    case LEU:
    case GT:
    case GTU:
    case GE:
    case GEU:
    case EQ:
    case NE:
    case UNORDERED:
    case LTGT:
      /* Branch comparisons have VOIDmode, so use the first operand's
	 mode instead.  */
      mode = GET_MODE (XEXP (x, 0));
      if (float_mode_p)
	*total = tune_info->fp_add[mode == DFmode];
      else
	*total = riscv_binary_cost (x, 1, 3);
      return false;

    case MINUS:
      if (float_mode_p
	  && !HONOR_NANS (mode)
	  && !HONOR_SIGNED_ZEROS (mode))
	{
	  /* See if we can use NMADD or NMSUB.  See riscv.md for the
	     associated patterns.  */
	  rtx op0 = XEXP (x, 0);
	  rtx op1 = XEXP (x, 1);
	  if (GET_CODE (op0) == MULT && GET_CODE (XEXP (op0, 0)) == NEG)
	    {
	      *total = (tune_info->fp_mul[mode == DFmode]
			+ set_src_cost (XEXP (XEXP (op0, 0), 0), speed)
			+ set_src_cost (XEXP (op0, 1), speed)
			+ set_src_cost (op1, speed));
	      return true;
	    }
	  if (GET_CODE (op1) == MULT)
	    {
	      *total = (tune_info->fp_mul[mode == DFmode]
			+ set_src_cost (op0, speed)
			+ set_src_cost (XEXP (op1, 0), speed)
			+ set_src_cost (XEXP (op1, 1), speed));
	      return true;
	    }
	}
      /* Fall through.  */

    case PLUS:
      if (float_mode_p)
	*total = tune_info->fp_add[mode == DFmode];
      else
	*total = riscv_binary_cost (x, 1, 4);
      return false;

    case NEG:
      if (float_mode_p
	  && !HONOR_NANS (mode)
	  && HONOR_SIGNED_ZEROS (mode))
	{
	  /* See if we can use NMADD or NMSUB.  See riscv.md for the
	     associated patterns.  */
	  rtx op = XEXP (x, 0);
	  if ((GET_CODE (op) == PLUS || GET_CODE (op) == MINUS)
	      && GET_CODE (XEXP (op, 0)) == MULT)
	    {
	      *total = (tune_info->fp_mul[mode == DFmode]
			+ set_src_cost (XEXP (XEXP (op, 0), 0), speed)
			+ set_src_cost (XEXP (XEXP (op, 0), 1), speed)
			+ set_src_cost (XEXP (op, 1), speed));
	      return true;
	    }
	}

      if (float_mode_p)
	*total = tune_info->fp_add[mode == DFmode];
      else
	*total = COSTS_N_INSNS (GET_MODE_SIZE (mode) > UNITS_PER_WORD ? 4 : 1);
      return false;

    case MULT:
      if (float_mode_p)
	*total = tune_info->fp_mul[mode == DFmode];
      else if (GET_MODE_SIZE (mode) > UNITS_PER_WORD)
	*total = 3 * tune_info->int_mul[0] + COSTS_N_INSNS (2);
      else if (!speed)
	*total = COSTS_N_INSNS (1);
      else
	*total = tune_info->int_mul[mode == DImode];
      return false;

    case DIV:
    case SQRT:
    case MOD:
      if (float_mode_p)
	{
	  *total = tune_info->fp_div[mode == DFmode];
	  return false;
	}
      /* Fall through.  */

    case UDIV:
    case UMOD:
      if (speed)
	*total = tune_info->int_div[mode == DImode];
      else
	*total = COSTS_N_INSNS (1);
      return false;

    case SIGN_EXTEND:
      *total = riscv_sign_extend_cost (mode, XEXP (x, 0));
      return false;

    case ZERO_EXTEND:
      *total = riscv_zero_extend_cost (mode, XEXP (x, 0));
      return false;

    case FLOAT:
    case UNSIGNED_FLOAT:
    case FIX:
    case FLOAT_EXTEND:
    case FLOAT_TRUNCATE:
      *total = tune_info->fp_add[mode == DFmode];
      return false;

    default:
      return false;
    }
}

/* Implement TARGET_ADDRESS_COST.  */

static int
riscv_address_cost (rtx addr, enum machine_mode mode,
		    addr_space_t as ATTRIBUTE_UNUSED,
		    bool speed ATTRIBUTE_UNUSED)
{
  struct riscv_address_info addr_info;
  int n = 1;

  if (riscv_address_insns (addr, mode, false)) {
    if ((Pulp_Cpu>=PULP_V0) && !TARGET_MASK_NOPOSTMOD) {
	if (TARGET_MASK_NOFINDUCT) {
       		riscv_classify_address (&addr_info, addr, mode, false);
       		/* Discourage *reg(reg) since this pattern decrease induction attractiviry */
       		if (addr_info.type == ADDRESS_REG_REG ||
		    (addr_info.type == ADDRESS_REG && addr_info.offset == const0_rtx)) n++;
       		else if (addr_info.type == ADDRESS_REG_POST_INC || addr_info.type == ADDRESS_REG_POST_DEC ||
			 addr_info.type == ADDRESS_REG_POST_MODIFY) n--;

	} else {
       		/* Even more discouraged *reg(reg) since this pattern decrease induction attractiviry */
		if (GET_CODE(addr) == PLUS) {
                        if (GET_CODE (XEXP (addr, 0)) == REG && GET_CODE (XEXP (addr, 1)) == REG) return 16;
                        else return 12;
                } else return 0;
	}

    }
    return n;
  }
  return 0;
}

/* Return one word of double-word value OP.  HIGH_P is true to select the
   high part or false to select the low part. */

rtx
riscv_subword (rtx op, bool high_p)
{
  unsigned int byte;
  enum machine_mode mode;

  mode = GET_MODE (op);
  if (mode == VOIDmode)
    mode = TARGET_64BIT ? TImode : DImode;

  byte = high_p ? UNITS_PER_WORD : 0;

  if (FP_REG_RTX_P (op))
    return gen_rtx_REG (word_mode, REGNO (op) + high_p);

  if (MEM_P (op))
    return adjust_address (op, word_mode, byte);

  return simplify_gen_subreg (word_mode, op, mode, byte);
}

/* Return true if a 64-bit move from SRC to DEST should be split into two.  */

bool
riscv_split_64bit_move_p (rtx dest, rtx src)
{
  /* All 64b moves are legal in 64b mode.  All 64b FPR <-> FPR and
     FPR <-> MEM moves are legal in 32b mode, too.  Although
     FPR <-> GPR moves are not available in general in 32b mode,
     we can at least load 0 into an FPR with fcvt.d.w fpr, x0. */
  return !(TARGET_64BIT
	   || (FP_REG_RTX_P (src) && FP_REG_RTX_P (dest))
	   || (FP_REG_RTX_P (dest) && MEM_P (src))
	   || (FP_REG_RTX_P (src) && MEM_P (dest))
	   || (FP_REG_RTX_P(dest) && src == CONST0_RTX(GET_MODE(src))));
}

/* Split a doubleword move from SRC to DEST.  On 32-bit targets,
   this function handles 64-bit moves for which riscv_split_64bit_move_p
   holds.  For 64-bit targets, this function handles 128-bit moves.  */

void
riscv_split_doubleword_move (rtx dest, rtx src)
{
  rtx low_dest;

   /* The operation can be split into two normal moves.  Decide in
      which order to do them.  */
   low_dest = riscv_subword (dest, false);
   if (REG_P (low_dest) && reg_overlap_mentioned_p (low_dest, src))
     {
       riscv_emit_move (riscv_subword (dest, true), riscv_subword (src, true));
       riscv_emit_move (low_dest, riscv_subword (src, false));
     }
   else
     {
       riscv_emit_move (low_dest, riscv_subword (src, false));
       riscv_emit_move (riscv_subword (dest, true), riscv_subword (src, true));
     }
}

const char * riscv_explicit_load_store(rtx AddrReg, rtx SrcReg, unsigned int Address, int IsLoad)

{
	rtx xoperands[4];
	rtx BaseOp, OffsetOp;
	unsigned int Base;
	int Offset;

	Base = (Address>>12)&0x0FFFFF;
	BaseOp = GEN_INT (trunc_int_for_mode (Base, SImode));
	Offset = ((int) ((Address & 0x0FFF)<<20))>>20;
	OffsetOp = GEN_INT (trunc_int_for_mode (Offset, SImode));

	xoperands[0] = AddrReg;
	xoperands[1] = BaseOp;
	xoperands[2] = OffsetOp;
	xoperands[3] = SrcReg;
	output_asm_insn("lui\t%0,%1", xoperands);
	if (IsLoad) output_asm_insn("p.elw\t%0,%2(%0)", xoperands);
	else output_asm_insn("sw\t%3,%2(%0)", xoperands);

	return "";
}


/* Return the appropriate instructions to move SRC into DEST.  Assume
   that SRC is operand 1 and DEST is operand 0.  */

const char *
riscv_output_move (rtx dest, rtx src)
{
  enum rtx_code dest_code, src_code;
  enum machine_mode mode;
  bool dbl_p;

  dest_code = GET_CODE (dest);
  src_code = GET_CODE (src);
  mode = GET_MODE (dest);
  dbl_p = (GET_MODE_SIZE (mode) == 8);
/*
printf("Output Move:\n");
printf("Dest:\n");
debug_rtx(dest);
printf("Src:\n");
debug_rtx(src);
printf("----------------------------\n");
*/

  if (dbl_p && riscv_split_64bit_move_p (dest, src))
    return "#";

  if (dest_code == REG && GP_REG_P (REGNO (dest)))
    {
      if (src_code == REG && FP_REG_P (REGNO (src)))
	return dbl_p ? "fmv.x.d\t%0,%1" : "fmv.x.s\t%0,%1";

      if (src_code == MEM) {
  	struct riscv_address_info addr;
        int Prefix=0;

	riscv_classify_address (&addr, XEXP (src, 0), word_mode, true);
  	if ((Pulp_Cpu>=PULP_V0) && !TARGET_MASK_NOINDREGREG) {
    		if (addr.type == ADDRESS_REG_REG) Prefix = 1;
        }
	if (addr.type == ADDRESS_TINY_SYMBOL /* || addr.type == ADDRESS_REG_TINY_SYMBOL */ ) {
		switch (GET_MODE_SIZE (mode)) {
	  		case 1: return Prefix?"p.lbu\t%0,%%tiny(%1)(x0)":"lbu\t%0,%%tiny(%1)(x0)";
	  		case 2: return Prefix?"p.lhu\t%0,%%tiny(%1)(x0)":"lhu\t%0,%%tiny(%1)(x0)";
	  		case 4: return Prefix?"p.lw\t%0,%%tiny(%1)(x0)":"lw\t%0,%%tiny(%1)(x0)";
	  		case 8: return "ld\t%0,%1";
	  	}
	} else {
		switch (GET_MODE_SIZE (mode)) {
	  		case 1: return Prefix?"p.lbu\t%0,%1":"lbu\t%0,%1";
	  		case 2: return Prefix?"p.lhu\t%0,%1":"lhu\t%0,%1";
	  		case 4: return Prefix?"p.lw\t%0,%1":"lw\t%0,%1";
	  		case 8: return "ld\t%0,%1";
	  	}
	}
       }

      if (src_code == CONST_INT) return "li\t%0,%1";
      else if (src_code == CONST_VECTOR) {
		if (((Pulp_Cpu>=PULP_V2) && !TARGET_MASK_NOVECT) && riscv_replicated_const_vector(src, -32, 31)) {
			if (GET_MODE(src)==V4QImode) return "pv.add.sci.b\t%0,x0,%W1";
			else return "pv.add.sci.h\t%0,x0,%w1";
		} else {
			return "li\t%0,%V1";
		}
      }

      if (src_code == HIGH)
	return "lui\t%0,%h1";

      if (symbolic_operand (src, VOIDmode))
	switch (riscv_classify_symbolic_expression (src))
	  {
	  case SYMBOL_GOT_DISP: return "la\t%0,%1";
	  case SYMBOL_ABSOLUTE: return "lla\t%0,%1";
	  // case SYMBOL_TINY_ABSOLUTE: return "addi\t%0,x0,%1";
	  case SYMBOL_TINY_ABSOLUTE: return "lla\t%0,%1";
	  default: gcc_unreachable();
	  }
    }
  if ((src_code == REG && GP_REG_P (REGNO (src)))
      || (src == CONST0_RTX (mode)))
    {
      if (dest_code == REG)
	{
	  if (GP_REG_P (REGNO (dest)))
	    return "mv\t%0,%z1";

	  if (FP_REG_P (REGNO (dest)))
	    {
	      if (!dbl_p)
		return "fmv.s.x\t%0,%z1";
	      if (TARGET_64BIT)
		return "fmv.d.x\t%0,%z1";
	      /* in RV32, we can emulate fmv.d.x %0, x0 using fcvt.d.w */
	      gcc_assert (src == CONST0_RTX (mode));
	      return "fcvt.d.w\t%0,x0";
	    }
	}
      if (dest_code == MEM) {
  	struct riscv_address_info addr;
        int Prefix=0;

	riscv_classify_address (&addr, XEXP (dest, 0), word_mode, true);
  	if ((Pulp_Cpu>=PULP_V0) && !TARGET_MASK_NOINDREGREG) {
    		if (addr.type == ADDRESS_REG_REG) Prefix = 1;
        }
	if (addr.type == ADDRESS_TINY_SYMBOL /* || addr.type == ADDRESS_REG_TINY_SYMBOL */) {
		switch (GET_MODE_SIZE (mode)) {
	  		case 1: return Prefix?"p.sb\t%z1,%%tiny(%0)(x0)":"sb\t%z1,%%tiny(%0)(x0)";
	  		case 2: return Prefix?"p.sh\t%z1,%%tiny(%0)(x0)":"sh\t%z1,%%tiny(%0)(x0)";
	  		case 4: return Prefix?"p.sw\t%z1,%%tiny(%0)(x0)":"sw\t%z1,%%tiny(%0)(x0)";
	  		case 8: return "sd\t%z1,%0";
	  	}
	} else {
		switch (GET_MODE_SIZE (mode)) {
	  		case 1: return Prefix?"p.sb\t%z1,%0":"sb\t%z1,%0";
	  		case 2: return Prefix?"p.sh\t%z1,%0":"sh\t%z1,%0";
	  		case 4: return Prefix?"p.sw\t%z1,%0":"sw\t%z1,%0";
	  		case 8: return "sd\t%z1,%0";
	  	}
	}
      }
    }
  if (src_code == REG && FP_REG_P (REGNO (src)))
    {
      if (dest_code == REG && FP_REG_P (REGNO (dest)))
	return dbl_p ? "fmv.d\t%0,%1" : "fmv.s\t%0,%1";

      if (dest_code == MEM)
	return dbl_p ? "fsd\t%1,%0" : "fsw\t%1,%0";
    }
  if (dest_code == REG && FP_REG_P (REGNO (dest)))
    {
      if (src_code == MEM)
	return dbl_p ? "fld\t%0,%1" : "flw\t%0,%1";
    }
  gcc_unreachable ();
}

/* Return true if CMP1 is a suitable second operand for integer ordering
   test CODE.  See also the *sCC patterns in riscv.md.  */

static bool
riscv_int_order_operand_ok_p (enum rtx_code code, rtx cmp1)
{
  switch (code)
    {
    case GT:
    case GTU:
      return reg_or_0_operand (cmp1, VOIDmode);

    case GE:
    case GEU:
      return cmp1 == const1_rtx;

    case LT:
    case LTU:
      return arith_operand (cmp1, VOIDmode);

    case LE:
      return sle_operand (cmp1, VOIDmode);

    case LEU:
      return sleu_operand (cmp1, VOIDmode);

    default:
      gcc_unreachable ();
    }
}

/* Return true if *CMP1 (of mode MODE) is a valid second operand for
   integer ordering test *CODE, or if an equivalent combination can
   be formed by adjusting *CODE and *CMP1.  When returning true, update
   *CODE and *CMP1 with the chosen code and operand, otherwise leave
   them alone.  */

static bool
riscv_canonicalize_int_order_test (enum rtx_code *code, rtx *cmp1,
				  enum machine_mode mode)
{
  HOST_WIDE_INT plus_one;

  if (riscv_int_order_operand_ok_p (*code, *cmp1))
    return true;

  if (CONST_INT_P (*cmp1))
    switch (*code)
      {
      case LE:
	plus_one = trunc_int_for_mode (UINTVAL (*cmp1) + 1, mode);
	if (INTVAL (*cmp1) < plus_one)
	  {
	    *code = LT;
	    *cmp1 = force_reg (mode, GEN_INT (plus_one));
	    return true;
	  }
	break;

      case LEU:
	plus_one = trunc_int_for_mode (UINTVAL (*cmp1) + 1, mode);
	if (plus_one != 0)
	  {
	    *code = LTU;
	    *cmp1 = force_reg (mode, GEN_INT (plus_one));
	    return true;
	  }
	break;
      default:
	break;
      }
  return false;
}

/* Compare CMP0 and CMP1 using ordering test CODE and store the result
   in TARGET.  CMP0 and TARGET are register_operands.  If INVERT_PTR
   is nonnull, it's OK to set TARGET to the inverse of the result and
   flip *INVERT_PTR instead.  */

static void
riscv_emit_int_order_test (enum rtx_code code, bool *invert_ptr,
			  rtx target, rtx cmp0, rtx cmp1)
{
  enum machine_mode mode;

  /* First see if there is a RISCV instruction that can do this operation.
     If not, try doing the same for the inverse operation.  If that also
     fails, force CMP1 into a register and try again.  */
  mode = GET_MODE (cmp0);
  if (riscv_canonicalize_int_order_test (&code, &cmp1, mode))
    riscv_emit_binary (code, target, cmp0, cmp1);
  else
    {
      enum rtx_code inv_code = reverse_condition (code);
      if (!riscv_canonicalize_int_order_test (&inv_code, &cmp1, mode))
	{
	  cmp1 = force_reg (mode, cmp1);
	  riscv_emit_int_order_test (code, invert_ptr, target, cmp0, cmp1);
	}
      else if (invert_ptr == 0)
	{
	  rtx inv_target;

	  inv_target = riscv_force_binary (GET_MODE (target),
					  inv_code, cmp0, cmp1);
	  riscv_emit_binary (XOR, target, inv_target, const1_rtx);
	}
      else
	{
	  *invert_ptr = !*invert_ptr;
	  riscv_emit_binary (inv_code, target, cmp0, cmp1);
	}
    }
}

/* Return a register that is zero iff CMP0 and CMP1 are equal.
   The register will have the same mode as CMP0.  */

static rtx
riscv_zero_if_equal (rtx cmp0, rtx cmp1)
{
  if (cmp1 == const0_rtx)
    return cmp0;

  return expand_binop (GET_MODE (cmp0), sub_optab,
		       cmp0, cmp1, 0, 0, OPTAB_DIRECT);
}

/* Return false if we can easily emit code for the FP comparison specified
   by *CODE.  If not, set *CODE to its inverse and return true. */

static bool
riscv_reversed_fp_cond (enum rtx_code *code)
{
  switch (*code)
    {
    case EQ:
    case LT:
    case LE:
    case GT:
    case GE:
    case LTGT:
    case ORDERED:
      /* We know how to emit code for these cases... */
      return false;

    default:
      /* ...but we must invert these and rely on the others. */
      *code = reverse_condition_maybe_unordered (*code);
      return true;
    }
}

/* Convert a comparison into something that can be used in a branch or
   conditional move.  On entry, *OP0 and *OP1 are the values being
   compared and *CODE is the code used to compare them.

   Update *CODE, *OP0 and *OP1 so that they describe the final comparison. */

static void
riscv_emit_compare (enum rtx_code *code, rtx *op0, rtx *op1)
{
  rtx cmp_op0 = *op0;
  rtx cmp_op1 = *op1;

  if (GET_MODE_CLASS (GET_MODE (*op0)) == MODE_INT)
    {
      if (splittable_const_int_operand (cmp_op1, VOIDmode))
	{
	  HOST_WIDE_INT rhs = INTVAL (cmp_op1), new_rhs;
	  enum rtx_code new_code;

	  switch (*code)
	    {
	    case LTU: new_rhs = rhs - 1; new_code = LEU; goto try_new_rhs;
	    case LEU: new_rhs = rhs + 1; new_code = LTU; goto try_new_rhs;
	    case GTU: new_rhs = rhs + 1; new_code = GEU; goto try_new_rhs;
	    case GEU: new_rhs = rhs - 1; new_code = GTU; goto try_new_rhs;
	    case LT: new_rhs = rhs - 1; new_code = LE; goto try_new_rhs;
	    case LE: new_rhs = rhs + 1; new_code = LT; goto try_new_rhs;
	    case GT: new_rhs = rhs + 1; new_code = GE; goto try_new_rhs;
	    case GE: new_rhs = rhs - 1; new_code = GT;
	    try_new_rhs:
	      /* Convert e.g. OP0 > 4095 into OP0 >= 4096.  */
	      if ((rhs < 0) == (new_rhs < 0)
		  && riscv_integer_cost (new_rhs) < riscv_integer_cost (rhs))
		{
		  *op1 = GEN_INT (new_rhs);
		  *code = new_code;
		}
	      break;

	    case EQ:
	    case NE:
              if ((Pulp_Cpu>=PULP_V2) && !TARGET_MASK_NOBALL && (*code == EQ || *code == NE) && (GET_CODE(*op1) == CONST_INT) &&
	          (INTVAL(*op1) >= -16) && (INTVAL(*op1) <= 15)) {
              } else if (SMALL_OPERAND (-rhs))
	      /* Convert e.g. OP0 == 2048 into OP0 - 2048 == 0.  */
		{
		  *op0 = gen_reg_rtx (GET_MODE (cmp_op0));
		  riscv_emit_binary (PLUS, *op0, cmp_op0, GEN_INT (-rhs));
		  *op1 = const0_rtx;
		}
	    default:
	      break;
	    }
	}

      if ((*op1 == const0_rtx) || ((Pulp_Cpu>=PULP_V2) && !TARGET_MASK_NOBALL && (*code == EQ || *code == NE) && (GET_CODE(*op1) == CONST_INT) &&
	  (INTVAL(*op1) >= -16) && (INTVAL(*op1) <= 15))) {
      } else {
	*op1 = force_reg (GET_MODE (cmp_op0), *op1);
      }


/*
      if (*op1 != const0_rtx && !(((Pulp_Cpu>=PULP_V2) && !TARGET_MASK_NOBALL) && *code == EQ && *op1 == constm1_rtx))
	*op1 = force_reg (GET_MODE (cmp_op0), *op1);
*/
    }
  else
    {
      /* For FP comparisons, set an integer register with the result of the
	 comparison, then branch on it. */
      rtx tmp0, tmp1, final_op;
      enum rtx_code fp_code = *code;
      *code = riscv_reversed_fp_cond (&fp_code) ? EQ : NE;

      switch (fp_code)
	{
	case ORDERED:
	  /* a == a && b == b */
	  tmp0 = gen_reg_rtx (SImode);
	  riscv_emit_binary (EQ, tmp0, cmp_op0, cmp_op0);
	  tmp1 = gen_reg_rtx (SImode);
	  riscv_emit_binary (EQ, tmp1, cmp_op1, cmp_op1);
	  final_op = gen_reg_rtx (SImode);
	  riscv_emit_binary (AND, final_op, tmp0, tmp1);
	  break;

	case LTGT:
	  /* a < b || a > b */
	  tmp0 = gen_reg_rtx (SImode);
	  riscv_emit_binary (LT, tmp0, cmp_op0, cmp_op1);
	  tmp1 = gen_reg_rtx (SImode);
	  riscv_emit_binary (GT, tmp1, cmp_op0, cmp_op1);
	  final_op = gen_reg_rtx (SImode);
	  riscv_emit_binary (IOR, final_op, tmp0, tmp1);
	  break;

	case EQ:
	case LE:
	case LT:
	case GE:
	case GT:
	  /* We have instructions for these cases. */
	  final_op = gen_reg_rtx (SImode);
	  riscv_emit_binary (fp_code, final_op, cmp_op0, cmp_op1);
	  break;

	default:
	  gcc_unreachable ();
	}

      /* Compare the binary result against 0. */
      *op0 = final_op;
      *op1 = const0_rtx;
    }
}

/* Try performing the comparison in OPERANDS[1], whose arms are OPERANDS[2]
   and OPERAND[3].  Store the result in OPERANDS[0].

   On 64-bit targets, the mode of the comparison and target will always be
   SImode, thus possibly narrower than that of the comparison's operands.  */

void
riscv_expand_scc (rtx operands[])
{
  rtx target = operands[0];
  enum rtx_code code = GET_CODE (operands[1]);
  rtx op0 = operands[2];
  rtx op1 = operands[3];

  gcc_assert (GET_MODE_CLASS (GET_MODE (op0)) == MODE_INT);
  if (code == EQ || code == NE)
    {
      rtx zie = riscv_zero_if_equal (op0, op1);
      riscv_emit_binary (code, target, zie, const0_rtx);
    }
  else
    riscv_emit_int_order_test (code, 0, target, op0, op1);
}

/* Compare OPERANDS[1] with OPERANDS[2] using comparison code
   CODE and jump to OPERANDS[3] if the condition holds.  */

void
riscv_expand_conditional_branch (rtx *operands)
{
  enum rtx_code code = GET_CODE (operands[0]);
  rtx op0 = operands[1];
  rtx op1 = operands[2];
  rtx condition;

  riscv_emit_compare (&code, &op0, &op1);
  condition = gen_rtx_fmt_ee (code, VOIDmode, op0, op1);
  emit_jump_insn (gen_condjump (condition, operands[3]));
}

/* Helper for vector support, pulp v2 */


void riscv_expand_vector_init(rtx target, rtx vals)

{
	enum machine_mode mode = GET_MODE (target);
	enum machine_mode inner_mode = GET_MODE_INNER (mode);
	int n_elts = GET_MODE_NUNITS (mode);
	int n_var = 0;
	bool all_same = true;
	bool first = true;
	rtx x;
	int i;

	for (i = 0; i < n_elts; ++i) {
		x = XVECEXP (vals, 0, i);
		if (!CONSTANT_P (x)) ++n_var;
		if (i > 0 && !rtx_equal_p (x, XVECEXP (vals, 0, 0))) all_same = false;
	}
	if (all_same) {
		rtx y;
		if (inner_mode != GET_MODE(XVECEXP (vals, 0, 0)))
			// Should not happen but for vector shift with a replicated scalar it happens
			y = simplify_gen_subreg (inner_mode,  XVECEXP (vals, 0, 0), GET_MODE(XVECEXP (vals, 0, 0)), 0);
		else y =  XVECEXP (vals, 0, 0);
		x = copy_to_mode_reg (inner_mode, y); // XVECEXP (vals, 0, 0));
		emit_insn (gen_rtx_SET (VOIDmode, target, gen_rtx_VEC_DUPLICATE (mode, x)));
		return;
	}
	// i = GET_MODE_NUNITS (mode);
	// while (i-- > 0) {
	/* Since we are in little endian, start from the lsp part. If the first item can be safely moved to a register
	   with all slices except its one known to be 0 do it otherwise move 0 to the target reg and then insert.
	   All slices are zeroed therefore we can skip them when their actual value is zero.
	   We could also extract all slices with an imm value an forge a 32bit constant to be moved to target reg, we
	   then skip all immediates since they are already in
	*/
	for (i = 0; i < n_elts; ++i) {
		x = copy_to_mode_reg(inner_mode, XVECEXP (vals, 0, i));
		switch (mode) {
			case V2HImode:
				if (first) emit_insn(gen_vec_set_firstv2hi(target, x, GEN_INT(i)));
				else if (XVECEXP (vals, 0, i) != const0_rtx) emit_insn(gen_vec_setv2hi(target, x, GEN_INT(i)));
				break;
			case V4QImode:
				if (first) emit_insn(gen_vec_set_firstv4qi(target, x, GEN_INT(i)));
				else if (XVECEXP (vals, 0, i) != const0_rtx) emit_insn(gen_vec_setv4qi(target, x, GEN_INT(i)));
				break;
			default:
				abort();
		}
		first = false;
	}
}

int riscv_replicated_const_vector (rtx op, int min_val, int max_val)

{
        if (GET_CODE (op) == CONST_VECTOR) {
                enum machine_mode mode = GET_MODE(op);
                enum machine_mode inner_mode = GET_MODE_INNER (mode);
                HOST_WIDE_INT ref=0;
                int ref1;
                HOST_WIDE_INT mask = GET_MODE_MASK (inner_mode);
                int i;

                for (i = 0; i < GET_MODE_NUNITS (mode); i++) {
                        HOST_WIDE_INT cval = INTVAL (CONST_VECTOR_ELT (op, i)) & mask;
                        if (i==0) ref = cval;
                        else if (ref != cval) return 0;
                }
                if (GET_MODE_NUNITS (mode) == 2) ref1 = ((int)ref<<16)>>16; else ref1 = ((int)ref<<24)>>24;
                return (((int) ref1 >= min_val) && ((int) ref1 <= max_val));
        }
        return 0;
}

rtx
riscv_to_int_mode (rtx x)
{
  enum machine_mode mode = GET_MODE (x);
  return VOIDmode == mode ? x : simplify_gen_subreg (int_mode_for_mode (mode), x, mode, 0);
}

/* Various helper to validate operands for pulp v2 */

int riscv_bit_size_for_clip (HOST_WIDE_INT i)

{
	int rv;

	for (rv = 0; rv < 31; rv ++)
		if (((HOST_WIDE_INT) 1 << rv) > i) return rv + 1;
	gcc_unreachable ();
}

bool riscv_valid_clip_operands (rtx ux, rtx lx, int sign)

{
	HOST_WIDE_INT u = INTVAL (ux);
	HOST_WIDE_INT l = INTVAL (lx);
	int i;


	if (sign) {
		for (i = 0; i < 30; i ++)
			if ((u == ((HOST_WIDE_INT) 1 << i) - 1) && (l == - ((HOST_WIDE_INT) 1 << i))) return true;
	} else {
		if (l != 0) return false;
		for (i = 0; i < 30; i ++)
			if ((u == ((HOST_WIDE_INT) 1 << i) - 1)) return true;
	}
	return false;
}

int riscv_valid_norm_round_imm_op(rtx norm_oper, rtx round_oper)

{

	if (GET_CODE(norm_oper) == CONST_INT) {
		HOST_WIDE_INT val = INTVAL (norm_oper);
		HOST_WIDE_INT val1;
		if ((int) val < 0 || (int) val > 31) return 0;
		if (!round_oper) return 1;
		if (GET_CODE(round_oper) != CONST_INT) return 0;
		val1 = INTVAL (round_oper);
		if ((1 << ((int) val - 1)) ==  (int) val1) return 1;
	}
	return 0;
}

bool riscv_valid_bit_field_imm_operand(rtx x, rtx shift_op, int Set_Mode, int *Size, int *Offset)

{
	int V, O=0, S=0;
	int i, j;

	if (GET_CODE(x) != CONST_INT) return false;
	if (shift_op && GET_CODE(shift_op) != CONST_INT) return false;
	V = (int) (INTVAL (x));

	if (Set_Mode) {
		for (i=0; i<32; i++) {
			if ((1<<i) & V) {
				for (j=i; j<32; j++) {
					if (!((1<<j) & V)) break;
					S++;
				}
				for (;j<32; j++) if (((1<<j) & V)) return false;
				if (shift_op && ((int) INTVAL(shift_op) != O)) return false;
				if (Size) *Size=S;
				if (Offset) *Offset=O;
				return true;
			}
			O++;
		}
	} else {
		for (i=0; i<32; i++) {
			if (!((1<<i) & V)) {
				for (j=i; j<32; j++) {
					if ((1<<j) & V) break;
					S++;
				}
				for (;j<32; j++) if (!((1<<j) & V)) return false;
				if (Size) *Size=S;
				if (Offset) *Offset=O;
				return true;
			}
			O++;
		}
	}
	return false;
}

int riscv_valid_bit_insert(rtx op1, rtx op2, rtx op3, int *Len, int *Off)

{
	unsigned int Imm1, Imm2, Imm3;
	int i;
	unsigned int I=0;
	unsigned int L=0;
	unsigned int H=0;

	if (GET_CODE(op1) != CONST_INT || GET_CODE(op2) != CONST_INT || (op3 && (GET_CODE(op3) != CONST_INT))) return 0;
	Imm2 = (unsigned int) (INTVAL (op2));
	Imm1 = (unsigned int) (INTVAL (op1));
	if (op3) Imm3 = (unsigned int) (INTVAL (op3)); else Imm3 = ~Imm1;
	// fprintf(stderr, "Binsert candidate: I1: %X, I2: %d, I3: %X\n", Imm1, Imm2, Imm3);
	for (i=0; i<32; I++, i++) if ((Imm3 & (1<<i))!=0) break;
	for (;i<32; L++, i++) if ((Imm3 & (1<<i))==0) break;
	for (;i<32; H++, i++) if ((Imm3 & (1<<i))!=0) {
		// fprintf(stderr, "Wrong header\n");
		return 0;
	}
	if (op3 == NULL && H!=0) return 0;
	if ((unsigned int) (I) != Imm2) {
		// fprintf(stderr, "I: %d != Imm2: %d\n", I, Imm2);
		return 0;
	}
	if (Imm3 != (~Imm1)) {
		// fprintf(stderr, "Imm3: %X != ~Imm1: %d\n", Imm3, (~Imm1));
		return 0;
	}
	if (Len) *Len = L;
	if (Off) *Off = I;
	// fprintf(stderr, "Binsert candidate OK: Len: %d, Offset: %d, H: %d\n", L, I, H);
	return 1;
}
int riscv_bitmask (unsigned HOST_WIDE_INT x, int *len, enum machine_mode mode)
{
	int top, bottom;

	top = floor_log2 (x);
	if (top == HOST_BITS_PER_WIDE_INT - 1) x = -x;
	else x = ((unsigned HOST_WIDE_INT) 1 << (top + 1)) - x;

	bottom = exact_log2 (x);
	if (mode == VOIDmode || bottom == -1) return bottom;

	if (mode == SImode && top > 31) {
		if (top == 63) top = 31;
		else gcc_unreachable ();
	}
	*len = top - bottom + 1;
	return bottom;
}

bool riscv_bitmask_p (unsigned HOST_WIDE_INT x)

{
	return (!TARGET_MASK_NOBITOP) && riscv_bitmask (x, NULL, VOIDmode) != -1;
}

bool riscv_bitmask_ins_p (unsigned HOST_WIDE_INT x, int pos, enum machine_mode mode)

{
	int len, position;
	if (!(!TARGET_MASK_NOBITOP)) return 0;
	position = riscv_bitmask (x, &len, mode);
	return position == 0 && len == pos;
}

bool riscv_bottom_bitmask_p (unsigned HOST_WIDE_INT x)

{
	return (!TARGET_MASK_NOBITOP) && riscv_bitmask (x, NULL, VOIDmode) == 0;
}

bool riscv_valid_permute_operands(rtx op1, rtx op2, rtx sel)

{
	if ((GET_CODE(sel) == CONST_INT || GET_CODE(sel) == CONST_VECTOR)) {
		return (rtx_equal_p(op1, op2));
	} else return true;
}

/* TARGET_.... functions */

/* Implements TARGET_VECTOR_MODE_SUPPORTED_P */

static bool riscv_vector_mode_supported_p (enum machine_mode mode)
{
  switch (mode)
    {
    case V2HImode:
    case V4QImode:
    case V2QImode:
      return true;
    default:
      return false;
    }
}

/* Implements  TARGET_VECTORIZE_PREFERRED_SIMD_MODE */

static enum machine_mode riscv_preferred_simd_mode (enum machine_mode mode)
{
  switch (mode)
    {
    case HImode:
      return V2HImode;
    case QImode:
      return V4QImode;
    default:
      return word_mode;
    }
}

/* Implements TARGET_VECTORIZE_SUPPORT_VECTOR_MISALIGNMENT */

static bool
riscv_builtin_support_vector_misalignment (enum machine_mode mode ATTRIBUTE_UNUSED,
                                          const_tree type ATTRIBUTE_UNUSED,
                                          int misalignment,
                                          bool is_packed)
{
        HOST_WIDE_INT align = TYPE_ALIGN_UNIT (type);

        if (is_packed) return align == 1;

        /* If the misalignment is unknown, we should be able to handle the access
         so long as it is not to a member of a packed data structure.  */
        if (misalignment == -1) return true;

        /* Return true if the misalignment is a multiple of the natural alignment
         of the vector's element type.  This is probably always going to be
         true in practice, since we've already established that this isn't a
         packed access.  */
        return ((misalignment % align) == 0);
}

/* Implements TARGET_VECTORIZE_VECTOR_ALIGNMENT_REACHABLE */

static bool
riscv_vector_alignment_reachable (const_tree type ATTRIBUTE_UNUSED, bool is_packed)
{
    return !is_packed;
}

/* Implements TARGET_VECTORIZE_BUILTIN_VECTORIZATION_COST */

static int
riscv_builtin_vectorization_cost (enum vect_cost_for_stmt type_of_cost,
                                 tree vectype,
                                 int misalign ATTRIBUTE_UNUSED)
{
  unsigned elements;

  switch (type_of_cost)
    {
      case scalar_stmt:
      case scalar_load:
      case scalar_store:
      case vector_stmt:
      case vector_load:
      case vector_store:
      case vec_to_scalar:
      case scalar_to_vec:
      case cond_branch_not_taken:
      case vec_perm:
      case vec_promote_demote:
        return 1;

      case unaligned_load:
      case unaligned_store:
        return 1;

      case cond_branch_taken:
        return 1;

      case vec_construct:
        elements = TYPE_VECTOR_SUBPARTS (vectype);
        return elements / 2 + 1;

      default:
        gcc_unreachable ();
    }
}

/* Implement TARGET_FUNCTION_ARG_BOUNDARY.  Every parameter gets at
   least PARM_BOUNDARY bits of alignment, but will be given anything up
   to STACK_BOUNDARY bits if the type requires it.  */

static unsigned int
riscv_function_arg_boundary (enum machine_mode mode, const_tree type)
{
  unsigned int alignment;

  alignment = type ? TYPE_ALIGN (type) : GET_MODE_ALIGNMENT (mode);
  if (alignment < PARM_BOUNDARY)
    alignment = PARM_BOUNDARY;
  if (alignment > STACK_BOUNDARY)
    alignment = STACK_BOUNDARY;
  return alignment;
}

/* Fill INFO with information about a single argument.  CUM is the
   cumulative state for earlier arguments.  MODE is the mode of this
   argument and TYPE is its type (if known).  NAMED is true if this
   is a named (fixed) argument rather than a variable one.  */

static void
riscv_get_arg_info (struct riscv_arg_info *info, const CUMULATIVE_ARGS *cum,
		   enum machine_mode mode, const_tree type, bool named)
{
  bool doubleword_aligned_p;
  unsigned int num_bytes, num_words, max_regs;

  /* Work out the size of the argument.  */
  num_bytes = type ? int_size_in_bytes (type) : GET_MODE_SIZE (mode);
  num_words = (num_bytes + UNITS_PER_WORD - 1) / UNITS_PER_WORD;

  /* Scalar, complex and vector floating-point types are passed in
     floating-point registers, as long as this is a named rather
     than a variable argument.  */
  info->fpr_p = (named
		 && (type == 0 || FLOAT_TYPE_P (type))
		 && (GET_MODE_CLASS (mode) == MODE_FLOAT
		     || GET_MODE_CLASS (mode) == MODE_COMPLEX_FLOAT
		     || GET_MODE_CLASS (mode) == MODE_VECTOR_FLOAT)
		 && GET_MODE_UNIT_SIZE (mode) <= UNITS_PER_FPVALUE);

  /* Complex floats should only go into FPRs if there are two FPRs free,
     otherwise they should be passed in the same way as a struct
     containing two floats.  */
  if (info->fpr_p
      && GET_MODE_CLASS (mode) == MODE_COMPLEX_FLOAT
      && GET_MODE_UNIT_SIZE (mode) < UNITS_PER_FPVALUE)
    {
      if (cum->num_gprs >= MAX_ARGS_IN_REGISTERS - 1)
        info->fpr_p = false;
      else
        num_words = 2;
    }

  /* See whether the argument has doubleword alignment.  */
  doubleword_aligned_p = (riscv_function_arg_boundary (mode, type)
			  > BITS_PER_WORD);

  /* Set REG_OFFSET to the register count we're interested in.
     The EABI allocates the floating-point registers separately,
     but the other ABIs allocate them like integer registers.  */
  info->reg_offset = cum->num_gprs;

  /* Advance to an even register if the argument is doubleword-aligned.  */
  if (doubleword_aligned_p)
    info->reg_offset += info->reg_offset & 1;

  /* Work out the offset of a stack argument.  */
  info->stack_offset = cum->stack_words;
  if (doubleword_aligned_p)
    info->stack_offset += info->stack_offset & 1;

  max_regs = MAX_ARGS_IN_REGISTERS - info->reg_offset;

  /* Partition the argument between registers and stack.  */
  info->reg_words = MIN (num_words, max_regs);
  info->stack_words = num_words - info->reg_words;
}

/* INFO describes a register argument that has the normal format for the
   argument's mode.  Return the register it uses, assuming that FPRs are
   available if HARD_FLOAT_P.  */

static unsigned int
riscv_arg_regno (const struct riscv_arg_info *info, bool hard_float_p)
{
  if (!info->fpr_p || !hard_float_p)
    return GP_ARG_FIRST + info->reg_offset;
  else
    return FP_ARG_FIRST + info->reg_offset;
}

/* Implement TARGET_FUNCTION_ARG.  */

static rtx
riscv_function_arg (cumulative_args_t cum_v, enum machine_mode mode,
		    const_tree type, bool named)
{
  CUMULATIVE_ARGS *cum = get_cumulative_args (cum_v);
  struct riscv_arg_info info;

  if (mode == VOIDmode)
    return NULL;

  riscv_get_arg_info (&info, cum, mode, type, named);

  /* Return straight away if the whole argument is passed on the stack.  */
  if (info.reg_offset == MAX_ARGS_IN_REGISTERS)
    return NULL;

  /* The n32 and n64 ABIs say that if any 64-bit chunk of the structure
     contains a double in its entirety, then that 64-bit chunk is passed
     in a floating-point register.  */
  if (TARGET_HARD_FLOAT
      && named
      && type != 0
      && TREE_CODE (type) == RECORD_TYPE
      && TYPE_SIZE_UNIT (type)
      && tree_fits_uhwi_p (TYPE_SIZE_UNIT (type)))
    {
      tree field;

      /* First check to see if there is any such field.  */
      for (field = TYPE_FIELDS (type); field; field = DECL_CHAIN (field))
	if (TREE_CODE (field) == FIELD_DECL
	    && SCALAR_FLOAT_TYPE_P (TREE_TYPE (field))
	    && TYPE_PRECISION (TREE_TYPE (field)) == BITS_PER_WORD
	    && tree_fits_shwi_p (bit_position (field))
	    && int_bit_position (field) % BITS_PER_WORD == 0)
	  break;

      if (field != 0)
	{
	  /* Now handle the special case by returning a PARALLEL
	     indicating where each 64-bit chunk goes.  INFO.REG_WORDS
	     chunks are passed in registers.  */
	  unsigned int i;
	  HOST_WIDE_INT bitpos;
	  rtx ret;

	  /* assign_parms checks the mode of ENTRY_PARM, so we must
	     use the actual mode here.  */
	  ret = gen_rtx_PARALLEL (mode, rtvec_alloc (info.reg_words));

	  bitpos = 0;
	  field = TYPE_FIELDS (type);
	  for (i = 0; i < info.reg_words; i++)
	    {
	      rtx reg;

	      for (; field; field = DECL_CHAIN (field))
		if (TREE_CODE (field) == FIELD_DECL
		    && int_bit_position (field) >= bitpos)
		  break;

	      if (field
		  && int_bit_position (field) == bitpos
		  && SCALAR_FLOAT_TYPE_P (TREE_TYPE (field))
		  && TYPE_PRECISION (TREE_TYPE (field)) == BITS_PER_WORD)
		reg = gen_rtx_REG (DFmode, FP_ARG_FIRST + info.reg_offset + i);
	      else
		reg = gen_rtx_REG (DImode, GP_ARG_FIRST + info.reg_offset + i);

	      XVECEXP (ret, 0, i)
		= gen_rtx_EXPR_LIST (VOIDmode, reg,
				     GEN_INT (bitpos / BITS_PER_UNIT));

	      bitpos += BITS_PER_WORD;
	    }
	  return ret;
	}
    }

  /* Handle the n32/n64 conventions for passing complex floating-point
     arguments in FPR pairs.  The real part goes in the lower register
     and the imaginary part goes in the upper register.  */
  if (info.fpr_p
      && GET_MODE_CLASS (mode) == MODE_COMPLEX_FLOAT)
    {
      rtx real, imag;
      enum machine_mode inner;
      unsigned int regno;

      inner = GET_MODE_INNER (mode);
      regno = FP_ARG_FIRST + info.reg_offset;
      if (info.reg_words * UNITS_PER_WORD == GET_MODE_SIZE (inner))
	{
	  /* Real part in registers, imaginary part on stack.  */
	  gcc_assert (info.stack_words == info.reg_words);
	  return gen_rtx_REG (inner, regno);
	}
      else
	{
	  gcc_assert (info.stack_words == 0);
	  real = gen_rtx_EXPR_LIST (VOIDmode,
				    gen_rtx_REG (inner, regno),
				    const0_rtx);
	  imag = gen_rtx_EXPR_LIST (VOIDmode,
				    gen_rtx_REG (inner,
						 regno + info.reg_words / 2),
				    GEN_INT (GET_MODE_SIZE (inner)));
	  return gen_rtx_PARALLEL (mode, gen_rtvec (2, real, imag));
	}
    }

  return gen_rtx_REG (mode, riscv_arg_regno (&info, TARGET_HARD_FLOAT));
}

/* Implement TARGET_FUNCTION_ARG_ADVANCE.  */

static void
riscv_function_arg_advance (cumulative_args_t cum_v, enum machine_mode mode,
			    const_tree type, bool named)
{
  CUMULATIVE_ARGS *cum = get_cumulative_args (cum_v);
  struct riscv_arg_info info;

  riscv_get_arg_info (&info, cum, mode, type, named);

  /* Advance the register count.  This has the effect of setting
     num_gprs to MAX_ARGS_IN_REGISTERS if a doubleword-aligned
     argument required us to skip the final GPR and pass the whole
     argument on the stack.  */
  cum->num_gprs = info.reg_offset + info.reg_words;

  /* Advance the stack word count.  */
  if (info.stack_words > 0)
    cum->stack_words = info.stack_offset + info.stack_words;
}

/* Implement TARGET_ARG_PARTIAL_BYTES.  */

static int
riscv_arg_partial_bytes (cumulative_args_t cum,
			 enum machine_mode mode, tree type, bool named)
{
  struct riscv_arg_info info;

  riscv_get_arg_info (&info, get_cumulative_args (cum), mode, type, named);
  return info.stack_words > 0 ? info.reg_words * UNITS_PER_WORD : 0;
}

/* See whether VALTYPE is a record whose fields should be returned in
   floating-point registers.  If so, return the number of fields and
   list them in FIELDS (which should have two elements).  Return 0
   otherwise.

   For n32 & n64, a structure with one or two fields is returned in
   floating-point registers as long as every field has a floating-point
   type.  */

static int
riscv_fpr_return_fields (const_tree valtype, tree *fields)
{
  tree field;
  int i;

  if (TREE_CODE (valtype) != RECORD_TYPE)
    return 0;

  i = 0;
  for (field = TYPE_FIELDS (valtype); field != 0; field = DECL_CHAIN (field))
    {
      if (TREE_CODE (field) != FIELD_DECL)
	continue;

      if (!SCALAR_FLOAT_TYPE_P (TREE_TYPE (field)))
	return 0;

      if (i == 2)
	return 0;

      fields[i++] = field;
    }
  return i;
}

/* Return true if the function return value MODE will get returned in a
   floating-point register.  */

static bool
riscv_return_mode_in_fpr_p (enum machine_mode mode)
{
  return ((GET_MODE_CLASS (mode) == MODE_FLOAT
	   || GET_MODE_CLASS (mode) == MODE_VECTOR_FLOAT
	   || GET_MODE_CLASS (mode) == MODE_COMPLEX_FLOAT)
	  && GET_MODE_UNIT_SIZE (mode) <= UNITS_PER_HWFPVALUE);
}

/* Return the representation of an FPR return register when the
   value being returned in FP_RETURN has mode VALUE_MODE and the
   return type itself has mode TYPE_MODE.  On NewABI targets,
   the two modes may be different for structures like:

       struct __attribute__((packed)) foo { float f; }

   where we return the SFmode value of "f" in FP_RETURN, but where
   the structure itself has mode BLKmode.  */

static rtx
riscv_return_fpr_single (enum machine_mode type_mode,
			enum machine_mode value_mode)
{
  rtx x;

  x = gen_rtx_REG (value_mode, FP_RETURN);
  if (type_mode != value_mode)
    {
      x = gen_rtx_EXPR_LIST (VOIDmode, x, const0_rtx);
      x = gen_rtx_PARALLEL (type_mode, gen_rtvec (1, x));
    }
  return x;
}

/* Return a composite value in a pair of floating-point registers.
   MODE1 and OFFSET1 are the mode and byte offset for the first value,
   likewise MODE2 and OFFSET2 for the second.  MODE is the mode of the
   complete value.

   For n32 & n64, $f0 always holds the first value and $f2 the second.
   Otherwise the values are packed together as closely as possible.  */

static rtx
riscv_return_fpr_pair (enum machine_mode mode,
		      enum machine_mode mode1, HOST_WIDE_INT offset1,
		      enum machine_mode mode2, HOST_WIDE_INT offset2)
{
  return gen_rtx_PARALLEL
    (mode,
     gen_rtvec (2,
		gen_rtx_EXPR_LIST (VOIDmode,
				   gen_rtx_REG (mode1, FP_RETURN),
				   GEN_INT (offset1)),
		gen_rtx_EXPR_LIST (VOIDmode,
				   gen_rtx_REG (mode2, FP_RETURN + 1),
				   GEN_INT (offset2))));

}

/* Implement FUNCTION_VALUE and LIBCALL_VALUE.  For normal calls,
   VALTYPE is the return type and MODE is VOIDmode.  For libcalls,
   VALTYPE is null and MODE is the mode of the return value.  */

rtx
riscv_function_value (const_tree valtype, const_tree func, enum machine_mode mode)
{
  if (valtype)
    {
      tree fields[2];
      int unsigned_p;

      mode = TYPE_MODE (valtype);
      unsigned_p = TYPE_UNSIGNED (valtype);

      /* Since TARGET_PROMOTE_FUNCTION_MODE unconditionally promotes,
	 return values, promote the mode here too.  */
      mode = promote_function_mode (valtype, mode, &unsigned_p, func, 1);

      /* Handle structures whose fields are returned in $f0/$f2.  */
      switch (riscv_fpr_return_fields (valtype, fields))
	{
	case 1:
	  return riscv_return_fpr_single (mode,
					 TYPE_MODE (TREE_TYPE (fields[0])));

	case 2:
	  return riscv_return_fpr_pair (mode,
				       TYPE_MODE (TREE_TYPE (fields[0])),
				       int_byte_position (fields[0]),
				       TYPE_MODE (TREE_TYPE (fields[1])),
				       int_byte_position (fields[1]));
	}

      /* Only use FPRs for scalar, complex or vector types.  */
      if (!FLOAT_TYPE_P (valtype))
	return gen_rtx_REG (mode, GP_RETURN);
    }

  /* Handle long doubles for n32 & n64.  */
  if (mode == TFmode)
    return riscv_return_fpr_pair (mode,
    			     DImode, 0,
    			     DImode, GET_MODE_SIZE (mode) / 2);

  if (riscv_return_mode_in_fpr_p (mode))
    {
      if (GET_MODE_CLASS (mode) == MODE_COMPLEX_FLOAT)
        return riscv_return_fpr_pair (mode,
    				 GET_MODE_INNER (mode), 0,
    				 GET_MODE_INNER (mode),
    				 GET_MODE_SIZE (mode) / 2);
      else
        return gen_rtx_REG (mode, FP_RETURN);
    }

  return gen_rtx_REG (mode, GP_RETURN);
}

/* Implement TARGET_RETURN_IN_MEMORY.  Scalars and small structures
   that fit in two registers are returned in a0/a1. */

static bool
riscv_return_in_memory (const_tree type, const_tree fndecl ATTRIBUTE_UNUSED)
{
  return !IN_RANGE (int_size_in_bytes (type), 0, 2 * UNITS_PER_WORD);
}

/* Implement TARGET_PASS_BY_REFERENCE. */

static bool
riscv_pass_by_reference (cumulative_args_t cum ATTRIBUTE_UNUSED,
			 enum machine_mode mode, const_tree type,
			 bool named ATTRIBUTE_UNUSED)
{
  if (type && riscv_return_in_memory (type, NULL_TREE))
    return true;
  return targetm.calls.must_pass_in_stack (mode, type);
}

/* Implement TARGET_SETUP_INCOMING_VARARGS.  */

static void
riscv_setup_incoming_varargs (cumulative_args_t cum, enum machine_mode mode,
			     tree type, int *pretend_size ATTRIBUTE_UNUSED,
			     int no_rtl)
{
  CUMULATIVE_ARGS local_cum;
  int gp_saved;

  /* The caller has advanced CUM up to, but not beyond, the last named
     argument.  Advance a local copy of CUM past the last "real" named
     argument, to find out how many registers are left over.  */
  local_cum = *get_cumulative_args (cum);
  riscv_function_arg_advance (pack_cumulative_args (&local_cum), mode, type, 1);

  /* Found out how many registers we need to save.  */
  gp_saved = MAX_ARGS_IN_REGISTERS - local_cum.num_gprs;

  if (!no_rtl && gp_saved > 0)
    {
      rtx ptr, mem;

      ptr = plus_constant (Pmode, virtual_incoming_args_rtx,
			   REG_PARM_STACK_SPACE (cfun->decl)
			   - gp_saved * UNITS_PER_WORD);
      mem = gen_frame_mem (BLKmode, ptr);
      set_mem_alias_set (mem, get_varargs_alias_set ());

      move_block_from_reg (local_cum.num_gprs + GP_ARG_FIRST,
			   mem, gp_saved);
    }
  if (REG_PARM_STACK_SPACE (cfun->decl) == 0)
    cfun->machine->varargs_size = gp_saved * UNITS_PER_WORD;
}

/* Implement TARGET_EXPAND_BUILTIN_VA_START.  */

static void
riscv_va_start (tree valist, rtx nextarg)
{
  nextarg = plus_constant (Pmode, nextarg, -cfun->machine->varargs_size);
  std_expand_builtin_va_start (valist, nextarg);
}

/* Expand a call of type TYPE.  RESULT is where the result will go (null
   for "call"s and "sibcall"s), ADDR is the address of the function,
   ARGS_SIZE is the size of the arguments and AUX is the value passed
   to us by riscv_function_arg.  Return the call itself.  */

rtx
riscv_expand_call (bool sibcall_p, rtx result, rtx addr, rtx args_size)
{
  rtx pattern;

  if (!call_insn_operand (addr, VOIDmode))
    {
      rtx reg = RISCV_EPILOGUE_TEMP (Pmode);
      riscv_emit_move (reg, addr);
      addr = reg;
    }

  if (result == 0)
    {
      rtx (*fn) (rtx, rtx);

      if (sibcall_p)
	fn = gen_sibcall_internal;
      else
	fn = gen_call_internal;

      pattern = fn (addr, args_size);
    }
  else if (GET_CODE (result) == PARALLEL && XVECLEN (result, 0) == 2)
    {
      /* Handle return values created by riscv_return_fpr_pair.  */
      rtx (*fn) (rtx, rtx, rtx, rtx);
      rtx reg1, reg2;

      if (sibcall_p)
	fn = gen_sibcall_value_multiple_internal;
      else
	fn = gen_call_value_multiple_internal;

      reg1 = XEXP (XVECEXP (result, 0, 0), 0);
      reg2 = XEXP (XVECEXP (result, 0, 1), 0);
      pattern = fn (reg1, addr, args_size, reg2);
    }
  else
    {
      rtx (*fn) (rtx, rtx, rtx);

      if (sibcall_p)
	fn = gen_sibcall_value_internal;
      else
	fn = gen_call_value_internal;

      /* Handle return values created by riscv_return_fpr_single.  */
      if (GET_CODE (result) == PARALLEL && XVECLEN (result, 0) == 1)
	result = XEXP (XVECEXP (result, 0, 0), 0);
      pattern = fn (result, addr, args_size);
    }

  return emit_call_insn (pattern);
}

/* Emit straight-line code to move LENGTH bytes from SRC to DEST.
   Assume that the areas do not overlap.  */

static void
riscv_block_move_straight (rtx dest, rtx src, HOST_WIDE_INT length)
{
  HOST_WIDE_INT offset, delta;
  unsigned HOST_WIDE_INT bits;
  int i;
  enum machine_mode mode;
  rtx *regs;

  bits = MAX( BITS_PER_UNIT,
             MIN( BITS_PER_WORD, MIN( MEM_ALIGN(src),MEM_ALIGN(dest) ) ) );

  mode = mode_for_size (bits, MODE_INT, 0);
  delta = bits / BITS_PER_UNIT;

  /* Allocate a buffer for the temporary registers.  */
  regs = XALLOCAVEC (rtx, length / delta);

  /* Load as many BITS-sized chunks as possible.  Use a normal load if
     the source has enough alignment, otherwise use left/right pairs.  */
  for (offset = 0, i = 0; offset + delta <= length; offset += delta, i++)
    {
      regs[i] = gen_reg_rtx (mode);
	riscv_emit_move (regs[i], adjust_address (src, mode, offset));
    }

  /* Copy the chunks to the destination.  */
  for (offset = 0, i = 0; offset + delta <= length; offset += delta, i++)
      riscv_emit_move (adjust_address (dest, mode, offset), regs[i]);

  /* Mop up any left-over bytes.  */
  if (offset < length)
    {
      src = adjust_address (src, BLKmode, offset);
      dest = adjust_address (dest, BLKmode, offset);
      move_by_pieces (dest, src, length - offset,
		      MIN (MEM_ALIGN (src), MEM_ALIGN (dest)), 0);
    }
}

/* Helper function for doing a loop-based block operation on memory
   reference MEM.  Each iteration of the loop will operate on LENGTH
   bytes of MEM.

   Create a new base register for use within the loop and point it to
   the start of MEM.  Create a new memory reference that uses this
   register.  Store them in *LOOP_REG and *LOOP_MEM respectively.  */

static void
riscv_adjust_block_mem (rtx mem, HOST_WIDE_INT length,
		       rtx *loop_reg, rtx *loop_mem)
{
  *loop_reg = copy_addr_to_reg (XEXP (mem, 0));

  /* Although the new mem does not refer to a known location,
     it does keep up to LENGTH bytes of alignment.  */
  *loop_mem = change_address (mem, BLKmode, *loop_reg);
  set_mem_align (*loop_mem, MIN (MEM_ALIGN (mem), length * BITS_PER_UNIT));
}

/* Move LENGTH bytes from SRC to DEST using a loop that moves BYTES_PER_ITER
   bytes at a time.  LENGTH must be at least BYTES_PER_ITER.  Assume that
   the memory regions do not overlap.  */

static void
riscv_block_move_loop (rtx dest, rtx src, HOST_WIDE_INT length,
		      HOST_WIDE_INT bytes_per_iter)
{
  rtx label, src_reg, dest_reg, final_src, test;
  HOST_WIDE_INT leftover;

  leftover = length % bytes_per_iter;
  length -= leftover;

  /* Create registers and memory references for use within the loop.  */
  riscv_adjust_block_mem (src, bytes_per_iter, &src_reg, &src);
  riscv_adjust_block_mem (dest, bytes_per_iter, &dest_reg, &dest);

  /* Calculate the value that SRC_REG should have after the last iteration
     of the loop.  */
  final_src = expand_simple_binop (Pmode, PLUS, src_reg, GEN_INT (length),
				   0, 0, OPTAB_WIDEN);

  /* Emit the start of the loop.  */
  label = gen_label_rtx ();
  emit_label (label);

  /* Emit the loop body.  */
  riscv_block_move_straight (dest, src, bytes_per_iter);

  /* Move on to the next block.  */
  riscv_emit_move (src_reg, plus_constant (Pmode, src_reg, bytes_per_iter));
  riscv_emit_move (dest_reg, plus_constant (Pmode, dest_reg, bytes_per_iter));

  /* Emit the loop condition.  */
  test = gen_rtx_NE (VOIDmode, src_reg, final_src);
  if (Pmode == DImode)
    emit_jump_insn (gen_cbranchdi4 (test, src_reg, final_src, label));
  else
    emit_jump_insn (gen_cbranchsi4 (test, src_reg, final_src, label));

  /* Mop up any left-over bytes.  */
  if (leftover)
    riscv_block_move_straight (dest, src, leftover);
}

/* Expand a movmemsi instruction, which copies LENGTH bytes from
   memory reference SRC to memory reference DEST.  */

bool
riscv_expand_block_move (rtx dest, rtx src, rtx length)
{
  if (CONST_INT_P (length))
    {
      HOST_WIDE_INT factor, align;
      
      align = MIN (MIN (MEM_ALIGN (src), MEM_ALIGN (dest)), BITS_PER_WORD);
      factor = BITS_PER_WORD / align;

      if (INTVAL (length) <= RISCV_MAX_MOVE_BYTES_STRAIGHT / factor)
	{
	  riscv_block_move_straight (dest, src, INTVAL (length));
	  return true;
	}
      else if (optimize && align >= BITS_PER_WORD)
	{
	  riscv_block_move_loop (dest, src, INTVAL (length),
				RISCV_MAX_MOVE_BYTES_PER_LOOP_ITER / factor);
	  return true;
	}
    }
  return false;
}

static const struct attribute_spec riscv_attribute_table[] =
{
  /* { name, min_len, max_len, decl_req, type_req, fn_type_req, handler } */
  { "interrupt",      0, 0, false, true,  true,  NULL, true  },
  { "tiny",           0, 0, true,  false, false, NULL, true  },
  { "import",         0, 0, false, true,  true,  NULL, true  },
  { "import_var",     0, 0, true,  false, false, NULL, true  },
  { "export",         0, 0, false, true,  true,  NULL, true  },
  { "export_var",     0, 0, true,  false, false, NULL, true  },
  { NULL,             0, 0, false, false, false, NULL, false }
};

#undef TARGET_ATTRIBUTE_TABLE
#define TARGET_ATTRIBUTE_TABLE riscv_attribute_table

/* (Re-)Initialize riscv_lo_relocs and riscv_hi_relocs.  */

static void
riscv_init_relocs (void)
{
  memset (riscv_hi_relocs, '\0', sizeof (riscv_hi_relocs));
  memset (riscv_lo_relocs, '\0', sizeof (riscv_lo_relocs));

  if (!flag_pic && riscv_cmodel == CM_MEDLOW)
    {
      riscv_hi_relocs[SYMBOL_ABSOLUTE] = "%hi(";
      riscv_lo_relocs[SYMBOL_ABSOLUTE] = "%lo(";
    }

  if (!flag_pic || flag_pie)
    {
      riscv_hi_relocs[SYMBOL_TLS_LE] = "%tprel_hi(";
      riscv_lo_relocs[SYMBOL_TLS_LE] = "%tprel_lo(";
    }
}

bool
riscv_is_tiny_symbol_p (rtx addr)
{
  tree decl;
  tree attrs;

  if (GET_CODE(addr) != SYMBOL_REF) return false;
  decl = SYMBOL_REF_DECL(addr);
  if (decl) {
     attrs = DECL_ATTRIBUTES (decl);
     if ((attrs && lookup_attribute ("tiny", attrs))) return true;
  }
  return false;
}


/* Print symbolic operand OP, which is part of a HIGH or LO_SUM
   in context CONTEXT.  RELOCS is the array of relocations to use.  */

static void
riscv_print_operand_reloc (FILE *file, rtx op, const char **relocs)
{
  enum riscv_symbol_type symbol_type;
  const char *p;

  symbol_type = riscv_classify_symbolic_expression (op);
  gcc_assert (relocs[symbol_type]);

  fputs (relocs[symbol_type], file);
  output_addr_const (file, riscv_strip_unspec_address (op));
  for (p = relocs[symbol_type]; *p != 0; p++)
    if (*p == '(')
      fputc (')', file);
}

static const char *
riscv_memory_model_suffix (enum memmodel model)
{
  switch (model)
    {
      case MEMMODEL_ACQ_REL:
      case MEMMODEL_SEQ_CST:
	return ".sc";
      case MEMMODEL_ACQUIRE:
      case MEMMODEL_CONSUME:
	return ".aq";
      case MEMMODEL_RELEASE:
	return ".rl";
      case MEMMODEL_RELAXED:
	return "";
      default: gcc_unreachable();
    }
}

/* Implement TARGET_PRINT_OPERAND.  The RISCV-specific operand codes are:

   'h'	Print the high-part relocation associated with OP, after stripping
	  any outermost HIGH.
   'R'	Print the low-part relocation associated with OP.
   'C'	Print the integer branch condition for comparison OP.
   'A'	Print the atomic operation suffix for memory model OP.
   'z'	Print $0 if OP is zero, otherwise print OP normally.  */

static void
riscv_print_operand (FILE *file, rtx op, int letter)
{
  enum rtx_code code;

  gcc_assert (op);
  code = GET_CODE (op);

  switch (letter)
    {
    case 'h':
      if (code == HIGH)
	op = XEXP (op, 0);
      riscv_print_operand_reloc (file, op, riscv_hi_relocs);
      break;

    case 'R':
      riscv_print_operand_reloc (file, op, riscv_lo_relocs);
      break;

    case 'C':
      /* The RTL names match the instruction names. */
      fputs (GET_RTX_NAME (code), file);
      break;

    case 'A':
      fputs (riscv_memory_model_suffix ((enum memmodel)INTVAL (op)), file);
      break;
    case 'B':
      fprintf (file, "%d", riscv_bit_size_for_clip (INTVAL (op)));
      break;
    case 'W':
      {
         enum machine_mode inner_mode = GET_MODE_INNER (GET_MODE(op));
         HOST_WIDE_INT mask = GET_MODE_MASK (inner_mode);
         HOST_WIDE_INT val = INTVAL (CONST_VECTOR_ELT (op, 0)) & mask;
         if (val_signbit_known_set_p(inner_mode, val))
                 val |= ~GET_MODE_MASK (inner_mode);
         fprintf (file, "%d", (int) val);
      }
      break;
    case 'w':
      {
         enum machine_mode inner_mode = GET_MODE_INNER (GET_MODE(op));
         HOST_WIDE_INT mask = GET_MODE_MASK (inner_mode);
         HOST_WIDE_INT val = INTVAL (CONST_VECTOR_ELT (op, 0)) & mask;
         if (val_signbit_known_set_p(inner_mode, val)) // ????
                 val |= ~GET_MODE_MASK (inner_mode);
         fprintf (file, "%d", (int) val);
      }
      break;
    case 'V':
      {
	enum machine_mode inner_mode = GET_MODE_INNER (GET_MODE(op));
        HOST_WIDE_INT mask = GET_MODE_MASK (inner_mode);
	int i;
	int Val=0;
	int Off = (GET_MODE_NUNITS (GET_MODE(op)) == 2)?16:8;
	for (i = 0; i < GET_MODE_NUNITS (GET_MODE(op)); i++) {
		HOST_WIDE_INT cval = INTVAL (CONST_VECTOR_ELT (op, i)) & mask;
		Val = Val | ((int) cval << Off*i);
	}
        fprintf (file, "%d", (int) Val);
        break;
      }
    default:
      switch (code)
	{
	case REG:
	  if (letter && letter != 'z')
	    output_operand_lossage ("invalid use of '%%%c'", letter);
	  fprintf (file, "%s", reg_names[REGNO (op)]);
	  break;

	case MEM:
	  if (letter == 'y') {
	    fprintf (file, "%s", reg_names[REGNO(XEXP(op, 0))]);
	  } else if (letter && letter != 'z') {
	    output_operand_lossage ("invalid use of '%%%c'", letter);
	  } else {
	    output_address (XEXP (op, 0));
	  }
/*
	  if (MEM_ALIGN(op) < GET_MODE_ALIGNMENT(GET_MODE(op))) {
		fprintf(file, " UUU ");
	  } else fprintf(file, " AAA ");
*/	  
	  break;

	default:
	  if (letter == 'z' && op == CONST0_RTX (GET_MODE (op)))
	    fputs (reg_names[GP_REG_FIRST], file);
	  else if (letter && letter != 'z')
	    output_operand_lossage ("invalid use of '%%%c'", letter);
	  else
	    output_addr_const (file, riscv_strip_unspec_address (op));
	  break;
	}
    }
}

int ModeSize(enum machine_mode mode)

{
   switch (mode) {
      case QImode: return 1;
      case HImode: return 2;
      case SImode: return 4;
      default: return 0;
   }
}


/* Implement TARGET_PRINT_OPERAND_ADDRESS.  */

static void
riscv_print_operand_address (FILE *file, rtx x)
{
  struct riscv_address_info addr;

  if (riscv_classify_address (&addr, x, word_mode, true))
    switch (addr.type)
      {
      case ADDRESS_REG_TINY_SYMBOL:
	fprintf (file, "%%tiny(");
	riscv_print_operand (file, addr.offset, 0);
	fprintf (file, ")");
	fprintf (file, "(%s)", reg_names[REGNO (addr.reg)]);
	return;
      case ADDRESS_REG_REG:
      case ADDRESS_REG:
	riscv_print_operand (file, addr.offset, 0);
	fprintf (file, "(%s)", reg_names[REGNO (addr.reg)]);
	return;

      case ADDRESS_LO_SUM:
	riscv_print_operand_reloc (file, addr.offset, riscv_lo_relocs);
	fprintf (file, "(%s)", reg_names[REGNO (addr.reg)]);
	return;

      case ADDRESS_CONST_INT:
	output_addr_const (file, x);
	fprintf (file, "(%s)", reg_names[GP_REG_FIRST]);
	return;

      case ADDRESS_SYMBOLIC:
	output_addr_const (file, riscv_strip_unspec_address (x));
	return;

      case ADDRESS_TINY_SYMBOL:
        output_addr_const (file, riscv_strip_unspec_address (x));
        return;

      case ADDRESS_REG_POST_INC:
	// riscv_print_operand (file, addr.offset, 0);
	fprintf (file, "%d(%s!)", ModeSize(addr.mode), reg_names[REGNO (addr.reg)]);
	return;

      case ADDRESS_REG_POST_DEC:
	// riscv_print_operand (file, addr.offset, 0);
	fprintf (file, "%d(%s!)", ModeSize(addr.mode), reg_names[REGNO (addr.reg)]);
	return;

      case ADDRESS_REG_POST_MODIFY:
	riscv_print_operand (file, addr.offset, 0);
	fprintf (file, "(%s!)", reg_names[REGNO (addr.reg)]);
	return;
      }
  gcc_unreachable ();
}

static bool
riscv_size_ok_for_small_data_p (int size)
{
  return g_switch_value && IN_RANGE (size, 1, g_switch_value);
}

/* Return true if EXP should be placed in the small data section. */

static bool
riscv_in_small_data_p (const_tree x)
{
  if (TREE_CODE (x) == STRING_CST || TREE_CODE (x) == FUNCTION_DECL)
    return false;

  if (TREE_CODE (x) == VAR_DECL && DECL_SECTION_NAME (x))
    {
      const char *sec = DECL_SECTION_NAME (x);
      return strcmp (sec, ".sdata") == 0 || strcmp (sec, ".sbss") == 0;
    }

  return riscv_size_ok_for_small_data_p (int_size_in_bytes (TREE_TYPE (x)));
}

static bool
riscv_use_anchors_for_symbol_p (const_rtx symbol)
{
      if (riscv_is_tiny_symbol_p((rtx) symbol)) return false;
      return default_use_anchors_for_symbol_p (symbol);
}

/* Return a section for X, handling small data. */

static section *
riscv_elf_select_rtx_section (enum machine_mode mode, rtx x,
			      unsigned HOST_WIDE_INT align)
{
  section *s = default_elf_select_rtx_section (mode, x, align);

  if (riscv_size_ok_for_small_data_p (GET_MODE_SIZE (mode)))
    {
      if (strncmp (s->named.name, ".rodata.cst", strlen (".rodata.cst")) == 0)
	{
	  /* Rename .rodata.cst* to .srodata.cst*. */
	  char *name = (char *) alloca (strlen (s->named.name) + 2);
	  sprintf (name, ".s%s", s->named.name + 1);
	  return get_section (name, s->named.common.flags, NULL);
	}

      if (s == data_section)
	return sdata_section;
    }

  return s;
}

/* Implement TARGET_ASM_OUTPUT_DWARF_DTPREL.  */

static void ATTRIBUTE_UNUSED
riscv_output_dwarf_dtprel (FILE *file, int size, rtx x)
{
  switch (size)
    {
    case 4:
      fputs ("\t.dtprelword\t", file);
      break;

    case 8:
      fputs ("\t.dtpreldword\t", file);
      break;

    default:
      gcc_unreachable ();
    }
  output_addr_const (file, x);
  fputs ("+0x800", file);
}

/* Make the last instruction frame-related and note that it performs
   the operation described by FRAME_PATTERN.  */

static void
riscv_set_frame_expr (rtx frame_pattern)
{
  rtx insn;

  insn = get_last_insn ();
  RTX_FRAME_RELATED_P (insn) = 1;
  REG_NOTES (insn) = alloc_EXPR_LIST (REG_FRAME_RELATED_EXPR,
				      frame_pattern,
				      REG_NOTES (insn));
}

/* Return a frame-related rtx that stores REG at MEM.
   REG must be a single register.  */

static rtx
riscv_frame_set (rtx mem, rtx reg)
{
  rtx set;

  set = gen_rtx_SET (VOIDmode, mem, reg);
  RTX_FRAME_RELATED_P (set) = 1;

  return set;
}

static int scan_reg_definitions(int regno)

{
        struct df_reg_info *reg_info = DF_REG_DEF_GET(regno);
        df_ref chain;
        int cnt = 0;

        if (!reg_info || reg_info->n_refs==0) return 0;

        chain = reg_info->reg_chain;

        while (chain) {
                if (chain->base.cl == DF_REF_ARTIFICIAL) {
                } else if (chain->base.cl == DF_REF_REGULAR) {
                        cnt++;
                } else { // DF_REF_BASE
                }
                chain = chain->base.next_reg;
        }
        return cnt;

}

/* Return true if the current function must save register REGNO.  */

static bool
riscv_save_reg_p (unsigned int regno, bool is_it)
{
  bool call_saved = !global_regs[regno] && !call_really_used_regs[regno];
  bool might_clobber = crtl->saves_all_registers
		       || df_regs_ever_live_p (regno)
		       || (regno == HARD_FRAME_POINTER_REGNUM
			   && frame_pointer_needed);
  bool it_rel = is_it && df_regs_ever_live_p(regno) && scan_reg_definitions(regno);

  return (call_saved && might_clobber)
	 || (regno == RETURN_ADDR_REGNUM && crtl->calls_eh_return)
         || it_rel;
}

/* Determine whether to call GPR save/restore routines.  */
static bool
riscv_use_save_libcall (const struct riscv_frame_info *frame)
{
  if (!TARGET_SAVE_RESTORE || crtl->calls_eh_return || frame_pointer_needed || frame->is_it)
    return false;

  return frame->save_libcall_adjustment != 0;
}

/* Determine which GPR save/restore routine to call.  */

static unsigned
riscv_save_libcall_count (unsigned mask)
{
  for (unsigned n = GP_REG_LAST; n > GP_REG_FIRST; n--)
    if (BITSET_P (mask, n))
      return CALLEE_SAVED_REG_NUMBER (n) + 1;
  abort ();
}

/* Populate the current function's riscv_frame_info structure.

   RISC-V stack frames grown downward.  High addresses are at the top.

	+-------------------------------+
	|                               |
	|  incoming stack arguments     |
	|                               |
	+-------------------------------+ <-- incoming stack pointer
	|                               |
	|  callee-allocated save area   |
	|  for arguments that are       |
	|  split between registers and  |
	|  the stack                    |
	|                               |
	+-------------------------------+ <-- arg_pointer_rtx
	|                               |
	|  callee-allocated save area   |
	|  for register varargs         |
	|                               |
	+-------------------------------+ <-- hard_frame_pointer_rtx;
	|                               |     stack_pointer_rtx + gp_sp_offset
	|  GPR save area                |       + UNITS_PER_WORD
	|                               |
	+-------------------------------+ <-- stack_pointer_rtx + fp_sp_offset
	|                               |       + UNITS_PER_HWVALUE
	|  FPR save area                |
	|                               |
	+-------------------------------+ <-- frame_pointer_rtx (virtual)
	|                               |
	|  local variables              |
	|                               |
      P +-------------------------------+
	|                               |
	|  outgoing stack arguments     |
	|                               |
	+-------------------------------+ <-- stack_pointer_rtx

   Dynamic stack allocations such as alloca insert data at point P.
   They decrease stack_pointer_rtx but leave frame_pointer_rtx and
   hard_frame_pointer_rtx unchanged.  */

static void
riscv_compute_frame_info (void)
{
  struct riscv_frame_info *frame;
  HOST_WIDE_INT offset;
  unsigned int regno, i, num_x_saved = 0, num_f_saved = 0;
  static bool Trace=false;

  frame = &cfun->machine->frame;
  memset (frame, 0, sizeof (*frame));
  frame->is_it = cfun->machine->is_interrupt;
  if (Trace) fprintf(stderr, "- %30s ----FRAME INFOS---------------------\n", current_function_name());

  if (Trace) fprintf(stderr, "Setting up frame info, is_it: %s\n", frame->is_it?"Yes":"No");
  /* Find out which GPRs we need to save.  */
  for (regno = GP_REG_FIRST; regno <= GP_REG_LAST; regno++)
    if (riscv_save_reg_p (regno, frame->is_it)) {
      frame->mask |= 1 << (regno - GP_REG_FIRST), num_x_saved++;
      if (Trace)
                fprintf(stderr, "\tGGGr%3d [%5s]: Lives:%3s, Call Used: %3s, Call Really Used:%3s, Invalidated:%3s, Def Cnt=%3d\n",
                                regno, reg_names[regno], df_regs_ever_live_p(regno)?"Yes":"No", call_used_regs[regno]?"Yes":"No",
                                call_really_used_regs[regno]?"Yes":"No",
                                TEST_HARD_REG_BIT (regs_invalidated_by_call, regno)?"Yes":"No",
                                scan_reg_definitions(regno));

    }

  /* If this function calls eh_return, we must also save and restore the
     EH data registers.  */
  if (crtl->calls_eh_return)
    for (i = 0; (regno = EH_RETURN_DATA_REGNO (i)) != INVALID_REGNUM; i++) {
      frame->mask |= 1 << (regno - GP_REG_FIRST), num_x_saved++;
      if (Trace)
                fprintf(stderr, "\tHHHr%3d [%5s]: Lives:%3s, Call Used: %3s, Call Really Used:%3s, Invalidated:%3s, Def Cnt=%3d\n",
                                regno, reg_names[regno], df_regs_ever_live_p(regno)?"Yes":"No", call_used_regs[regno]?"Yes":"No",
                                call_really_used_regs[regno]?"Yes":"No",
                                TEST_HARD_REG_BIT (regs_invalidated_by_call, regno)?"Yes":"No",
                                scan_reg_definitions(regno));
    }

  /* Find out which FPRs we need to save.  This loop must iterate over
     the same space as its companion in riscv_for_each_saved_gpr_and_fpr.  */
  if (TARGET_HARD_FLOAT)
    for (regno = FP_REG_FIRST; regno <= FP_REG_LAST; regno++)
      if (riscv_save_reg_p (regno, frame->is_it)) {
        frame->fmask |= 1 << (regno - FP_REG_FIRST), num_f_saved++;
      if (Trace)
                fprintf(stderr, "\tFFFr%3d [%5s]: Lives:%3s, Call Used: %3s, Call Really Used:%3s, Invalidated:%3s, Def Cnt=%3d\n",
                                regno, reg_names[regno], df_regs_ever_live_p(regno)?"Yes":"No", call_used_regs[regno]?"Yes":"No",
                                call_really_used_regs[regno]?"Yes":"No",
                                TEST_HARD_REG_BIT (regs_invalidated_by_call, regno)?"Yes":"No",
                                scan_reg_definitions(regno));
      }

  /* At the bottom of the frame are any outgoing stack arguments. */
  offset = crtl->outgoing_args_size; 
  if (Trace) fprintf(stderr, "Outgoing Args: %d\n", (int) offset);
  /* Next are local stack variables. */
  offset += RISCV_STACK_ALIGN (get_frame_size ());
  /* The virtual frame pointer points above the local variables. */
  frame->frame_pointer_offset = offset;
  if (Trace) fprintf(stderr, "After align, FP off: %d\n", (int) offset);
  /* Next are the callee-saved FPRs. */
  if (frame->fmask)
    {
      offset += RISCV_STACK_ALIGN (num_f_saved * UNITS_PER_FPREG);
      frame->fp_sp_offset = offset - UNITS_PER_HWFPVALUE;
    }
  /* Next are the callee-saved GPRs. */
  if (frame->mask)
    {
      unsigned x_save_size = RISCV_STACK_ALIGN (num_x_saved * UNITS_PER_WORD);
      unsigned num_save_restore = 1 + riscv_save_libcall_count (frame->mask);

      /* Only use save/restore routines if they don't alter the stack size.  */
      if (RISCV_STACK_ALIGN (num_save_restore * UNITS_PER_WORD) == x_save_size) {
        if (!frame->is_it) {
	  frame->save_libcall_adjustment = x_save_size;
          if (Trace) fprintf(stderr, "Can use runtime save, save_libcall_adjustment: %d\n", x_save_size);
        } else {
          if (Trace) fprintf(stderr, "Could use runtime save but dropped since we are in an it handler\n");
	}
      }

      offset += x_save_size;
      frame->gp_sp_offset = offset - UNITS_PER_WORD;
      if (Trace) fprintf(stderr, "gp_sp_offset: %d\n", (int) frame->gp_sp_offset);
    }
  /* The hard frame pointer points above the callee-saved GPRs. */
  frame->hard_frame_pointer_offset = offset;
  if (Trace) fprintf(stderr, "hard_frame_pointer_offset: %d\n", (int) frame->hard_frame_pointer_offset);
  /* Above the hard frame pointer is the callee-allocated varags save area. */
  offset += RISCV_STACK_ALIGN (cfun->machine->varargs_size);
  frame->arg_pointer_offset = offset;
  if (Trace) fprintf(stderr, "arg_pointer_offset: %d\n", (int) frame->arg_pointer_offset);
  /* Next is the callee-allocated area for pretend stack arguments.  */
  offset += crtl->args.pretend_args_size;
  frame->total_size = offset;
  if (Trace) fprintf(stderr, "Adding pretends arg: %d\n", (int) crtl->args.pretend_args_size);
  if (Trace) fprintf(stderr, "Final frame size: %d\n", (int) offset);
  /* Next points the incoming stack pointer and any incoming arguments. */

  /* Only use save/restore routines when the GPRs are atop the frame.  */
  if (frame->hard_frame_pointer_offset != frame->total_size) {
    if (Trace) fprintf(stderr, "hard frame pt offset != Final frame size, => Disabling runtime save \n");
    frame->save_libcall_adjustment = 0;
  }
  if (Trace) fprintf(stderr, "---------------------------------------------------------------------------\n");
}

/* Make sure that we're not trying to eliminate to the wrong hard frame
   pointer.  */

static bool
riscv_can_eliminate (const int from ATTRIBUTE_UNUSED, const int to)
{
  return (to == HARD_FRAME_POINTER_REGNUM || to == STACK_POINTER_REGNUM);
}

/* Implement INITIAL_ELIMINATION_OFFSET.  FROM is either the frame pointer
   or argument pointer.  TO is either the stack pointer or hard frame
   pointer.  */

HOST_WIDE_INT
riscv_initial_elimination_offset (int from, int to)
{
  HOST_WIDE_INT src, dest;

  riscv_compute_frame_info ();

  if (to == HARD_FRAME_POINTER_REGNUM)
    dest = cfun->machine->frame.hard_frame_pointer_offset;
  else if (to == STACK_POINTER_REGNUM)
    dest = 0; /* this is the base of all offsets */
  else
    gcc_unreachable ();

  if (from == FRAME_POINTER_REGNUM)
    src = cfun->machine->frame.frame_pointer_offset;
  else if (from == ARG_POINTER_REGNUM)
    src = cfun->machine->frame.arg_pointer_offset;
  else
    gcc_unreachable ();

  return src - dest;
}

/* Implement RETURN_ADDR_RTX.  We do not support moving back to a
   previous frame.  */

rtx
riscv_return_addr (int count, rtx frame ATTRIBUTE_UNUSED)
{
  if (count != 0)
    return const0_rtx;

  return get_hard_reg_initial_val (Pmode, RETURN_ADDR_REGNUM);
}

/* Emit code to change the current function's return address to
   ADDRESS.  SCRATCH is available as a scratch register, if needed.
   ADDRESS and SCRATCH are both word-mode GPRs.  */

void
riscv_set_return_address (rtx address, rtx scratch)
{
  rtx slot_address;

  gcc_assert (BITSET_P (cfun->machine->frame.mask, RETURN_ADDR_REGNUM));
  slot_address = riscv_add_offset (scratch, stack_pointer_rtx,
				  cfun->machine->frame.gp_sp_offset);
  riscv_emit_move (gen_frame_mem (GET_MODE (address), slot_address), address);
}

/* A function to save or store a register.  The first argument is the
   register and the second is the stack slot.  */
typedef void (*riscv_save_restore_fn) (rtx, rtx);

/* Use FN to save or restore register REGNO.  MODE is the register's
   mode and OFFSET is the offset of its save slot from the current
   stack pointer.  */

static void
riscv_save_restore_reg (enum machine_mode mode, int regno,
		       HOST_WIDE_INT offset, riscv_save_restore_fn fn)
{
  rtx mem;

  mem = gen_frame_mem (mode, plus_constant (Pmode, stack_pointer_rtx, offset));
  fn (gen_rtx_REG (mode, regno), mem);
}

/* Call FN for each register that is saved by the current function.
   SP_OFFSET is the offset of the current stack pointer from the start
   of the frame.  */

static void
riscv_for_each_saved_gpr_and_fpr (HOST_WIDE_INT sp_offset,
				 riscv_save_restore_fn fn)
{
  HOST_WIDE_INT offset;
  int regno;

  /* Save the link register and s-registers. */
  offset = cfun->machine->frame.gp_sp_offset - sp_offset;
  for (regno = GP_REG_FIRST; regno <= GP_REG_LAST-1; regno++)
    if (BITSET_P (cfun->machine->frame.mask, regno - GP_REG_FIRST))
      {
        riscv_save_restore_reg (word_mode, regno, offset, fn);
        offset -= UNITS_PER_WORD;
      }

  /* This loop must iterate over the same space as its companion in
     riscv_compute_frame_info.  */
  offset = cfun->machine->frame.fp_sp_offset - sp_offset;
  for (regno = FP_REG_FIRST; regno <= FP_REG_LAST; regno++)
    if (BITSET_P (cfun->machine->frame.fmask, regno - FP_REG_FIRST))
      {
	riscv_save_restore_reg (DFmode, regno, offset, fn);
	offset -= GET_MODE_SIZE (DFmode);
      }
}

/* Emit a move from SRC to DEST, given that one of them is a register
   save slot and that the other is a register.  TEMP is a temporary
   GPR of the same mode that is available if need be.  */

static void
riscv_emit_save_slot_move (rtx dest, rtx src, rtx temp)
{
  unsigned int regno;
  rtx mem;
  enum reg_class rclass;

  if (REG_P (src))
    {
      regno = REGNO (src);
      mem = dest;
    }
  else
    {
      regno = REGNO (dest);
      mem = src;
    }

  rclass = riscv_secondary_reload_class (REGNO_REG_CLASS (regno),
					 GET_MODE (mem), mem, mem == src);

  if (rclass == NO_REGS)
    riscv_emit_move (dest, src);
  else
    {
      gcc_assert (!reg_overlap_mentioned_p (dest, temp));
      riscv_emit_move (temp, src);
      riscv_emit_move (dest, temp);
    }
  if (MEM_P (dest))
    riscv_set_frame_expr (riscv_frame_set (dest, src));
}

/* Save register REG to MEM.  Make the instruction frame-related.  */

static void
riscv_save_reg (rtx reg, rtx mem)
{
  riscv_emit_save_slot_move (mem, reg, RISCV_PROLOGUE_TEMP (GET_MODE (reg)));
}

/* Return the code to invoke the GPR save routine.  */

const char *
riscv_output_gpr_save (unsigned mask)
{
  static char buf[GP_REG_NUM * 32];
  size_t len = 0;
  unsigned n = riscv_save_libcall_count (mask), i;
  unsigned frame_size = RISCV_STACK_ALIGN ((n + 1) * UNITS_PER_WORD);

  len += sprintf (buf + len, "call\tt0,__riscv_save_%u", n);

#ifdef DWARF2_UNWIND_INFO
  /* Describe the effect of the call to __riscv_save_X.  */
  if (dwarf2out_do_cfi_asm ())
    {
      len += sprintf (buf + len, "\n\t.cfi_def_cfa_offset %u", frame_size);

      for (i = GP_REG_FIRST; i <= GP_REG_LAST; i++)
	if (BITSET_P (cfun->machine->frame.mask, i))
	  len += sprintf (buf + len, "\n\t.cfi_offset %u,%d", i,
			  (CALLEE_SAVED_REG_NUMBER (i) + 2) * -UNITS_PER_WORD);
    }
#endif

  return buf;
}

/* Expand the "prologue" pattern.  */

void
riscv_expand_prologue (void)
{
  struct riscv_frame_info *frame = &cfun->machine->frame;
  HOST_WIDE_INT size = frame->total_size;
  unsigned mask = frame->mask;
  rtx insn;
  static bool Trace=false;

  if (Trace) {
        fprintf(stderr, "\n");
        fprintf(stderr, "- %30s ----Expand Prologue ---------------\n", current_function_name());

        fprintf(stderr, "\n");
	fprintf(stderr, "Total Frame Size = %d\n", (int) size);
  }

  if (flag_stack_usage_info)
    current_function_static_stack_size = size;

  /* When optimizing for size, call a subroutine to save the registers.  */
  if (riscv_use_save_libcall (frame))
    {
      if (Trace) fprintf(stderr, "Using runtime call to save registers\n");
      frame->mask = 0; /* Temporarily fib that we need not save GPRs.  */
      size -= frame->save_libcall_adjustment;
      emit_insn (gen_gpr_save (GEN_INT (mask)));
    }

  /* Save the registers.  Allocate up to RISCV_MAX_FIRST_STACK_STEP
     bytes beforehand; this is enough to cover the register save area
     without going out of range.  */
  if ((frame->mask | frame->fmask) != 0)
    {
      HOST_WIDE_INT step1;

      step1 = MIN (size, RISCV_MAX_FIRST_STACK_STEP);
      if (Trace) fprintf(stderr, "Adjusting SP by = %d, remains %d\n", (int) (-step1), (int) (size-step1));
      insn = gen_add3_insn (stack_pointer_rtx,
			    stack_pointer_rtx,
			    GEN_INT (-step1));
      RTX_FRAME_RELATED_P (emit_insn (insn)) = 1;
      size -= step1;
      riscv_for_each_saved_gpr_and_fpr (size, riscv_save_reg);
    }

  frame->mask = mask; /* Undo the above fib.  */

  /* Set up the frame pointer, if we're using one.  */
  if (frame_pointer_needed)
    {
      insn = gen_add3_insn (hard_frame_pointer_rtx, stack_pointer_rtx,
                            GEN_INT (frame->hard_frame_pointer_offset - size));
      RTX_FRAME_RELATED_P (emit_insn (insn)) = 1;
    }

  /* Allocate the rest of the frame.  */
  if (size > 0)
    {
      if (Trace) fprintf(stderr, "Processing remainder %d\n", (int) size);
      if (SMALL_OPERAND (-size))
	{
	  insn = gen_add3_insn (stack_pointer_rtx, stack_pointer_rtx,
				GEN_INT (-size));
	  RTX_FRAME_RELATED_P (emit_insn (insn)) = 1;
	}
      else
	{
	  riscv_emit_move (RISCV_PROLOGUE_TEMP (Pmode), GEN_INT (-size));
	  emit_insn (gen_add3_insn (stack_pointer_rtx,
				    stack_pointer_rtx,
				    RISCV_PROLOGUE_TEMP (Pmode)));

	  /* Describe the effect of the previous instructions.  */
	  insn = plus_constant (Pmode, stack_pointer_rtx, -size);
	  insn = gen_rtx_SET (VOIDmode, stack_pointer_rtx, insn);
	  riscv_set_frame_expr (insn);
	}
    }
    if (Trace) fprintf(stderr, "----------------------------------- DONE ------------------------\n");
}

/* Emit instructions to restore register REG from slot MEM.  */

static void
riscv_restore_reg (rtx reg, rtx mem)
{
  riscv_emit_save_slot_move (reg, mem, RISCV_EPILOGUE_TEMP (GET_MODE (reg)));
}

int riscv_epilogue_uses(int regno)

{
	if (reload_completed && cfun->machine && cfun->machine->is_interrupt) return 1;
	return (regno == RETURN_ADDR_REGNUM);
}

static void
riscv_set_current_function (tree decl)

{
        tree attrs;
        static bool Reg_Init = false;
        static char saved_call_used_regs[FIRST_PSEUDO_REGISTER];
        int i;


        if (Reg_Init) {
                        for (i=1; i<32; i++) call_used_regs[i] = saved_call_used_regs[i];
        }
        if (decl == NULL_TREE || current_function_decl == NULL_TREE ||
            current_function_decl == error_mark_node || ! cfun->machine) return;

        cfun->machine->is_interrupt = 0;
        if (decl) {
                attrs = TYPE_ATTRIBUTES (TREE_TYPE(decl));
                if ((attrs && lookup_attribute ("interrupt", attrs))) {
			tree function_type;

			if (DECL_DECLARED_INLINE_P (decl))
    				error ("cannot inline interrupt function %qE", DECL_NAME (decl));
  			DECL_UNINLINABLE (decl) = 1;

  			function_type = TREE_TYPE (decl);

  			if (TREE_TYPE (function_type) != void_type_node)
    				error ("interrupt function must have return type of void");

  			if (prototype_p (function_type)
      			    && (TREE_VALUE (TYPE_ARG_TYPES (function_type)) != void_type_node
          		        || TREE_CHAIN (TYPE_ARG_TYPES (function_type)) != NULL_TREE))
    				error ("interrupt function must have no arguments");

			cfun->machine->is_interrupt = 1;
		}
        } else return;

        if (cfun->machine->is_interrupt) {
                if (!Reg_Init) {
                        for (i=1; i<32; i++) saved_call_used_regs[i] = call_used_regs[i];
                        Reg_Init = true;
                }
                for (i=1; i<32; i++) call_used_regs[i] = 0;
        }

}


static bool riscv_current_func_contains_call()

{
	rtx_insn *first = entry_of_function();
	rtx_insn *insn;

	for (insn = next_active_insn (first); insn; insn = next_active_insn (insn)) {
		if (CALL_P (insn)) return true;
	}
	return false;
}

/* Expand an "epilogue" or "sibcall_epilogue" pattern; SIBCALL_P
   says which.  */

void
riscv_expand_epilogue (bool sibcall_p)
{
  /* Split the frame into two.  STEP1 is the amount of stack we should
     deallocate before restoring the registers.  STEP2 is the amount we
     should deallocate afterwards.

     Start off by assuming that no registers need to be restored.  */
  struct riscv_frame_info *frame = &cfun->machine->frame;
  unsigned mask = frame->mask;
  HOST_WIDE_INT step1 = frame->total_size;
  HOST_WIDE_INT step2 = 0;
  bool use_restore_libcall = !sibcall_p && riscv_use_save_libcall (frame);
  rtx ra = gen_rtx_REG (Pmode, RETURN_ADDR_REGNUM);

  if (cfun->machine->is_interrupt) {
	if (cfun->machine->has_hardware_loops) {
    		error ("interrupt function contains hardware loop: %s", current_function_name());
	}
	cfun->machine->contains_call = (!TARGET_MASK_NOHWLOOP && riscv_current_func_contains_call());
	if (cfun->machine->contains_call) {
    		error ("interrupt function contains function calls: %s", current_function_name());
	}
  }

  if (!sibcall_p && riscv_can_use_return_insn ())
    {
      if (frame->is_it)
        emit_jump_insn (gen_simple_it_return ());
      else
        emit_jump_insn (gen_return ());
      return;
    }

  /* Move past any dynamic stack allocations.  */
  if (cfun->calls_alloca)
    {
      rtx adjust = GEN_INT (-frame->hard_frame_pointer_offset);
      if (!SMALL_INT (adjust))
	{
	  riscv_emit_move (RISCV_EPILOGUE_TEMP (Pmode), adjust);
	  adjust = RISCV_EPILOGUE_TEMP (Pmode);
	}

      emit_insn (gen_add3_insn (stack_pointer_rtx, hard_frame_pointer_rtx, adjust));
    }

  /* If we need to restore registers, deallocate as much stack as
     possible in the second step without going out of range.  */
  if ((frame->mask | frame->fmask) != 0)
    {
      step2 = MIN (step1, RISCV_MAX_FIRST_STACK_STEP);
      step1 -= step2;
    }

  /* Set TARGET to BASE + STEP1.  */
  if (step1 > 0)
    {
      /* Get an rtx for STEP1 that we can add to BASE.  */
      rtx adjust = GEN_INT (step1);
      if (!SMALL_OPERAND (step1))
	{
	  riscv_emit_move (RISCV_EPILOGUE_TEMP (Pmode), adjust);
	  adjust = RISCV_EPILOGUE_TEMP (Pmode);
	}

      emit_insn (gen_add3_insn (stack_pointer_rtx, stack_pointer_rtx, adjust));
    }

  if (use_restore_libcall)
    frame->mask = 0; /* Temporarily fib that we need not save GPRs.  */

  /* Restore the registers.  */
  riscv_for_each_saved_gpr_and_fpr (frame->total_size - step2,
				    riscv_restore_reg);

  if (use_restore_libcall)
    {
      frame->mask = mask; /* Undo the above fib.  */
      gcc_assert (step2 >= frame->save_libcall_adjustment);
      step2 -= frame->save_libcall_adjustment;
    }

  /* Deallocate the final bit of the frame.  */
  if (step2 > 0)
    emit_insn (gen_add3_insn (stack_pointer_rtx, stack_pointer_rtx,
			      GEN_INT (step2)));

  if (use_restore_libcall)
    {
      emit_insn (gen_gpr_restore (GEN_INT (riscv_save_libcall_count (mask))));
      emit_jump_insn (gen_gpr_restore_return (ra));
      return;
    }

  /* Add in the __builtin_eh_return stack adjustment. */
  if (crtl->calls_eh_return)
    emit_insn (gen_add3_insn (stack_pointer_rtx, stack_pointer_rtx,
			      EH_RETURN_STACKADJ_RTX));
  if (!sibcall_p) {
      if (frame->is_it)
        emit_jump_insn (gen_simple_it_return ());
      else
        emit_jump_insn (gen_simple_return_internal (ra));
  }
}

/* Functions to save and restore machine-specific function data.  */
void
riscv_init_expanders (void)
{
  /* Arrange to initialize and mark the machine per-function status.  */
  // init_machine_status = riscv_init_machine_status;

  if (cfun && cfun->machine)
    {
      cfun->machine->has_hardware_loops = 0;
      cfun->machine->contains_call = 0;
    }
}

/* Return nonzero if this function is known to have a null epilogue.
   This allows the optimizer to omit jumps to jumps if no stack
   was created.  */

bool
riscv_can_use_return_insn (void)
{
  return reload_completed && cfun->machine->frame.total_size == 0;
}

/* Return true if register REGNO can store a value of mode MODE.
   The result of this function is cached in riscv_hard_regno_mode_ok.  */

static bool
riscv_hard_regno_mode_ok_p (unsigned int regno, enum machine_mode mode)
{
  unsigned int size = GET_MODE_SIZE (mode);
  enum mode_class mclass = GET_MODE_CLASS (mode);

  /* This is hella bogus but ira_build segfaults on RV32 without it. */
  if (VECTOR_MODE_P (mode))
    return true;

  if (GP_REG_P (regno))
    {
      if (size <= UNITS_PER_WORD)
	return true;

      /* Double-word values must be even-register-aligned.  */
      if (size <= 2 * UNITS_PER_WORD)
	return regno % 2 == 0;
    }

  if (FP_REG_P (regno))
    {
      if (mclass == MODE_FLOAT
	  || mclass == MODE_COMPLEX_FLOAT
	  || mclass == MODE_VECTOR_FLOAT)
	return size <= UNITS_PER_FPVALUE;
    }

  if (HWLOOP_REG_P (regno))
    {
      if (size <= UNITS_PER_WORD)
	return true;
    }
  if (VIT_REG_P (regno)) return true;

  return false;
}

/* Implement HARD_REGNO_NREGS.  */

unsigned int
riscv_hard_regno_nregs (int regno, enum machine_mode mode)
{
  if (FP_REG_P (regno))
    return (GET_MODE_SIZE (mode) + UNITS_PER_FPREG - 1) / UNITS_PER_FPREG;

  /* All other registers are word-sized.  */
  return (GET_MODE_SIZE (mode) + UNITS_PER_WORD - 1) / UNITS_PER_WORD;
}

/* Implement CLASS_MAX_NREGS, taking the maximum of the cases
   in riscv_hard_regno_nregs.  */

int
riscv_class_max_nregs (enum reg_class rclass, enum machine_mode mode)
{
  int size;
  HARD_REG_SET left;

  size = 0x8000;
  COPY_HARD_REG_SET (left, reg_class_contents[(int) rclass]);
  if (hard_reg_set_intersect_p (left, reg_class_contents[(int) FP_REGS]))
    {
      size = MIN (size, UNITS_PER_FPREG);
      AND_COMPL_HARD_REG_SET (left, reg_class_contents[(int) FP_REGS]);
    }
  if (!hard_reg_set_empty_p (left))
    size = MIN (size, UNITS_PER_WORD);
  return (GET_MODE_SIZE (mode) + size - 1) / size;
}

/* Implement TARGET_PREFERRED_RELOAD_CLASS.  */

static reg_class_t
riscv_preferred_reload_class (rtx x ATTRIBUTE_UNUSED, reg_class_t rclass)
{
  return reg_class_subset_p (FP_REGS, rclass) ? FP_REGS :
         reg_class_subset_p (GR_REGS, rclass) ? GR_REGS :
	 rclass;
}

/* RCLASS is a class involved in a REGISTER_MOVE_COST calculation.
   Return a "canonical" class to represent it in later calculations.  */

static reg_class_t
riscv_canonicalize_move_class (reg_class_t rclass)
{
  if (reg_class_subset_p (rclass, GENERAL_REGS))
    rclass = GENERAL_REGS;

  return rclass;
}

/* Implement TARGET_REGISTER_MOVE_COST.  Return 0 for classes that are the
   maximum of the move costs for subclasses; regclass will work out
   the maximum for us.  */

static int
riscv_register_move_cost (enum machine_mode mode ATTRIBUTE_UNUSED,
			 reg_class_t from, reg_class_t to)
{
  from = riscv_canonicalize_move_class (from);
  to = riscv_canonicalize_move_class (to);

  if ((from == GENERAL_REGS && to == GENERAL_REGS)
      || (from == GENERAL_REGS && to == FP_REGS)
      || (from == FP_REGS && to == FP_REGS))
    return COSTS_N_INSNS (1);

  if (from == FP_REGS && to == GENERAL_REGS)
    return tune_info->fp_to_int_cost;

  return 0;
}

/* Implement TARGET_MEMORY_MOVE_COST.  */

static int
riscv_memory_move_cost (enum machine_mode mode, reg_class_t rclass, bool in)
{
  return (tune_info->memory_cost
	  + memory_move_secondary_cost (mode, rclass, in));
} 

/* Return the register class required for a secondary register when
   copying between one of the registers in RCLASS and value X, which
   has mode MODE.  X is the source of the move if IN_P, otherwise it
   is the destination.  Return NO_REGS if no secondary register is
   needed.  */

enum reg_class
riscv_secondary_reload_class (enum reg_class rclass,
			     enum machine_mode mode, rtx x,
			     bool in_p ATTRIBUTE_UNUSED)
{
  int regno;

  regno = true_regnum (x);

  if (reg_class_subset_p (rclass, FP_REGS))
    {
      if (MEM_P (x) && (GET_MODE_SIZE (mode) == 4 || GET_MODE_SIZE (mode) == 8))
	/* We can use flw/fld/fsw/fsd. */
	return NO_REGS;

      if (GP_REG_P (regno) || x == CONST0_RTX (mode))
	/* We can use fmv or go through memory when mode > Pmode. */
	return NO_REGS;

      if (CONSTANT_P (x) && !targetm.cannot_force_const_mem (mode, x))
	/* We can force the constant to memory and use flw/fld. */
	return NO_REGS;

      if (FP_REG_P (regno))
	/* We can use fmv.fmt. */
	return NO_REGS;

      /* Otherwise, we need to reload through an integer register.  */
      return GR_REGS;
    }
  if (FP_REG_P (regno))
    return reg_class_subset_p (rclass, GR_REGS) ? NO_REGS : GR_REGS;

  return NO_REGS;
}

/* Implement TARGET_MODE_REP_EXTENDED.  */

static int
riscv_mode_rep_extended (enum machine_mode mode, enum machine_mode mode_rep)
{
  /* On 64-bit targets, SImode register values are sign-extended to DImode.  */
  if (TARGET_64BIT && mode == SImode && mode_rep == DImode)
    return SIGN_EXTEND;

  return UNKNOWN;
}

/* Implement TARGET_SCALAR_MODE_SUPPORTED_P.  */

static bool
riscv_scalar_mode_supported_p (enum machine_mode mode)
{
  if (ALL_FIXED_POINT_MODE_P (mode)
      && GET_MODE_PRECISION (mode) <= 2 * BITS_PER_WORD)
    return true;

  return default_scalar_mode_supported_p (mode);
}

/* Implement TARGET_SCHED_ADJUST_COST.  We assume that anti and output
   dependencies have no cost. */

static int
riscv_adjust_cost (rtx_insn *insn ATTRIBUTE_UNUSED, rtx link,
		   rtx_insn *dep ATTRIBUTE_UNUSED, int cost)
{
  if (REG_NOTE_KIND (link) != 0)
    return 0;
  return cost;
}

/* Return the number of instructions that can be issued per cycle.  */

static int
riscv_issue_rate (void)
{
  return tune_info->issue_rate;
}

struct PostExtractAction {
	int Yes;
	int Size;
	int Off;
	int Sign;
};

struct ExtraBuiltinImmArg {
	int Count;
	int IsReg[4];
	int Pos[4];
	int Value[4];
	struct PostExtractAction PostExtract;
};

typedef int (*BuiltinChecker)(int Code, int BuiltinIndex, struct ExtraBuiltinImmArg *ExtraImmArg, int Narg, ...);

/* This structure describes a single built-in function.  */
struct riscv_builtin_description {
  /* The code of the main .md file instruction.  See riscv_builtin_type
     for more information.  */
  enum insn_code icode;

  /* The name of the built-in function.  */
  const char *name;

  /* Specifies how the function should be expanded.  */
  enum riscv_builtin_type builtin_type;

  /* The function's prototype.  */
  enum riscv_function_type function_type;

  /* Whether the function is available.  */
  unsigned int (*avail) (void);

  /* Whether the function args are correct, optional */
  BuiltinChecker check;
};

static unsigned int
riscv_builtin_avail_riscv (void)
{
  return 1;
}

static unsigned int
riscv_builtin_avail_pulp_v0_only (void)
{
  if (Pulp_Cpu==PULP_V0) return 1;
  return 0;
}

static unsigned int
riscv_builtin_avail_pulp_v2 (void)
{
  if (Pulp_Cpu>=PULP_V2) return 1;
  return 0;
}

static unsigned int
riscv_builtin_avail_pulp_v2_new (void)
{
  if (TARGET_MASK_NEW_INSN && Pulp_Cpu>=PULP_V2) return 1;
  return 0;
}

static unsigned int
riscv_builtin_avail_pulp_v3 (void)
{
  if (Pulp_Cpu>=PULP_V3) return 1;
  return 0;
}


static unsigned int
riscv_builtin_avail_pulp_vall (void)
{
  if (Pulp_Cpu>=PULP_V0) return 1;
  return 0;
}

static int CheckBuiltin(int Code, int BuiltinIndex, struct ExtraBuiltinImmArg *ExtraImmArg, int Narg, ...);

/* Construct a riscv_builtin_description from the given arguments.

   INSN is the name of the associated instruction pattern, without the
   leading CODE_FOR_riscv_.

   CODE is the floating-point condition code associated with the
   function.  It can be 'f' if the field is not applicable.

   NAME is the name of the function itself, without the leading
   "__builtin_riscv_".

   BUILTIN_TYPE and FUNCTION_TYPE are riscv_builtin_description fields.

   AVAIL is the name of the availability predicate, without the leading
   riscv_builtin_avail_.  */
#define RISCV_BUILTIN(INSN, NAME, BUILTIN_TYPE, FUNCTION_TYPE, AVAIL, CHECK)	\
  { CODE_FOR_ ## INSN, "__builtin_riscv_" NAME,				\
    BUILTIN_TYPE, FUNCTION_TYPE, riscv_builtin_avail_ ## AVAIL , CHECK },

#define RISCV_BUILTIN1(INSN, NAME, BUILTIN_TYPE, FUNCTION_TYPE, AVAIL, CHECK)	\
  { CODE_FOR_ ## INSN, "__builtin_pulp_" NAME,				\
    BUILTIN_TYPE, FUNCTION_TYPE, riscv_builtin_avail_ ## AVAIL , CHECK },

#define RISCV_BUILTIN2(INSN, NAME, BUILTIN_TYPE, FUNCTION_TYPE, AVAIL, CHECK)	\
  { CODE_FOR_ ## INSN, "__builtin_" NAME,				\
    BUILTIN_TYPE, FUNCTION_TYPE, riscv_builtin_avail_ ## AVAIL , CHECK },

/* Define __builtin_riscv_<INSN>, which is a RISCV_BUILTIN_DIRECT function
   mapped to instruction CODE_FOR_<INSN>,  FUNCTION_TYPE and AVAIL
   are as for RISCV_BUILTIN.  */
#define DIRECT_BUILTIN(INSN, FUNCTION_TYPE, AVAIL, CHECK)			\
  RISCV_BUILTIN (INSN, #INSN, RISCV_BUILTIN_DIRECT, FUNCTION_TYPE, AVAIL, CHECK)

#define DIRECT_BUILTIN1(INSN, NAME, FUNCTION_TYPE, AVAIL, CHECK)			\
  RISCV_BUILTIN1 (INSN, #NAME, RISCV_BUILTIN_DIRECT, FUNCTION_TYPE, AVAIL, CHECK)


/* Define __builtin_riscv_<INSN>, which is a RISCV_BUILTIN_DIRECT_NO_TARGET
   function mapped to instruction CODE_FOR_<INSN>,  FUNCTION_TYPE
   and AVAIL are as for RISCV_BUILTIN.  */
#define DIRECT_NO_TARGET_BUILTIN(INSN, FUNCTION_TYPE, AVAIL, CHECK)		\
  RISCV_BUILTIN (INSN, #INSN, RISCV_BUILTIN_DIRECT_NO_TARGET,		\
		FUNCTION_TYPE, AVAIL, CHECK)

#define DIRECT_NO_TARGET_BUILTIN1(INSN, NAME, FUNCTION_TYPE, AVAIL, CHECK)			\
  RISCV_BUILTIN1 (INSN, #NAME, RISCV_BUILTIN_DIRECT_NO_TARGET, FUNCTION_TYPE, AVAIL, CHECK)

static const struct riscv_builtin_description riscv_builtins[] = {
#include "pulp_builtins.def"
};

#undef DIRECT_NO_TARGET_BUILTIN1
#define DIRECT_NO_TARGET_BUILTIN1(INSN, NAME, FUNCTION_TYPE, AVAIL, CHECK) PULP_BUILTIN_ ## NAME,
#undef DIRECT_NO_TARGET_BUILTIN
#define DIRECT_NO_TARGET_BUILTIN(INSN, FUNCTION_TYPE, AVAIL, CHECK) PULP_BUILTIN_ ## NAME,
#undef DIRECT_BUILTIN1
#define DIRECT_BUILTIN1(INSN, NAME, FUNCTION_TYPE, AVAIL, CHECK) PULP_BUILTIN_ ## NAME,
enum Pulp_Builtin_Id {
#include "pulp_builtins.def"
};

static int CheckBuiltin(int Code, int BuiltinIndex, struct ExtraBuiltinImmArg *ExtraImmArg, int Narg, ...)

{
	int i;
	rtx Op[6] = {NULL, NULL, NULL, NULL, NULL, NULL};
	const char *Diag=NULL;
	va_list ap;

	va_start(ap, Narg);
	for (i=0; i<Narg; i++) Op[i] = va_arg(ap, rtx);
	va_end(ap);

	if (ExtraImmArg) {
		ExtraImmArg->Count = 0;
		ExtraImmArg->PostExtract.Yes = 0;
	}

	switch ((enum Pulp_Builtin_Id) BuiltinIndex) {
		case PULP_BUILTIN_CoreCount:
			ExtraImmArg->Count = 2;
			ExtraImmArg->Pos[0] = 1; ExtraImmArg->Pos[1] = 2;
			ExtraImmArg->IsReg[0] = 1; ExtraImmArg->IsReg[1] = 0;
			// Gap8: (APB_SOC_CTRL_ADDR + 0x12) = SOC_PERIPHERALS_BASE_ADDR + 0x0300 + 0x12
			// 0x1A100000 + 0x3000 + 0x12
			ExtraImmArg->Value[0] = 0x1A103000;
			ExtraImmArg->Value[1] = 0x12;
			break;
		case PULP_BUILTIN_CoreId:
			ExtraImmArg->Count = 1;
			ExtraImmArg->IsReg[0] = 0;
			ExtraImmArg->Value[0] = 0xF10;
			ExtraImmArg->PostExtract.Yes = 1; ExtraImmArg->PostExtract.Size = 5;
			ExtraImmArg->PostExtract.Off = 0; ExtraImmArg->PostExtract.Sign = 0;
			Op[0] = gen_rtx_CONST_INT(SImode, ExtraImmArg->Value[0]);
			break;
		case PULP_BUILTIN_ClusterId:
			ExtraImmArg->Count = 1;
			ExtraImmArg->IsReg[0] = 0;
			ExtraImmArg->Value[0] = 0xF10;
			ExtraImmArg->PostExtract.Yes = 1; ExtraImmArg->PostExtract.Size = 6;
			ExtraImmArg->PostExtract.Off = 5; ExtraImmArg->PostExtract.Sign = 0;
			Op[0] = gen_rtx_CONST_INT(SImode, ExtraImmArg->Value[0]);
			break;
		case PULP_BUILTIN_IsFc:
			ExtraImmArg->Count = 1;
			ExtraImmArg->IsReg[0] = 0;
			ExtraImmArg->Value[0] = 0xF10;
			ExtraImmArg->PostExtract.Yes = 1; ExtraImmArg->PostExtract.Size = 1;
			ExtraImmArg->PostExtract.Off = 10; ExtraImmArg->PostExtract.Sign = 1;
			Op[0] = gen_rtx_CONST_INT(SImode, ExtraImmArg->Value[0]);
			break;
		default:
			break;
	}

	switch (Code) {
		/* Op3 const and in 0..31, Op4 const and == 2^(Op3 - 1) */
		case CODE_FOR_macsRNr_si3:
		case CODE_FOR_macuRNr_si3:
		case CODE_FOR_machhsRNr_si3:
		case CODE_FOR_machhuRNr_si3:
			if (Op[3] && (GET_CODE(Op[3]) == CONST_INT) && Op[4] && (GET_CODE(Op[4]) == CONST_INT)) {
				int Norm = INTVAL (Op[3]);
				int Round = INTVAL (Op[4]);
				if (Norm >= 0 && Norm <= 31) {
					if ((1 << (Norm - 1)) == Round) return 1;
				}
			}
			Diag = "__builtin_pulp_mac{hh,}{s,u}NRr (X, Y, Acc, Norm, Round) expects Norm and Round cst, Norm<=31, Round==2^(Norm-1)";
			break;
		/* Op2 const and in 0..31, Op3 const and == 2^(Op2 - 1) */
		case CODE_FOR_mulsRNr_si3:
		case CODE_FOR_muluRNr_si3:
		case CODE_FOR_mulhhsRNr_si3:
		case CODE_FOR_mulhhuRNr_si3:
			Diag = "__builtin_pulp_mul{hh,}{s,u}NRr (X, Y, Norm, Round) expects Norm and Round cst, Norm<=31, Round==2^(Norm-1)";
		case CODE_FOR_addRN_si3:
		case CODE_FOR_addRNu_si3:
		case CODE_FOR_subRN_si3:
		case CODE_FOR_subRNu_si3:
			if (Op[2] && (GET_CODE(Op[2]) == CONST_INT) && Op[3] && (GET_CODE(Op[3]) == CONST_INT)) {
				int Norm = INTVAL (Op[2]);
				int Round = INTVAL (Op[3]);
				if (Norm >= 0 && Norm <= 31) {
					if ((1 << (Norm - 1)) == Round) return 1;
				}
			}
			if (Diag == NULL)
				Diag = "__builtin_pulp_{add,addu,sub}{hh,}NRr (X, Y, Norm, Round) expects Norm and Round cst, Norm<=31, Round==2^(Norm-1)";
			break;
		/* Op3 const and in 0..31 */
		case CODE_FOR_macsNr_si3:
		case CODE_FOR_macuNr_si3:
		case CODE_FOR_machhsNr_si3:
		case CODE_FOR_machhuNr_si3:
			if (Op[3] && (GET_CODE(Op[3]) == CONST_INT)) {
				int Norm = INTVAL (Op[3]);
				if (Norm >= 0 && Norm <= 31) return 1;
			}
			Diag = "__builtin_pulp_mac{hh,}{s,u}NRr (X, Y, Acc, Norm, Round) expects Norm and Round cst, Norm<=31, Round==2^(Norm-1)";
			break;
		/* Op2 const and in 0..31 */
		case CODE_FOR_mulsNr_si3:
		case CODE_FOR_muluNr_si3:
		case CODE_FOR_mulhhsNr_si3:
		case CODE_FOR_mulhhuNr_si3:
			Diag = "__builtin_pulp_mul{hh,}{s,u}Nr (X, Y, Norm) expects Norm cst, Norm<=31";
		case CODE_FOR_addN_si3:
		case CODE_FOR_addNu_si3:
		case CODE_FOR_subN_si3:
		case CODE_FOR_subNu_si3:
			if (Op[2] && (GET_CODE(Op[2]) == CONST_INT)) {
				int Norm = INTVAL (Op[2]);
				if (Norm >= 0 && Norm <= 31) return 1;
			}
			if (Diag==NULL) Diag = "__builtin_pulp_{add,addu,sub}Nr (X, Y, Norm) expects Norm cst, Norm<=31";
			break;
		/* Op1 const, Op2 const, Op1==-2^(N-1), Op2==2^(N-1)-1 */
		case CODE_FOR_clip_minmax:
			if (Op[1] && (GET_CODE(Op[1]) == CONST_INT) && Op[2] && (GET_CODE(Op[2]) == CONST_INT)) {
				int i;
				int Min = INTVAL (Op[1]);
				int Max = INTVAL (Op[2]);
				for (i = 0; i < 30; i ++) if ((Max == (1 << i) - 1) && (Min == - (1 << i))) return 1;
			}
			Diag = "__builtin_pulp_clip (X, Min, Max) expects Min and Max cst, Min=-2^(N-1), Max=2^(N-1)-1";
			break;
		/* Op1 const, Op2 const, Op1==0, Op2==2^(N-1)-1 */
		case CODE_FOR_clipu_minmax:
			if (Op[1] && (GET_CODE(Op[1]) == CONST_INT) && Op[2] && (GET_CODE(Op[2]) == CONST_INT)) {
				int i;
				int Min = INTVAL (Op[1]);
				int Max = INTVAL (Op[2]);
				if (Min==0) for (i = 0; i < 30; i ++) if ((Max == (1 << i) - 1)) return 1;
			}
			Diag = "__builtin_pulp_clipu (X, Min, Max) expects Min and Max cst, Min=0, Max=2^(N-1)-1";
			break;
		case CODE_FOR_bclrsi3:
			if (Op[1] && (GET_CODE(Op[1]) == CONST_INT)) {
				if (riscv_valid_bit_field_imm_operand(Op[1], NULL, 0, NULL, NULL)) return 1;
			}
			Diag = "builtin_pulp_bclr(X, BitMask) expects BistMask cst, BitMask = ~(BitToClear)\n";
			break;
		case CODE_FOR_bsetsi3:
			if (Op[1] && (GET_CODE(Op[1]) == CONST_INT)) {
				if (riscv_valid_bit_field_imm_operand(Op[1], NULL, 1, NULL, NULL)) return 1;
			}
			Diag = "builtin_pulp_bset(X, BitMask) expects BistMask cst, BitMask = BitToSet\n";
			break;
		/* Op1 const > 0, Op2 const >= 0, (Op1+Op2)<32 */
		case CODE_FOR_extvsi:
		case CODE_FOR_extzvsi:
			if (Op[1] && (GET_CODE(Op[1]) == CONST_INT) && Op[2] && (GET_CODE(Op[2]) == CONST_INT)) {
				int Size   = INTVAL (Op[1]);
				int Offset = INTVAL (Op[2]);
				if (Size > 0 && Offset >=0 && ((Size+Offset)<=32)) return 1;
			}
			Diag = "__builtin_pulp_bextract(X, Size, Offset) Expects Size and Offset immediate constants, Size>0, Offset>=0, (Size+Offset)<=32";
			break;
		/* Op0 const > 0, Op1 const >= 0, (Op0+Op1)<32 */
		/* Op0 -> Target
		   Op1 -> ~Mask
		   Op2 -> InsVal
		   Op3 -> Mask
		   Op4 -> Off
		*/
		case CODE_FOR_invsipat1:
			if (Op[1] && (GET_CODE(Op[1]) == CONST_INT) && Op[3] && (GET_CODE(Op[3]) == CONST_INT) &&
			    Op[4] && (GET_CODE(Op[4]) == CONST_INT)) {
				unsigned int Mask   = UINTVAL (Op[3]);
				unsigned int MaskBar= UINTVAL (Op[1]);
				unsigned int Size=0;
				unsigned int Offset = UINTVAL (Op[4]);
				int i;
				for (i=Offset; i<32; i++, Size++) if (((1<<i) & Mask) == 0) break;
				if ((MaskBar == ~Mask) && (Offset <= 31) &&
				    ((unsigned int) (((1<<Size) - 1) << Offset) == Mask)) return 1;
			}
			Diag = "__builtin_pulp_binsert (Target, MaskBar, InsVal, Size, Mask, Off) expects Off,Mask,MaskBar cst, Size>0, Off>=0, (Off+Size)<=32";
			break;
		case CODE_FOR_load_evt_unit:
			if (Op[1] && (GET_CODE(Op[1]) == CONST_INT)) return 1;
			Diag = "__builtin_event_unit_read(base, offset), offset expected to be immediate value";
			break;
		case CODE_FOR_read_spr_vol:
		case CODE_FOR_read_spr:
			if (Op[0] && (GET_CODE(Op[0]) == CONST_INT)) {
				unsigned int Reg = UINTVAL(Op[0]);
				if (Reg <= 4091) return 1;
			}
			Diag = "__builtin_pulp_spr_read(Spr) or __builtin_pulp_spr_read_vol(Spr) expects Spr to be immediate and in [0..4091]";
			break;
		case CODE_FOR_write_spr:
			if (Op[0] && (GET_CODE(Op[0]) == CONST_INT)) {
				unsigned int Reg = UINTVAL(Op[0]);
				if (Reg <= 4091) return 1;
			}
			Diag = "__builtin_pulp_spr_write(Spr, Value) expects Spr to be immediate and in [0..4091]";
			break;
		case CODE_FOR_read_then_write_spr:
			if (Op[0] && (GET_CODE(Op[0]) == CONST_INT)) {
				unsigned int Reg = UINTVAL(Op[0]);
				if (Reg <= 4091) return 1;
			}
			Diag = "__builtin_pulp_read_then_spr_write(Spr, Value) expects Spr to be immediate and in [0..4091]";
			break;
		case CODE_FOR_spr_bit_set:
			if (Op[0] && (GET_CODE(Op[0]) == CONST_INT)) {
				unsigned int Reg = UINTVAL(Op[0]);
				if (Reg <= 4091) return 1;
			}
			Diag = "__builtin_pulp_spr_bit_set(Spr, Value) expects Spr to be immediate and in [0..4091]";
			break;
		case CODE_FOR_read_then_spr_bit_set:
			if (Op[0] && (GET_CODE(Op[0]) == CONST_INT)) {
				unsigned int Reg = UINTVAL(Op[0]);
				if (Reg <= 4091) return 1;
			}
			Diag = "__builtin_pulp_read_then_spr_bit_set(Spr, Value) expects Spr to be immediate and in [0..4091]";
			break;
		case CODE_FOR_spr_bit_clr:
			if (Op[0] && (GET_CODE(Op[0]) == CONST_INT)) {
				unsigned int Reg = UINTVAL(Op[0]);
				if (Reg <= 4091) return 1;
			}
			Diag = "__builtin_pulp_spr_bit_clr(Spr, Value) expects Spr to be immediate and in [0..4091]";
			break;
		case CODE_FOR_read_then_spr_bit_clr:
			if (Op[0] && (GET_CODE(Op[0]) == CONST_INT)) {
				unsigned int Reg = UINTVAL(Op[0]);
				if (Reg <= 4091) return 1;
			}
			Diag = "__builtin_pulp_read_then_spr_bit_clr(Spr, Value) expects Spr to be immediate and in [0..4091]";
			break;
		/* Internal error no handler for this builtin code */
		default:
			// gcc_unreachable ();
			return 1;


	}
	/* Wrong arguments passed to builtin */
	if (Diag) error("Builtin %s", Diag); else error("Builtin No Diagnosis");
	return 0;
}

/* Index I is the function declaration for riscv_builtins[I], or null if the
   function isn't defined on this target.  */
static GTY(()) tree riscv_builtin_decls[ARRAY_SIZE (riscv_builtins)];


/* Source-level argument types.  */
#define RISCV_ATYPE_VOID void_type_node
#define RISCV_ATYPE_CHAR char_type_node
#define RISCV_ATYPE_SHORT short_integer_type_node
#define RISCV_ATYPE_INT integer_type_node
#define RISCV_ATYPE_POINTER ptr_type_node
#define RISCV_ATYPE_CPOINTER const_ptr_type_node

/* Standard mode-based argument types.  */
#define RISCV_ATYPE_UQI unsigned_intQI_type_node
#define RISCV_ATYPE_SI intSI_type_node
#define RISCV_ATYPE_USI unsigned_intSI_type_node
#define RISCV_ATYPE_DI intDI_type_node
#define RISCV_ATYPE_UDI unsigned_intDI_type_node
#define RISCV_ATYPE_SF float_type_node
#define RISCV_ATYPE_DF double_type_node

#define RISCV_ATYPE_V2HI opaque_V2HI_type_node
#define RISCV_ATYPE_V4QI opaque_V4QI_type_node

/* RISCV_FTYPE_ATYPESN takes N RISCV_FTYPES-like type codes and lists
   their associated RISCV_ATYPEs.  */
#define RISCV_FTYPE_ATYPES1(A, B) \
  RISCV_ATYPE_##A, RISCV_ATYPE_##B

#define RISCV_FTYPE_ATYPES2(A, B, C) \
  RISCV_ATYPE_##A, RISCV_ATYPE_##B, RISCV_ATYPE_##C

#define RISCV_FTYPE_ATYPES3(A, B, C, D) \
  RISCV_ATYPE_##A, RISCV_ATYPE_##B, RISCV_ATYPE_##C, RISCV_ATYPE_##D

#define RISCV_FTYPE_ATYPES4(A, B, C, D, E) \
  RISCV_ATYPE_##A, RISCV_ATYPE_##B, RISCV_ATYPE_##C, RISCV_ATYPE_##D, \
  RISCV_ATYPE_##E

#define RISCV_FTYPE_ATYPES5(A, B, C, D, E, F) \
  RISCV_ATYPE_##A, RISCV_ATYPE_##B, RISCV_ATYPE_##C, RISCV_ATYPE_##D, \
  RISCV_ATYPE_##E, RISCV_ATYPE_##F

#define RISCV_FTYPE_ATYPES6(A, B, C, D, E, F, G) \
  RISCV_ATYPE_##A, RISCV_ATYPE_##B, RISCV_ATYPE_##C, RISCV_ATYPE_##D, \
  RISCV_ATYPE_##E, RISCV_ATYPE_##F, RISCV_ATYPE_##G

/* Return the function type associated with function prototype TYPE.  */

#define MAX_REMAPPED_GOMP 20
static struct {
	unsigned int Gomp;
	unsigned int Pulp;
} Remapped_GOMP_Builtins[MAX_REMAPPED_GOMP];

static int Head_Remapped_GOMP_Builtins=0;

enum Riscv_Native_GOMP_Builtins {
	NATIVE_GOMP_LOOP_CHUNK_SIZE = 0,
	NATIVE_GOMP_LOOP_START = 1,
	NATIVE_GOMP_LAST = 2
};

static struct {
	tree	TypeDescr;
	int	Base;
	int 	Index;
} Native_GOMP_Builtins[NATIVE_GOMP_LAST] = 
{
	{NULL, 0x00204000, 0x70},		// OMP Loop Chunk Size
	{NULL, 0x00204000, 0x64},		// OMP Loop Start
};

unsigned int GetRemappedGompBuiltin(unsigned int ompcode, unsigned int def_ret)

{
	int i;

	for (i=0; i<Head_Remapped_GOMP_Builtins; i++) {
		if (Remapped_GOMP_Builtins[i].Gomp == ompcode) return Remapped_GOMP_Builtins[i].Pulp;
	}
	return def_ret ;
}


static tree
riscv_build_function_type (enum riscv_function_type type)
{
  static tree types[(int) RISCV_MAX_FTYPE_MAX];

  if (types[(int) type] == NULL_TREE)
    switch (type)
      {
#define DEF_RISCV_FTYPE(NUM, ARGS)					\
  case RISCV_FTYPE_NAME##NUM ARGS:					\
    types[(int) type]							\
      = build_function_type_list (RISCV_FTYPE_ATYPES##NUM ARGS,		\
				  NULL_TREE);				\
    break;
#include "config/riscv/riscv-ftypes.def"
#undef DEF_RISCV_FTYPE
      default:
	gcc_unreachable ();
      }

  return types[(int) type];
}

/* Implement TARGET_INIT_BUILTINS.  */

static void
riscv_init_builtins (void)
{
  const struct riscv_builtin_description *d;
  unsigned int i;

  opaque_V4QI_type_node    = build_opaque_vector_type (intQI_type_node, 4);
  opaque_V2HI_type_node    = build_opaque_vector_type (intHI_type_node, 2);

  /* Iterate through all of the bdesc arrays, initializing all of the
     builtin functions.  */
  for (i = 0; i < ARRAY_SIZE (riscv_builtins); i++)
    {
      d = &riscv_builtins[i];
      if (d->avail ()) {
	/* fprintf(stderr, "Adding %s\n", d->name); fflush(stderr); */
		riscv_builtin_decls[i] = add_builtin_function (d->name,
				  			       riscv_build_function_type (d->function_type),
				  			       i, BUILT_IN_MD, NULL, NULL);
		switch (d->icode) {
			case CODE_FOR_pulp_omp_barrier:
				Remapped_GOMP_Builtins[Head_Remapped_GOMP_Builtins].Gomp = BUILT_IN_GOMP_BARRIER;
				Remapped_GOMP_Builtins[Head_Remapped_GOMP_Builtins].Pulp = i;
				Head_Remapped_GOMP_Builtins++;
				break;
			case CODE_FOR_pulp_omp_critical_start:
				Remapped_GOMP_Builtins[Head_Remapped_GOMP_Builtins].Gomp = BUILT_IN_GOMP_CRITICAL_START;
				Remapped_GOMP_Builtins[Head_Remapped_GOMP_Builtins].Pulp = i;
				Head_Remapped_GOMP_Builtins++;
				break;
			case CODE_FOR_pulp_omp_critical_end:
				Remapped_GOMP_Builtins[Head_Remapped_GOMP_Builtins].Gomp = BUILT_IN_GOMP_CRITICAL_END;
				Remapped_GOMP_Builtins[Head_Remapped_GOMP_Builtins].Pulp = i;
				Head_Remapped_GOMP_Builtins++;
				break;
			case CODE_FOR_OffsetedReadOMP:
				Native_GOMP_Builtins[NATIVE_GOMP_LOOP_CHUNK_SIZE].TypeDescr = riscv_builtin_decls[i];
				Native_GOMP_Builtins[NATIVE_GOMP_LOOP_START].TypeDescr = riscv_builtin_decls[i];
				break;
			default:
			;
		}
       }
    }
}


/* Implement `TARGET_FOLD_BUILTIN'.  */

static tree
riscv_fold_builtin (tree fndecl, int n_args ATTRIBUTE_UNUSED, tree *arg,
                    bool ignore ATTRIBUTE_UNUSED)
{
	unsigned int fcode = DECL_FUNCTION_CODE (fndecl);
	tree val_type = TREE_TYPE (TREE_TYPE (fndecl));

	switch (fcode) {
		case PULP_BUILTIN_CoreCount:
			if (_Pulp_PE>0) {
				 return build_int_cst (integer_type_node, _Pulp_PE);
			} else return NULL_TREE;
			break;
		case PULP_BUILTIN_HasFc:
			if (_Pulp_FC) {
				return integer_one_node; // build_int_cst (integer_type_node, 1);
			} else {
				return integer_zero_node; // build_int_cst (integer_type_node, 0);
			}
			break;
		case PULP_BUILTIN_IsFc:
			if (_Pulp_FC == 0) return integer_zero_node; else return NULL_TREE;
			break;
		default:
			return NULL_TREE;
	}

}

/* Implement TARGET_BUILTIN_DECL.  */

static tree
riscv_builtin_decl (unsigned int code, bool initialize_p ATTRIBUTE_UNUSED)
{
  if (code >= ARRAY_SIZE (riscv_builtins))
    return error_mark_node;
  return riscv_builtin_decls[code];
}

/* Take argument ARGNO from EXP's argument list and convert it into a
   form suitable for input operand OPNO of instruction ICODE.  Return the
   value.  */

static rtx
riscv_prepare_builtin_arg (enum insn_code icode,
			  unsigned int opno, tree exp, unsigned int argno)
{
  tree arg;
  rtx value;
  enum machine_mode mode;

  arg = CALL_EXPR_ARG (exp, argno);
  value = expand_normal (arg);
  mode = insn_data[icode].operand[opno].mode;
  if (!insn_data[icode].operand[opno].predicate (value, mode))
    {
      /* We need to get the mode from ARG for two reasons:

	   - to cope with address operands, where MODE is the mode of the
	     memory, rather than of VALUE itself.

	   - to cope with special predicates like pmode_register_operand,
	     where MODE is VOIDmode.  */

      value = copy_to_mode_reg (TYPE_MODE (TREE_TYPE (arg)), value);


      /* Check the predicate again.  */
      if (!insn_data[icode].operand[opno].predicate (value, mode))
	{
	  error ("invalid argument to built-in function on arg %d, builtin arg mode: %s, actual arg mode: %s",
		  opno, GET_MODE_NAME(mode), GET_MODE_NAME(TYPE_MODE (TREE_TYPE (arg))));
	  return const0_rtx;
	}
    }

  return value;
}

/* Return an rtx suitable for output operand OP of instruction ICODE.
   If TARGET is non-null, try to use it where possible.  */

static rtx
riscv_prepare_builtin_target (enum insn_code icode, unsigned int op, rtx target)
{
  enum machine_mode mode;

  mode = insn_data[icode].operand[op].mode;
  if (target == 0 || !insn_data[icode].operand[op].predicate (target, mode))
    target = gen_reg_rtx (mode);

  return target;
}

static void PulpBuiltinGenPostExtract(struct ExtraBuiltinImmArg *ExtraArg, rtx OutReg)

{
	if (!ExtraArg->PostExtract.Yes) return;

	if (ExtraArg->PostExtract.Sign)
		emit_insn(gen_extvsi(OutReg, OutReg, gen_rtx_CONST_INT(SImode, ExtraArg->PostExtract.Size),
						     gen_rtx_CONST_INT(SImode, ExtraArg->PostExtract.Off)));
	else
		emit_insn(gen_extzvsi(OutReg, OutReg, gen_rtx_CONST_INT(SImode, ExtraArg->PostExtract.Size),
						      gen_rtx_CONST_INT(SImode, ExtraArg->PostExtract.Off)));
}

/* Expand a RISCV_BUILTIN_DIRECT or RISCV_BUILTIN_DIRECT_NO_TARGET function;
   HAS_TARGET_P says which.  EXP is the CALL_EXPR that calls the function
   and ICODE is the code of the associated .md pattern.  TARGET, if nonnull,
   suggests a good place to put the result.  */

static rtx
riscv_expand_builtin_direct (const struct riscv_builtin_description *d, int builtin_index, enum insn_code icode, rtx target, tree exp,
			    bool has_target_p)
{
  rtx ops[MAX_RECOG_OPERANDS];
  int opno, argno;
  struct ExtraBuiltinImmArg ExtraArg;

  /* Map any target to operand 0.  */
  ExtraArg.Count = 0; ExtraArg.PostExtract.Yes = 0;
  opno = 0;
  if (has_target_p)
    {
      target = riscv_prepare_builtin_target (icode, opno, target);
      ops[opno] = target;
      opno++;
    }

  /* Map the arguments to the other operands.  The n_operands value
     for an expander includes match_dups and match_scratches as well as
     match_operands, so n_operands is only an upper bound on the number
     of arguments to the expander function.  */

  gcc_assert (opno + call_expr_nargs (exp) <= insn_data[icode].n_operands);
  for (argno = 0; argno < call_expr_nargs (exp); argno++, opno++)
    ops[opno] = riscv_prepare_builtin_arg (icode, opno, exp, argno);
  if (has_target_p) {
  	if (d->check) {
		d->check(icode, builtin_index, &ExtraArg, call_expr_nargs (exp), ops[1], ops[2], ops[3], ops[4], ops[5]);
		if (ExtraArg.Count) {
			int i;
			for (i=0; i<ExtraArg.Count; i++) {
				if (ExtraArg.IsReg[i]) {
					rtx Reg = gen_reg_rtx (SImode);
				
					emit_insn(gen_movsi(Reg, gen_rtx_CONST_INT(SImode, ExtraArg.Value[i])));
					ops[opno] = Reg;
				} else {
					ops[opno] = gen_rtx_CONST_INT(SImode, ExtraArg.Value[i]);
				}
				opno++; argno++;
			}
		}
	}
  } else {
  	if (d->check) {
		d->check(icode, builtin_index, &ExtraArg, call_expr_nargs (exp), ops[0], ops[1], ops[2], ops[3]);
		if (ExtraArg.Count) {
			int i;
			for (i=0; i<ExtraArg.Count; i++) {
				if (ExtraArg.IsReg[i]) {
					rtx Reg = gen_reg_rtx (SImode);
				
					emit_insn(gen_movsi(Reg, gen_rtx_CONST_INT(SImode, ExtraArg.Value[i])));
					ops[opno] = Reg;
				} else {
					ops[opno] = gen_rtx_CONST_INT(SImode, ExtraArg.Value[i]);
				}
				opno++; argno++;
			}
		}
	}
  }

  switch (opno)
    {
    case 0:
      emit_insn (GEN_FCN (icode) ());
      break;
    case 1:
      emit_insn (GEN_FCN (icode) (ops[0]));
      break;
    case 2:
      emit_insn (GEN_FCN (icode) (ops[0], ops[1]));
      break;

    case 3:
      emit_insn (GEN_FCN (icode) (ops[0], ops[1], ops[2]));
      break;

    case 4:
      emit_insn (GEN_FCN (icode) (ops[0], ops[1], ops[2], ops[3]));
      break;

    case 5:
      emit_insn (GEN_FCN (icode) (ops[0], ops[1], ops[2], ops[3], ops[4]));
      break;

    case 6:
      emit_insn (GEN_FCN (icode) (ops[0], ops[1], ops[2], ops[3], ops[4], ops[5]));
      break;

    default:
      gcc_unreachable ();
    }
  PulpBuiltinGenPostExtract(&ExtraArg, ops[0]);
  return target;
}

/* Implement TARGET_REMAPPED_BUILTIN */

static int
riscv_remapped_builtin(tree exp)

{
	tree fndecl = get_callee_fndecl (exp);
	enum built_in_function fcode = DECL_FUNCTION_CODE (fndecl);

	if (TARGET_MASK_OPEN_NATIVE) {
 		fcode = (enum built_in_function) GetRemappedGompBuiltin(fcode, ARRAY_SIZE (riscv_builtins));
		if (fcode < ARRAY_SIZE (riscv_builtins)) return fcode;
	}
	return -1;
}

/* Implement TARGET_OMP_TARGET_DECL */

static tree
riscv_omp_target_decl(int t_omp_code, int *Base, int *Index)

{

	if (!TARGET_MASK_OPEN_NATIVE) return NULL;


	if (t_omp_code < 0 || t_omp_code >= NATIVE_GOMP_LAST) return NULL;
	if (Base) *Base = Native_GOMP_Builtins[t_omp_code].Base;
	if (Index) *Index = Native_GOMP_Builtins[t_omp_code].Index;
	return (Native_GOMP_Builtins[t_omp_code].TypeDescr);
}


/* Implement TARGET_EXPAND_BUILTIN.  */

static rtx
riscv_expand_builtin (tree exp, rtx target, rtx subtarget ATTRIBUTE_UNUSED,
		     enum machine_mode mode ATTRIBUTE_UNUSED,
		     int ignore ATTRIBUTE_UNUSED)
{
  tree fndecl;
  unsigned int fcode, avail, fcode_remapped;
  const struct riscv_builtin_description *d;

  fndecl = TREE_OPERAND (CALL_EXPR_FN (exp), 0);
  fcode = DECL_FUNCTION_CODE (fndecl);
  if (fcode >= ARRAY_SIZE (riscv_builtins)) {
  	fcode_remapped = GetRemappedGompBuiltin(fcode, ARRAY_SIZE (riscv_builtins));
  	if (fcode_remapped < ARRAY_SIZE (riscv_builtins)) fcode = fcode_remapped;
  }

  gcc_assert (fcode < ARRAY_SIZE (riscv_builtins));
  d = &riscv_builtins[fcode];
  avail = d->avail ();
  gcc_assert (avail != 0);
  switch (d->builtin_type)
    {
    case RISCV_BUILTIN_DIRECT:
      return riscv_expand_builtin_direct (d, fcode, d->icode, target, exp, true);

    case RISCV_BUILTIN_DIRECT_NO_TARGET:
      return riscv_expand_builtin_direct (d, fcode, d->icode, target, exp, false);
    }
  gcc_unreachable ();
}

/* Implement TARGET_ASM_OUTPUT_MI_THUNK.  Generate rtl rather than asm text
   in order to avoid duplicating too much logic from elsewhere.  */

static void
riscv_output_mi_thunk (FILE *file, tree thunk_fndecl ATTRIBUTE_UNUSED,
		      HOST_WIDE_INT delta, HOST_WIDE_INT vcall_offset,
		      tree function)
{
  rtx this_rtx, temp1, temp2, fnaddr;
  rtx_insn *insn;
  bool use_sibcall_p;

  /* Pretend to be a post-reload pass while generating rtl.  */
  reload_completed = 1;

  /* Mark the end of the (empty) prologue.  */
  emit_note (NOTE_INSN_PROLOGUE_END);

  /* Determine if we can use a sibcall to call FUNCTION directly.  */
  fnaddr = XEXP (DECL_RTL (function), 0);
  use_sibcall_p = absolute_symbolic_operand (fnaddr, Pmode);

  /* We need two temporary registers in some cases.  */
  temp1 = gen_rtx_REG (Pmode, GP_TEMP_FIRST);
  temp2 = gen_rtx_REG (Pmode, GP_TEMP_FIRST + 1);

  /* Find out which register contains the "this" pointer.  */
  if (aggregate_value_p (TREE_TYPE (TREE_TYPE (function)), function))
    this_rtx = gen_rtx_REG (Pmode, GP_ARG_FIRST + 1);
  else
    this_rtx = gen_rtx_REG (Pmode, GP_ARG_FIRST);

  /* Add DELTA to THIS_RTX.  */
  if (delta != 0)
    {
      rtx offset = GEN_INT (delta);
      if (!SMALL_OPERAND (delta))
	{
	  riscv_emit_move (temp1, offset);
	  offset = temp1;
	}
      emit_insn (gen_add3_insn (this_rtx, this_rtx, offset));
    }

  /* If needed, add *(*THIS_RTX + VCALL_OFFSET) to THIS_RTX.  */
  if (vcall_offset != 0)
    {
      rtx addr;

      /* Set TEMP1 to *THIS_RTX.  */
      riscv_emit_move (temp1, gen_rtx_MEM (Pmode, this_rtx));

      /* Set ADDR to a legitimate address for *THIS_RTX + VCALL_OFFSET.  */
      addr = riscv_add_offset (temp2, temp1, vcall_offset);

      /* Load the offset and add it to THIS_RTX.  */
      riscv_emit_move (temp1, gen_rtx_MEM (Pmode, addr));
      emit_insn (gen_add3_insn (this_rtx, this_rtx, temp1));
    }

  /* Jump to the target function.  Use a sibcall if direct jumps are
     allowed, otherwise load the address into a register first.  */
  if (use_sibcall_p)
    {
      insn = emit_call_insn (gen_sibcall_internal (fnaddr, const0_rtx));
      SIBLING_CALL_P (insn) = 1;
    }
  else
    {
      riscv_emit_move(temp1, fnaddr);
      emit_jump_insn (gen_indirect_jump (temp1));
    }

  /* Run just enough of rest_of_compilation.  This sequence was
     "borrowed" from alpha.c.  */
  insn = get_insns ();
  split_all_insns_noflow ();
  shorten_branches (insn);
  final_start_function (insn, file, 1);
  final (insn, file, 1);
  final_end_function ();

  /* Clean up the vars set above.  Note that final_end_function resets
     the global pointer for us.  */
  reload_completed = 0;
}

/* Allocate a chunk of memory for per-function machine-dependent data.  */

static struct machine_function *
riscv_init_machine_status (void)
{
  return ggc_cleared_alloc<machine_function> ();
}

/* Implement TARGET_OPTION_OVERRIDE.  */

static void
riscv_option_override (void)
{
  int regno, mode;
  const struct riscv_cpu_info *cpu;

#ifdef SUBTARGET_OVERRIDE_OPTIONS
  SUBTARGET_OVERRIDE_OPTIONS;
#endif

  flag_pcc_struct_return = 0;

  if (flag_pic)
    g_switch_value = 0;

  /* Prefer a call to memcpy over inline code when optimizing for size,
     though see MOVE_RATIO in riscv.h.  */
  if (optimize_size && (target_flags_explicit & MASK_MEMCPY) == 0)
    target_flags |= MASK_MEMCPY;

  /* Handle -mtune.  */
  cpu = riscv_parse_cpu (riscv_tune_string ? riscv_tune_string :
			 RISCV_TUNE_STRING_DEFAULT);
  tune_info = optimize_size ? &optimize_size_tune_info : cpu->tune_info;

  /* If the user hasn't specified a branch cost, use the processor's
     default.  */
  if (riscv_branch_cost == 0)
    riscv_branch_cost = tune_info->branch_cost;

  /* Set up riscv_hard_regno_mode_ok.  */
  for (mode = 0; mode < MAX_MACHINE_MODE; mode++)
    for (regno = 0; regno < FIRST_PSEUDO_REGISTER; regno++)
      riscv_hard_regno_mode_ok[mode][regno]
	= riscv_hard_regno_mode_ok_p (regno, (enum machine_mode) mode);

  /* Function to allocate machine-dependent function status.  */
  init_machine_status = &riscv_init_machine_status;

  if (riscv_cmodel_string)
    {
      if (strcmp (riscv_cmodel_string, "medlow") == 0)
	riscv_cmodel = CM_MEDLOW;
      else if (strcmp (riscv_cmodel_string, "medany") == 0)
	riscv_cmodel = CM_MEDANY;
      else
	error ("unsupported code model: %s", riscv_cmodel_string);
    }

  if (flag_pic)
    riscv_cmodel = CM_PIC;
  riscv_init_relocs ();
}

/* Implement TARGET_CONDITIONAL_REGISTER_USAGE.  */

static void
riscv_conditional_register_usage (void)
{
  int regno;

  if (!TARGET_HARD_FLOAT)
    {
      for (regno = FP_REG_FIRST; regno <= FP_REG_LAST; regno++)
	fixed_regs[regno] = call_used_regs[regno] = 1;
    }
}

/* Return a register priority for hard reg REGNO.  */
static int
riscv_register_priority (int regno)
{
  /* Favor x8-x15/f8-f15 to improve the odds of RVC instruction selection.  */
  if (TARGET_RVC && (IN_RANGE (regno, GP_REG_FIRST + 8, GP_REG_FIRST + 15)
		     || IN_RANGE (regno, FP_REG_FIRST + 8, FP_REG_FIRST + 15)))
    return 1;

  return 0;
}

/* Implement TARGET_TRAMPOLINE_INIT.  */

static void
riscv_trampoline_init (rtx m_tramp, tree fndecl, rtx chain_value)
{
  rtx addr, end_addr, mem;
  uint32_t trampoline[4];
  unsigned int i;
  HOST_WIDE_INT static_chain_offset, target_function_offset;

  /* Work out the offsets of the pointers from the start of the
     trampoline code.  */
  gcc_assert (ARRAY_SIZE (trampoline) * 4 == TRAMPOLINE_CODE_SIZE);
  static_chain_offset = TRAMPOLINE_CODE_SIZE;
  target_function_offset = static_chain_offset + GET_MODE_SIZE (ptr_mode);

  /* Get pointers to the beginning and end of the code block.  */
  addr = force_reg (Pmode, XEXP (m_tramp, 0));
  end_addr = riscv_force_binary (Pmode, PLUS, addr, GEN_INT (TRAMPOLINE_CODE_SIZE));

  /* auipc   t0, 0
     l[wd]   t1, target_function_offset(t0)
     l[wd]   t0, static_chain_offset(t0)
     jr      t1
  */
  trampoline[0] = OPCODE_AUIPC | (STATIC_CHAIN_REGNUM << SHIFT_RD);
  trampoline[1] = (Pmode == DImode ? OPCODE_LD : OPCODE_LW)
		  | (RISCV_PROLOGUE_TEMP_REGNUM << SHIFT_RD)
		  | (STATIC_CHAIN_REGNUM << SHIFT_RS1)
		  | (target_function_offset << SHIFT_IMM);
  trampoline[2] = (Pmode == DImode ? OPCODE_LD : OPCODE_LW)
		  | (STATIC_CHAIN_REGNUM << SHIFT_RD)
		  | (STATIC_CHAIN_REGNUM << SHIFT_RS1)
		  | (static_chain_offset << SHIFT_IMM);
  trampoline[3] = OPCODE_JALR | (RISCV_PROLOGUE_TEMP_REGNUM << SHIFT_RS1);

  /* Copy the trampoline code.  */
  for (i = 0; i < ARRAY_SIZE (trampoline); i++)
    {
      mem = adjust_address (m_tramp, SImode, i * GET_MODE_SIZE (SImode));
      riscv_emit_move (mem, gen_int_mode (trampoline[i], SImode));
    }

  /* Set up the static chain pointer field.  */
  mem = adjust_address (m_tramp, ptr_mode, static_chain_offset);
  riscv_emit_move (mem, chain_value);

  /* Set up the target function field.  */
  mem = adjust_address (m_tramp, ptr_mode, target_function_offset);
  riscv_emit_move (mem, XEXP (DECL_RTL (fndecl), 0));

  /* Flush the code part of the trampoline.  */
  emit_insn (gen_add3_insn (end_addr, addr, GEN_INT (TRAMPOLINE_SIZE)));
  emit_insn (gen_clear_cache (addr, end_addr));
}

/* Implement TARGET_FUNCTION_OK_FOR_SIBCALL.  */

static bool
riscv_function_ok_for_sibcall (tree decl ATTRIBUTE_UNUSED,
			       tree exp ATTRIBUTE_UNUSED)
{
  if (cfun->machine->is_interrupt) return false;
  if (TARGET_SAVE_RESTORE)
    {
      /* When optimzing for size, don't use sibcalls in non-leaf routines */
      if (cfun->machine->is_leaf == 0)
	cfun->machine->is_leaf = leaf_function_p () ? 1 : -1;

      return cfun->machine->is_leaf > 0;
    }

  return true;
}

static bool
riscv_lra_p (void)
{
  return riscv_lra_flag;
}

bool riscv_filter_pulp_operand(rtx x, bool ignore)

{
	if (ignore) return false;
	return ( ((GET_CODE(x) == MEM) &&
  		  (GET_CODE(XEXP(x, 0)) == POST_INC ||
		   GET_CODE(XEXP(x, 0)) == POST_DEC ||
		   GET_CODE(XEXP(x, 0)) == POST_MODIFY ||
   			(GET_CODE(XEXP(x, 0)) == PLUS &&
				(GET_CODE(XEXP(XEXP(x, 0), 1)) == REG || GET_CODE(XEXP(XEXP(x, 0), 1)) == SUBREG)
			)
  		  )
	         )
	       );
}


/* Hardware Loops */

#define MAX_LOOP_DEPTH 2

/* Maximum size of a loop.  */
#define MAX_LOOP_LENGTH 4096
#define MIN_LOOP_LENGTH 2

/* Maximum distance of the LSETUP instruction from the loop start.  */
#define MAX_LSETUP_DISTANCE 30

static const char *
riscv_invalid_within_doloop (const rtx_insn *insn)
{
  if (CALL_P (insn)) {
    cfun->machine->contains_call = 1;
    return "Function call in the loop.";
  }

  if (JUMP_P (insn) && INSN_CODE (insn) == CODE_FOR_return)
    return "Return from a call instruction in the loop.";

  return NULL;
}

static bool
riscv_can_use_doloop_p (const widest_int &, const widest_int &,
                      unsigned int loop_depth, bool)
{
        if ((Pulp_Cpu<PULP_V1) || TARGET_MASK_NOHWLOOP) return 0;

        return (loop_depth <= 2);
}


void riscv_hardware_loop (void)
{
  cfun->machine->has_hardware_loops++;
}

static int length_for_loop (rtx_insn *insn)
{
  int length = 0;

  if (NONDEBUG_INSN_P (insn)) length += (get_attr_length (insn))/4;
  return length;
}

/* Optimize LOOP.  */

static bool
hwloop_optimize (hwloop_info loop)
{
  basic_block bb;
  rtx_insn *insn, *last_insn, *insn_insert;
  rtx start_label, end_label;
  rtx iter_reg;
  rtx lc_reg, ls_reg, le_reg;
  bool clobber0, clobber1;
  rtx_insn *seq;
  rtx seq_end;
  int length;
  int loop_index;
  bool init_iter_is_constant = false;
  int init_iter_value = 0;
  rtx_insn *single_def_iter = NULL;
  bool single_def_iter_removable = false;
  bool Padding = false;
  bool UnsafeHead = false;

  if (dump_file) {
	edge e;
	int i;

	fprintf(dump_file, "Target specific processing of loop %d\n", loop->loop_no);
	fprintf(dump_file, "head         : bb%d\n", loop->head->index);
	fprintf(dump_file, "incoming_src : bb%d\n", loop->incoming_src?loop->incoming_src->index:-5555);
	fprintf(dump_file, "incoming_dest: bb%d\n", loop->incoming_dest?loop->incoming_dest->index:-5555);
	for (i = 0; vec_safe_iterate(loop->incoming, i, &e); i++) 
		fprintf(dump_file, " Incoming: src= bb%4d, dest= bb%4d, Edge is: %s\n", e->src->index, e->dest->index, (e->flags & EDGE_FALLTHRU)?"Fall Through":"Branch");
	
  }
  if (loop->depth > MAX_LOOP_DEPTH)
    {
      if (dump_file)
	fprintf (dump_file, ";; loop %d too deep\n", loop->loop_no);
      return false;
    }

  /* Get the loop iteration register.  */
  iter_reg = loop->iter_reg;

  gcc_assert (REG_P (iter_reg));

  if (loop->incoming_src) {
      if (!loop->incoming_dest || (loop->incoming_dest != loop->head)) {
         if (dump_file) fprintf (dump_file, ";; loop %d no incoming_src and no incoming_dest or incoming_dest != head\n", loop->loop_no);
         return false;
      }
  }
  if (!loop->incoming_src && (loop->incoming_dest != loop->head)) {
      if (dump_file) fprintf (dump_file, ";; loop %d no incoming_src and incoming_dest != head\n", loop->loop_no);
      return false;
  }
  if (loop->incoming_src)
    {
      /* Make sure the predecessor is before the loop start label, as required by
	 the LSETUP instruction.  */
      length = 0;
      insn = BB_END (loop->incoming_src);
      /* If we have to insert the LSETUP before a jump, count that jump in the
	 length.  */
      if (vec_safe_length (loop->incoming) > 1 || !(loop->incoming->last ()->flags & EDGE_FALLTHRU))
	{
	  gcc_assert (JUMP_P (insn));
	  insn = PREV_INSN (insn);
	  UnsafeHead = true;
	}

      for (; insn && insn != loop->start_label; insn = NEXT_INSN (insn)) length += length_for_loop (insn);

      if (!insn)
	{
	  if (dump_file)
	    fprintf (dump_file, ";; loop %d lsetup not before loop_start\n",
		     loop->loop_no);
	  return false;
	}

      if (length > MAX_LSETUP_DISTANCE)
	{
	  if (dump_file)
	    fprintf (dump_file, ";; loop %d lsetup too far away\n", loop->loop_no);
	  return false;
	}
    }

  /* Check if start_label appears before loop_end and calculate the
     offset between them.  We calculate the length of instructions
     conservatively.  */
  length = 0;
  for (insn = loop->start_label; insn && insn != loop->loop_end; insn = NEXT_INSN (insn)) {
/* */
    if (dump_file) {
	fprintf (dump_file, "Adding %d to loop length (%d) for insn\n", length_for_loop (insn), length);
	print_rtl_single (dump_file, insn);
    }
/* */
    length += length_for_loop (insn);
  }

  if (!insn)
    {
      if (dump_file)
	fprintf (dump_file, ";; loop %d start_label not before loop_end\n",
		 loop->loop_no);
      return false;
    }

  loop->length = length;
  if (loop->length > MAX_LOOP_LENGTH)
    {
      if (dump_file)
	fprintf (dump_file, ";; loop %d too long\n", loop->loop_no);
      return false;
    }

  /* Scan all the blocks to make sure they don't use iter_reg.  */
  if (loop->iter_reg_used || loop->iter_reg_used_outside)
    {
      if (dump_file)
	fprintf (dump_file, ";; loop %d uses iterator\n", loop->loop_no);
      return false;
    }

  clobber0 = (TEST_HARD_REG_BIT (loop->regs_set_in_loop, REG_LC0)
              || TEST_HARD_REG_BIT (loop->regs_set_in_loop, REG_LS0)
              || TEST_HARD_REG_BIT (loop->regs_set_in_loop, REG_LE0));
  clobber1 = (TEST_HARD_REG_BIT (loop->regs_set_in_loop, REG_LC1)
              || TEST_HARD_REG_BIT (loop->regs_set_in_loop, REG_LS1)
              || TEST_HARD_REG_BIT (loop->regs_set_in_loop, REG_LE1));
  if (clobber0 && clobber1)
    {
      if (dump_file)
        fprintf (dump_file, ";; loop %d no loop reg available\n",
                 loop->loop_no);
      return false;
    }

  /* There should be an instruction before the loop_end instruction
     in the same basic block. And the instruction must not be
     - JUMP
     - CONDITIONAL BRANCH
     - CALL
     - CSYNC
     - SSYNC
     - Returns (RTS, RTN, etc.)  */

  bb = loop->tail;
  last_insn = PREV_INSN (loop->loop_end);

  while (1)
    {
      for (; last_insn != BB_HEAD (bb); last_insn = PREV_INSN (last_insn)) {
		if (NONDEBUG_INSN_P (last_insn)) break;
		{
			/* Check if this insn could be a loop_end of an enclosed loop */
			hwloop_info i;
			unsigned ix;
			bool hit_enclosed_end_label=false;
			for (ix = 0; loop->loops.iterate (ix, &i); ix++) {
				if (i->end_label == last_insn) {
					hit_enclosed_end_label = true; break;
				}
			}
			if (hit_enclosed_end_label) {
				if (dump_file) {
					fprintf(dump_file, " Hitting enclose loop end label (enclosed=%d), insn:\n", i->loop_no);
					fprintf(dump_file, " Adding a nop after it\n");
					print_rtl_single (dump_file, last_insn);

				}
      				last_insn = emit_insn_after (gen_forced_nop (), last_insn);
				break;
			}
		}
      }

      if (last_insn != BB_HEAD (bb)) break;

      if (single_pred_p (bb)
	  && single_pred_edge (bb)->flags & EDGE_FALLTHRU
	  && single_pred (bb) != ENTRY_BLOCK_PTR_FOR_FN (cfun))
	{
	  bb = single_pred (bb);
	  last_insn = BB_END (bb);
	  continue;
	}
      else {
	  last_insn = NULL;
	  break;
	}
    }

  if (!last_insn) {
      if (dump_file) fprintf (dump_file, ";; loop %d has no last instruction\n", loop->loop_no);
      return false;
    }

  if (dump_file) {
	fprintf (dump_file, " Loop loop_end Inst is:\n");
	print_rtl_single (dump_file, loop->loop_end);
	fprintf (dump_file, " Loop Last Inst is:\n");
	print_rtl_single (dump_file, last_insn);
  }
  /* We check if last_inst can be the target of a branch, if yes add a nop after last_inst.
     Apply if BB(last_inst) != loop head since in this case the non fallthru edge is the loop back edge */
  if ((bb != loop->head) && (!single_pred_p(bb) || !(single_pred_edge (bb)->flags & EDGE_FALLTHRU))) {
	rtx_insn *pt;
	int cnt=0;

  	if (dump_file) {
		fprintf (dump_file, " Loop last BB (b%d) is the target of non fallthru branches\n", bb->index);
		fprintf (dump_file, " Single_pred: %s\n", single_pred_p(bb)?"Yes":"No");
		if (!single_pred_p(bb)) {
			fprintf (dump_file, " Single_pred_edge fallthru: %s\n", (single_pred_edge(bb)->flags & EDGE_FALLTHRU)?"Yes":"No");
		}
	}
	for (pt = BB_HEAD(bb); pt != last_insn; pt = NEXT_INSN (pt)) {
		if (NONDEBUG_INSN_P(pt)) cnt++;

 	}
	/* Could use a define containing the min number of inst that always have to be executed at loop tail */
	if (cnt == 0) {
  		if (dump_file) {
			fprintf (dump_file, " Branch to loop tail exist, adding a nop after last_insn\n");
		}
      		if (loop->length + 1 > MAX_LOOP_LENGTH) {
	  		if (dump_file) fprintf (dump_file, ";; loop %d too long\n", loop->loop_no);
	  		return false;
		} else loop->length += 1;
      		last_insn = emit_insn_after (gen_forced_nop (), last_insn);
	}
  }


  if (JUMP_P (last_insn) && !any_condjump_p (last_insn))
    {
      if (dump_file)
	fprintf (dump_file, ";; loop %d has bad last instruction\n",
		 loop->loop_no);
      return false;
    }
  /* In all other cases, try to replace a bad last insn with a nop.  */
  else if (JUMP_P (last_insn)
	   || CALL_P (last_insn)
	   || recog_memoized (last_insn) == CODE_FOR_simple_return_internal
	   || GET_CODE (PATTERN (last_insn)) == ASM_INPUT
	   || asm_noperands (PATTERN (last_insn)) >= 0) {
      	if (loop->length + 1 > MAX_LOOP_LENGTH) {
	  	if (dump_file) fprintf (dump_file, ";; loop %d too long\n", loop->loop_no);
	  	return false;
	} else loop->length += 1;
      	if (dump_file) {
		fprintf (dump_file, ";; loop %d has bad last insn; replace with nop\n", loop->loop_no);
		fprintf (dump_file, " Loop Last Inst is:\n");
		print_rtl_single (dump_file, last_insn);
	}

      	last_insn = emit_insn_after (gen_forced_nop (), last_insn);
    }

  if (loop->length < MIN_LOOP_LENGTH && TARGET_MASK_SLOOP) {
	Padding = true;
  } else {
  	while (loop->length < MIN_LOOP_LENGTH) {
      		last_insn = emit_insn_after (gen_forced_nop (), last_insn);
		loop->length += 1;
  	}
  }
  loop->last_insn = last_insn;

  /* The loop is good for replacement.  */
  start_label = loop->start_label;
  end_label = gen_label_rtx ();
  iter_reg = loop->iter_reg;
  // scratch_reg = gen_reg_rtx (SImode);

  loop->end_label = end_label;

  /* Create a sequence containing the loop setup.  */

	if (loop->depth == 1 && !clobber1) {
		loop_index = 1;
		lc_reg = gen_rtx_REG (SImode, REG_LC1);
		ls_reg = gen_rtx_REG (SImode, REG_LS1);
		le_reg = gen_rtx_REG (SImode, REG_LE1);
		SET_HARD_REG_BIT (loop->regs_set_in_loop, REG_LC1);
	} else {
		loop_index = 0;
		lc_reg = gen_rtx_REG (SImode, REG_LC0);
		ls_reg = gen_rtx_REG (SImode, REG_LS0);
		le_reg = gen_rtx_REG (SImode, REG_LE0);
		SET_HARD_REG_BIT (loop->regs_set_in_loop, REG_LC0);
	}

	{
		df_ref *use_rec;
		struct df_link *defs;

		if (dump_file) {
			fprintf (dump_file, "------- Processing loop end insn uses ---\n");
			print_rtl_single (dump_file, loop->loop_end);
		}
  		for (use_rec = &DF_INSN_USES(loop->loop_end); *use_rec; use_rec++) {
			df_ref use = *use_rec;
			if (dump_file) {
				fprintf (dump_file, "+++++ Processing use of\n");
				print_rtl_single_with_indent (dump_file, DF_REF_REG (use), 4);
				fprintf (dump_file, " in insn %d:\n", INSN_UID (loop->loop_end));
			}
			if (!rtx_equal_p(iter_reg, DF_REF_REG(use))) {
				if (dump_file) {
					fprintf (dump_file, "   Use is not iter reg, continue\n");
					fprintf (dump_file, "++++ End of process use\n");
				}
				continue;
			}
			for (defs = DF_REF_CHAIN (use); defs; defs = defs->next)
				if (! DF_REF_IS_ARTIFICIAL (defs->ref)) {
					rtx_insn *def_insn = DF_REF_INSN(defs->ref);

					if (dump_file) {
						fprintf (dump_file, " Use defined by insn\n");
						print_rtl_single_with_indent (dump_file, DF_REF_INSN (defs->ref), 8);
					}
					if (def_insn != loop->loop_end) {
						if (!single_def_iter) single_def_iter = def_insn;
						else if (single_def_iter != def_insn) {
							single_def_iter = NULL; break;
						}
					}
					
				}
			if (!single_def_iter) {
				if (dump_file) {
					fprintf (dump_file, "    Aborting insn uses parsing, multiple defs of iter_reg found\n");
					fprintf (dump_file, "++++ End of process use\n");
				}
				break;
			} else if (dump_file) fprintf (dump_file, "++++ End of process use\n");
		}
		if (single_def_iter) {
			int iter_count = -1;
			int multiple_use = 0;
			 df_ref *def_rec;

			// rtx set = SET_SRC(PATTERN(single_def_iter));
			// rtx dst = SET_DEST(PATTERN(single_def_iter));

			if (GET_CODE(PATTERN(single_def_iter)) == SET && GET_CODE(SET_SRC(PATTERN(single_def_iter))) == CONST_INT) {
				iter_count = INTVAL(SET_SRC(PATTERN(single_def_iter)));
				init_iter_is_constant = true;
				init_iter_value = iter_count;
				if (dump_file) {
					fprintf (dump_file, " Iter reg is defined once and only once:\n");
					print_rtl_single_with_indent (dump_file, single_def_iter, 4);
					fprintf (dump_file, "\n Iter count is: %d\n", iter_count);
				}
			} else {
				if (dump_file) {
					fprintf (dump_file, " Iter reg is defined once and only once but not as constant:\n");
					print_rtl_single_with_indent (dump_file, single_def_iter, 4);
				}
			}
			if (init_iter_is_constant) {
				for (def_rec = &DF_INSN_DEFS (insn); *def_rec; def_rec++) {
					if (dump_file) {
						fprintf (dump_file, "  Init iter output is used by insn:\n");
						print_rtl_single_with_indent (dump_file, DF_REF_INSN(*def_rec), 4);
					}
					if (DF_REF_INSN(*def_rec) != loop->loop_end) {
						multiple_use = 1;
						if (dump_file) {
							fprintf (dump_file, "  Cannot remove Init, used outside loop exit:\n");
							print_rtl_single_with_indent (dump_file, DF_REF_INSN(*def_rec), 4);
						}
					}
				}
				if (!multiple_use) {
					rtx dst = SET_DEST(PATTERN(single_def_iter));
					if (reg_used_between_p(dst, single_def_iter, NEXT_INSN(BB_END(BLOCK_FOR_INSN(single_def_iter))))) {
						if (dump_file) fprintf (dump_file, "\n Found Init use after single_def_iter in it's BB\n");
					} else {
						single_def_iter_removable = true;
						if (dump_file) fprintf (dump_file, "\n Init can be removed\n");
					}
				}
			}
		}
		if (dump_file) fprintf (dump_file, "------- END OF Processing loop end insn uses ---\n");
	}

	if (dump_file) {
		fprintf (dump_file, "  Loop Length is %d\n", loop->length);
		fprintf (dump_file, "  single_def_iter_removable=%s\n", single_def_iter_removable?"yes":"no");
		fprintf (dump_file, "  init_iter_is_constant=%s, %d\n", init_iter_is_constant?"yes":"no", init_iter_value);
		fprintf (dump_file, "  Padding=%s\n", Padding?"yes":"no");

	}
 	start_sequence ();
	if (loop->length > 65535 || Padding) {
		/* Use long form:
			lp.count level, iter_reg
			lp.start level, start_label
			lp.end level, end_label

			if immediate(iter_reg) and if 0 <= imm_value <= 2047
				we can use lp.counti level, imm_value
		*/
		if (init_iter_is_constant && (init_iter_value < 4095))
			seq_end = emit_insn(gen_set_hwloop_lc  (lc_reg, gen_int_mode(init_iter_value, SImode), gen_int_mode (loop_index, SImode)));
		else
			seq_end = emit_insn(gen_set_hwloop_lc  (lc_reg, iter_reg,                              gen_int_mode (loop_index, SImode)));
		emit_insn(gen_set_hwloop_lpstart(ls_reg, gen_rtx_LABEL_REF (Pmode, start_label), gen_int_mode (loop_index, SImode)));
		emit_insn(gen_set_hwloop_lpend  (le_reg, gen_rtx_LABEL_REF (Pmode,   end_label), gen_int_mode (loop_index, SImode)));

	} else if (loop->length > 15 || !init_iter_is_constant || (init_iter_is_constant && (init_iter_value >= 4096)) ) {
		/* Use short form:
			lp.count level, end_label, iter_reg
		*/
		single_def_iter_removable = false;
  		seq_end = emit_insn(gen_set_hwloop_lc_le(lc_reg, iter_reg, le_reg,
							 gen_rtx_LABEL_REF (Pmode, end_label),
							 gen_int_mode (loop_index, SImode)));
	} else {
		/* Use short form:
			lp.counti level, end_label, iter_reg
			
			immediate(iter_reg) and 0 <= imm_value <= 2047 and loop->length <= 31
				we can use lp.counti level, loop_end, imm_value
		*/
	  	gcc_assert (loop->length <= 16 && init_iter_is_constant && (init_iter_value <= 4095));
  		seq_end = emit_insn(gen_set_hwloop_lc_le(lc_reg,
						         gen_int_mode(init_iter_value, SImode),
							 le_reg,
							 gen_rtx_LABEL_REF (Pmode, end_label),
							 gen_int_mode (loop_index, SImode)));

	}
	insn_insert = (BB_HEAD (loop->head));

  if (dump_file)
    {
      fprintf (dump_file, ";; replacing loop %d initializer with\n",
	       loop->loop_no);
      print_rtl_single (dump_file, seq_end);
      fprintf (dump_file, ";; replacing loop %d terminator with\n",
	       loop->loop_no);
      print_rtl_single (dump_file, loop->loop_end);
    }

  /* If the loop isn't entered at the top, also create a jump to the entry
     point.  */
  if (!loop->incoming_src && loop->head != loop->incoming_dest)
    {
      rtx label = BB_HEAD (loop->incoming_dest);
      /* If we're jumping to the final basic block in the loop, and there's
	 only one cheap instruction before the end (typically an increment of
	 an induction variable), we can just emit a copy here instead of a
	 jump.  */
/*
      if (loop->incoming_dest == loop->tail
	  && next_real_insn (label) == last_insn
	  && asm_noperands (last_insn) < 0
	  && GET_CODE (PATTERN (last_insn)) == SET)
	{
	  seq_end = emit_insn (copy_rtx (PATTERN (last_insn)));
	}
      else
*/
	{
	  emit_jump_insn (gen_jump (label));
	  seq_end = emit_barrier ();
	}
    }

  seq = get_insns ();
  end_sequence ();

  if (loop->incoming_src)
    {
	if (UnsafeHead) {
      		basic_block new_bb;
      		edge e;
      		edge_iterator ei;
		if (dump_file) {
			fprintf(dump_file, "Loop %d has unsafe head, creating new BB for loop init and redirecting incoming to it.\n", loop->loop_no);
		}

      		emit_insn_before (seq, insn_insert);
      		seq = emit_label_before (gen_label_rtx (), seq);
      		new_bb = create_basic_block (seq, seq_end, loop->head->prev_bb);
      		FOR_EACH_EDGE (e, ei, loop->incoming) {
	  		if (!(e->flags & EDGE_FALLTHRU) || e->dest != loop->head)
	    			redirect_edge_and_branch_force (e, new_bb);
	  		else redirect_edge_succ (e, new_bb);
		}
      		e = make_edge (new_bb, loop->head, 0);
	} else emit_insn_before (seq, insn_insert);
/*
      rtx prev = BB_END (loop->incoming_src);
      if (vec_safe_length (loop->incoming) > 1 || !(loop->incoming->last ()->flags & EDGE_FALLTHRU)) {
	  gcc_assert (JUMP_P (prev));
	  prev = PREV_INSN (prev);
      }
      emit_insn_after (seq, prev);
*/
    }
  else
    {
      basic_block new_bb;
      edge e;
      edge_iterator ei;

#ifdef ENABLE_CHECKING
      if (loop->head != loop->incoming_dest)
	{
	  /* We aren't entering the loop at the top.  Since we've established
	     that the loop is entered only at one point, this means there
	     can't be fallthru edges into the head.  Any such fallthru edges
	     would become invalid when we insert the new block, so verify
	     that this does not in fact happen.  */
	  FOR_EACH_EDGE (e, ei, loop->head->preds)
	    gcc_assert (!(e->flags & EDGE_FALLTHRU));
	}
#endif

      /* emit_insn_before (seq, BB_HEAD (loop->head)); */
      emit_insn_before (seq, insn_insert);
      seq = emit_label_before (gen_label_rtx (), seq);

      new_bb = create_basic_block (seq, seq_end, loop->head->prev_bb);
      FOR_EACH_EDGE (e, ei, loop->incoming)
	{
	  if (!(e->flags & EDGE_FALLTHRU)
	      || e->dest != loop->head)
	    redirect_edge_and_branch_force (e, new_bb);
	  else
	    redirect_edge_succ (e, new_bb);
	}
      e = make_edge (new_bb, loop->head, 0);
    }

  if (single_def_iter_removable) delete_insn (single_def_iter);
  // delete_insn (loop->loop_end);
  /* Insert the loop end label before the last instruction of the loop.  */
  /* BUG RiscV, hwloop hw messes up the loop last inst and consider as last the inst right after the last
     so we emit after and not before */
  emit_label_before (loop->end_label, loop->last_insn);

  return true;
}

/* A callback for the hw-doloop pass.  Called when a loop we have discovered
   turns out not to be optimizable; we have to split the doloop_end pattern
   into a subtract and a test.  */
static void
hwloop_fail (hwloop_info loop)
{
      rtx insn, test;
      rtx jmp_label;

      emit_insn_before (gen_addsi3 (loop->iter_reg,
				    loop->iter_reg,
				    constm1_rtx),
			loop->loop_end);

      jmp_label = JUMP_LABEL(loop->loop_end);
      if (jmp_label != loop->start_label) {
	if (dump_file) {
		fprintf(dump_file, "Loop %d is failing and branch_target(loop_end) != Start_Label(Loop)\n", loop->loop_no);
	}
      }
      test = gen_rtx_NE (VOIDmode, loop->iter_reg, const0_rtx);
      insn = emit_jump_insn_before (gen_cbranchsi4 (test,
						    loop->iter_reg, const0_rtx,
						    jmp_label),
				    loop->loop_end);

      JUMP_LABEL (insn) = jmp_label; // loop->start_label;
      LABEL_NUSES (jmp_label)++;
      // LABEL_NUSES (loop->start_label)++;
      delete_insn (loop->loop_end);
}

/* A callback for the hw-doloop pass.  This function examines INSN; if
   it is a loop_end pattern we recognize, return the reg rtx for the
   loop counter.  Otherwise, return NULL_RTX.  */

static rtx
hwloop_pattern_reg (rtx_insn *insn)
{
  rtx reg;

  if ((Pulp_Cpu<PULP_V1) || TARGET_MASK_NOHWLOOP || !JUMP_P (insn) || recog_memoized (insn) != CODE_FOR_loop_end)
    return NULL_RTX;

  reg = SET_DEST (XVECEXP (PATTERN (insn), 0, 1));
  if (!REG_P (reg))
    return NULL_RTX;
  return reg;
}

static struct hw_doloop_hooks riscv_doloop_hooks =
{
  hwloop_pattern_reg,
  hwloop_optimize,
  hwloop_fail
};

/* Run from machine_dependent_reorg, this pass looks for doloop_end insns
   and tries to rewrite the RTL of these loops so that proper Blackfin
   hardware loops are generated.  */

static void
riscv_reorg_loops (void)
{
  df_chain_add_problem (DF_UD_CHAIN + DF_DU_CHAIN);
  df_analyze ();

  reorg_loops (false, &riscv_doloop_hooks);

  df_live_add_problem ();
  df_live_set_all_dirty ();
  df_analyze ();
}

static void
riscv_reorg (void)
{
  /* We are freeing block_for_insn in the toplev to keep compatibility
     with old MDEP_REORGS that are not CFG based.  Recompute it now.  */
  compute_bb_for_insn ();

  df_analyze ();

  /* Doloop optimization */
  if (cfun->machine->has_hardware_loops) riscv_reorg_loops ();

  df_finish_pass (false);
}

typedef struct GTY(()) import_export_symbol
{
  tree decl;
  const char *name;
} import_export_symbol;

/* Define gc'd vector type for import_export_symbol.  */

/* Vector of import_export_symbol pointers.  */
static GTY(()) vec<import_export_symbol, va_gc> *import_symbols;
static GTY(()) vec<import_export_symbol, va_gc> *export_symbols;

static void riscv_globalize_decl_name (FILE * stream, tree decl)

{
	if (TREE_CODE(decl) == VAR_DECL) {
		tree attrs;
		const char *name = XSTR (XEXP (DECL_RTL (decl), 0), 0);
		attrs = DECL_ATTRIBUTES (decl);
     		if ((attrs && lookup_attribute ("export_var", attrs))) {
			import_export_symbol p = {decl, name};
			// printf("Found Var Decl export on %s\n", name);
  			vec_safe_push (export_symbols, p);
		}
	}
	default_globalize_decl_name(stream, decl);
}

void riscv_output_external (FILE *file, tree decl, const char *name)

{
	gcc_assert (file == asm_out_file);
	import_export_symbol p = {decl, name};
	tree attrs;

  	if (decl) {

		attrs = DECL_ATTRIBUTES (decl);
     		if ((attrs && lookup_attribute ("import_var", attrs))) {
			// printf("Found Var Decl import on %s\n", name);
  			vec_safe_push (import_symbols, p);
		}
/*
     		if ((attrs && lookup_attribute ("export_var", attrs))) {
			printf("Found Var Decl export on %s\n", name);
		}
*/

		attrs = TYPE_ATTRIBUTES (TREE_TYPE(decl));
		if ((attrs && lookup_attribute ("import", attrs))) {
			// printf("Found Func import on %s\n", name);
  			vec_safe_push (import_symbols, p);
		}
		if ((attrs && lookup_attribute ("export" ,attrs))) {
			// printf("Found Func export on %s\n", name);
  			vec_safe_push (export_symbols, p);
		}
  	}
}

static void riscv_file_end (void)

{
	static int InImportSection=0;
	static int InExportSection=0;
	unsigned int i;
	import_export_symbol *p;

	for (i = 0; vec_safe_iterate (import_symbols, i, &p); i++) {
		tree decl = p->decl;
		if (!InImportSection) {
			fprintf(asm_out_file, "\n\t.section\t.pulp.import,\"aw\",@note\n");
			InImportSection = 1;
		}
		if (SYMBOL_REF_REFERENCED_P (XEXP (DECL_RTL (decl), 0))) {
			fprintf(asm_out_file, "\t.weak\t%s\n", p->name);
			fprintf(asm_out_file, "\t.type\t%s, @function\n", p->name);
			fprintf(asm_out_file, "\t.size\t%s, 4\n", p->name);
			fprintf(asm_out_file, "%s:\n", p->name);
			fprintf(asm_out_file, "\t.zero\t4\n");
		}
	}
	for (i = 0; vec_safe_iterate (export_symbols, i, &p); i++) {
		tree decl = p->decl;
		if (!InExportSection) {
			fprintf(asm_out_file, "\n\t.section\t.pulp.export,\"\",@note\n");
			InExportSection = 1;
		}
		fprintf(asm_out_file, "\t.string \"%s\"\n", p->name);
	}
	if (InImportSection) {
		fprintf(asm_out_file, "\t.section\t.pulp.import.names,\"\",@note\n");
		fprintf(asm_out_file, "\t.align\t2\n");
		fprintf(asm_out_file, "\t.string \"N\"\n");
		fprintf(asm_out_file, "\t.section\t.pulp.import.relocs,\"\",@note\n");
		fprintf(asm_out_file, "\t.align\t2\n");
		fprintf(asm_out_file, "\t.string \"R\"\n");
	}
	vec_free (import_symbols);
	vec_free (export_symbols);

}





/* Initialize the GCC target structure.  */
#undef TARGET_ASM_ALIGNED_HI_OP
#define TARGET_ASM_ALIGNED_HI_OP "\t.half\t"
#undef TARGET_ASM_ALIGNED_SI_OP
#define TARGET_ASM_ALIGNED_SI_OP "\t.word\t"
#undef TARGET_ASM_ALIGNED_DI_OP
#define TARGET_ASM_ALIGNED_DI_OP "\t.dword\t"

#undef TARGET_OPTION_OVERRIDE
#define TARGET_OPTION_OVERRIDE riscv_option_override

#undef TARGET_LEGITIMIZE_ADDRESS
#define TARGET_LEGITIMIZE_ADDRESS riscv_legitimize_address

#undef TARGET_SCHED_ADJUST_COST
#define TARGET_SCHED_ADJUST_COST riscv_adjust_cost
#undef TARGET_SCHED_ISSUE_RATE
#define TARGET_SCHED_ISSUE_RATE riscv_issue_rate

#undef TARGET_FUNCTION_OK_FOR_SIBCALL
#define TARGET_FUNCTION_OK_FOR_SIBCALL riscv_function_ok_for_sibcall

#undef TARGET_REGISTER_MOVE_COST
#define TARGET_REGISTER_MOVE_COST riscv_register_move_cost
#undef TARGET_MEMORY_MOVE_COST
#define TARGET_MEMORY_MOVE_COST riscv_memory_move_cost
#undef TARGET_RTX_COSTS
#define TARGET_RTX_COSTS riscv_rtx_costs
#undef TARGET_ADDRESS_COST
#define TARGET_ADDRESS_COST riscv_address_cost

#undef  TARGET_PREFERRED_RELOAD_CLASS
#define TARGET_PREFERRED_RELOAD_CLASS riscv_preferred_reload_class

#undef TARGET_ASM_FILE_START_FILE_DIRECTIVE
#define TARGET_ASM_FILE_START_FILE_DIRECTIVE true

#undef TARGET_EXPAND_BUILTIN_VA_START
#define TARGET_EXPAND_BUILTIN_VA_START riscv_va_start

#undef  TARGET_PROMOTE_FUNCTION_MODE
#define TARGET_PROMOTE_FUNCTION_MODE default_promote_function_mode_always_promote

#undef TARGET_RETURN_IN_MEMORY
#define TARGET_RETURN_IN_MEMORY riscv_return_in_memory

#undef  TARGET_SET_CURRENT_FUNCTION
#define TARGET_SET_CURRENT_FUNCTION riscv_set_current_function

#undef TARGET_ASM_OUTPUT_MI_THUNK
#define TARGET_ASM_OUTPUT_MI_THUNK riscv_output_mi_thunk
#undef TARGET_ASM_CAN_OUTPUT_MI_THUNK
#define TARGET_ASM_CAN_OUTPUT_MI_THUNK hook_bool_const_tree_hwi_hwi_const_tree_true

#undef TARGET_PRINT_OPERAND
#define TARGET_PRINT_OPERAND riscv_print_operand
#undef TARGET_PRINT_OPERAND_ADDRESS
#define TARGET_PRINT_OPERAND_ADDRESS riscv_print_operand_address

#undef TARGET_SETUP_INCOMING_VARARGS
#define TARGET_SETUP_INCOMING_VARARGS riscv_setup_incoming_varargs
#undef TARGET_STRICT_ARGUMENT_NAMING
#define TARGET_STRICT_ARGUMENT_NAMING hook_bool_CUMULATIVE_ARGS_true
#undef TARGET_MUST_PASS_IN_STACK
#define TARGET_MUST_PASS_IN_STACK must_pass_in_stack_var_size
#undef TARGET_PASS_BY_REFERENCE
#define TARGET_PASS_BY_REFERENCE riscv_pass_by_reference
#undef TARGET_ARG_PARTIAL_BYTES
#define TARGET_ARG_PARTIAL_BYTES riscv_arg_partial_bytes
#undef TARGET_FUNCTION_ARG
#define TARGET_FUNCTION_ARG riscv_function_arg
#undef TARGET_FUNCTION_ARG_ADVANCE
#define TARGET_FUNCTION_ARG_ADVANCE riscv_function_arg_advance
#undef TARGET_FUNCTION_ARG_BOUNDARY
#define TARGET_FUNCTION_ARG_BOUNDARY riscv_function_arg_boundary

#undef TARGET_MODE_REP_EXTENDED
#define TARGET_MODE_REP_EXTENDED riscv_mode_rep_extended

#undef TARGET_SCALAR_MODE_SUPPORTED_P
#define TARGET_SCALAR_MODE_SUPPORTED_P riscv_scalar_mode_supported_p

#undef TARGET_INIT_BUILTINS
#define TARGET_INIT_BUILTINS riscv_init_builtins
#undef TARGET_BUILTIN_DECL
#define TARGET_BUILTIN_DECL riscv_builtin_decl
#undef TARGET_EXPAND_BUILTIN
#define TARGET_EXPAND_BUILTIN riscv_expand_builtin

#undef  TARGET_FOLD_BUILTIN
#define TARGET_FOLD_BUILTIN riscv_fold_builtin

#undef TARGET_REMAPPED_BUILTIN
#define TARGET_REMAPPED_BUILTIN riscv_remapped_builtin

#undef TARGET_OMP_TARGET_DECL
#define TARGET_OMP_TARGET_DECL riscv_omp_target_decl

#undef TARGET_HAVE_TLS
#define TARGET_HAVE_TLS HAVE_AS_TLS

#undef TARGET_CANNOT_FORCE_CONST_MEM
#define TARGET_CANNOT_FORCE_CONST_MEM riscv_cannot_force_const_mem

#undef TARGET_LEGITIMATE_CONSTANT_P
#define TARGET_LEGITIMATE_CONSTANT_P riscv_legitimate_constant_p

#undef TARGET_USE_BLOCKS_FOR_CONSTANT_P
#define TARGET_USE_BLOCKS_FOR_CONSTANT_P hook_bool_mode_const_rtx_true

#ifdef HAVE_AS_DTPRELWORD
#undef TARGET_ASM_OUTPUT_DWARF_DTPREL
#define TARGET_ASM_OUTPUT_DWARF_DTPREL riscv_output_dwarf_dtprel
#endif

#undef TARGET_LEGITIMATE_ADDRESS_P
#define TARGET_LEGITIMATE_ADDRESS_P	riscv_legitimate_address_p

#undef TARGET_CAN_ELIMINATE
#define TARGET_CAN_ELIMINATE riscv_can_eliminate

#undef TARGET_CONDITIONAL_REGISTER_USAGE
#define TARGET_CONDITIONAL_REGISTER_USAGE riscv_conditional_register_usage

#undef TARGET_TRAMPOLINE_INIT
#define TARGET_TRAMPOLINE_INIT riscv_trampoline_init

#undef TARGET_IN_SMALL_DATA_P
#define TARGET_IN_SMALL_DATA_P riscv_in_small_data_p

#undef TARGET_USE_ANCHORS_FOR_SYMBOL_P
#define TARGET_USE_ANCHORS_FOR_SYMBOL_P riscv_use_anchors_for_symbol_p

#undef TARGET_ASM_SELECT_RTX_SECTION
#define TARGET_ASM_SELECT_RTX_SECTION  riscv_elf_select_rtx_section

#undef TARGET_MIN_ANCHOR_OFFSET
#define TARGET_MIN_ANCHOR_OFFSET (-IMM_REACH/2)

#undef TARGET_MAX_ANCHOR_OFFSET
#define TARGET_MAX_ANCHOR_OFFSET (IMM_REACH/2-1)

#undef TARGET_LRA_P
#define TARGET_LRA_P riscv_lra_p

#undef TARGET_REGISTER_PRIORITY
#define TARGET_REGISTER_PRIORITY riscv_register_priority

#undef  TARGET_CAN_USE_DOLOOP_P
#define TARGET_CAN_USE_DOLOOP_P riscv_can_use_doloop_p

#undef TARGET_INVALID_WITHIN_DOLOOP
#define TARGET_INVALID_WITHIN_DOLOOP riscv_invalid_within_doloop

#undef  TARGET_MACHINE_DEPENDENT_REORG
#define TARGET_MACHINE_DEPENDENT_REORG riscv_reorg

#undef TARGET_VECTOR_MODE_SUPPORTED_P
#define TARGET_VECTOR_MODE_SUPPORTED_P riscv_vector_mode_supported_p

#undef TARGET_VECTORIZE_PREFERRED_SIMD_MODE
#define TARGET_VECTORIZE_PREFERRED_SIMD_MODE riscv_preferred_simd_mode

#undef TARGET_VECTORIZE_SUPPORT_VECTOR_MISALIGNMENT
#define TARGET_VECTORIZE_SUPPORT_VECTOR_MISALIGNMENT riscv_builtin_support_vector_misalignment

#undef TARGET_VECTORIZE_VECTOR_ALIGNMENT_REACHABLE
#define TARGET_VECTORIZE_VECTOR_ALIGNMENT_REACHABLE riscv_vector_alignment_reachable


#undef TARGET_VECTORIZE_BUILTIN_VECTORIZATION_COST
#define TARGET_VECTORIZE_BUILTIN_VECTORIZATION_COST riscv_builtin_vectorization_cost

#undef TARGET_ASM_FILE_END
#define TARGET_ASM_FILE_END riscv_file_end

#undef TARGET_ASM_GLOBALIZE_DECL_NAME
#define TARGET_ASM_GLOBALIZE_DECL_NAME riscv_globalize_decl_name




struct gcc_target targetm = TARGET_INITIALIZER;

#include "gt-riscv.h"
