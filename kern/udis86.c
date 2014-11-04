/* udis86
 *
 * Copyright (c) 2002-2009 Vivek Thampi
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met:
 * 
 *     * Redistributions of source code must retain the above copyright notice, 
 *       this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright notice, 
 *       this list of conditions and the following disclaimer in the documentation 
 *       and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR 
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES 
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; 
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON 
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS 
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "kern/udis86.h"

/* BEGIN decode.h */

#define MAX_INSN_LENGTH 15

/* itab prefix bits */
#define P_none          ( 0 )
#define P_cast          ( 1 << 0 )
#define P_CAST(n)       ( ( n >> 0 ) & 1 )
#define P_rexb          ( 1 << 1 )
#define P_REXB(n)       ( ( n >> 1 ) & 1 )
#define P_inv64         ( 1 << 4 )
#define P_INV64(n)      ( ( n >> 4 ) & 1 )
#define P_rexw          ( 1 << 5 )
#define P_REXW(n)       ( ( n >> 5 ) & 1 )
#define P_def64         ( 1 << 7 )
#define P_DEF64(n)      ( ( n >> 7 ) & 1 )
#define P_rexr          ( 1 << 8 )
#define P_REXR(n)       ( ( n >> 8 ) & 1 )
#define P_oso           ( 1 << 9 )
#define P_OSO(n)        ( ( n >> 9 ) & 1 )
#define P_aso           ( 1 << 10 )
#define P_ASO(n)        ( ( n >> 10 ) & 1 )
#define P_rexx          ( 1 << 11 )
#define P_REXX(n)       ( ( n >> 11 ) & 1 )
#define P_ImpAddr       ( 1 << 12 )
#define P_IMPADDR(n)    ( ( n >> 12 ) & 1 )
#define P_seg           ( 1 << 13 )
#define P_SEG(n)        ( ( n >> 13 ) & 1 )
#define P_str           ( 1 << 14 )
#define P_STR(n)        ( ( n >> 14 ) & 1 )
#define P_strz          ( 1 << 15 )
#define P_STR_ZF(n)     ( ( n >> 15 ) & 1 )

/* operand type constants -- order is important! */

enum ud_operand_code {
    OP_NONE,

    OP_A,      OP_E,      OP_M,       OP_G,       
    OP_I,      OP_F,

    OP_R0,     OP_R1,     OP_R2,      OP_R3,
    OP_R4,     OP_R5,     OP_R6,      OP_R7,

    OP_AL,     OP_CL,     OP_DL,
    OP_AX,     OP_CX,     OP_DX,
    OP_eAX,    OP_eCX,    OP_eDX,
    OP_rAX,    OP_rCX,    OP_rDX,

    OP_ES,     OP_CS,     OP_SS,      OP_DS,  
    OP_FS,     OP_GS,

    OP_ST0,    OP_ST1,    OP_ST2,     OP_ST3,
    OP_ST4,    OP_ST5,    OP_ST6,     OP_ST7,

    OP_J,      OP_S,      OP_O,          
    OP_I1,     OP_I3,     OP_sI,

    OP_V,      OP_W,      OP_Q,       OP_P, 
    OP_U,      OP_N,      OP_MU,

    OP_R,      OP_C,      OP_D,       

    OP_MR
} UD_ATTR_PACKED;


/* operand size constants */

enum ud_operand_size {
    SZ_NA  = 0,
    SZ_Z   = 1,
    SZ_V   = 2,
    SZ_RDQ = 7,

    /* the following values are used as is,
     * and thus hard-coded. changing them 
     * will break internals 
     */
    SZ_B   = 8,
    SZ_W   = 16,
    SZ_D   = 32,
    SZ_Q   = 64,
    SZ_T   = 80,
    SZ_O   = 128,

    SZ_Y   = 17,

    /*
     * complex size types, that encode sizes for operands
     * of type MR (memory or register), for internal use
     * only. Id space 256 and above.
     */
    SZ_BD  = (SZ_B << 8) | SZ_D,
    SZ_BV  = (SZ_B << 8) | SZ_V,
    SZ_WD  = (SZ_W << 8) | SZ_D,
    SZ_WV  = (SZ_W << 8) | SZ_V,
    SZ_WY  = (SZ_W << 8) | SZ_Y,
    SZ_DY  = (SZ_D << 8) | SZ_Y,
    SZ_WO  = (SZ_W << 8) | SZ_O,
    SZ_DO  = (SZ_D << 8) | SZ_O,
    SZ_QO  = (SZ_Q << 8) | SZ_O,

} UD_ATTR_PACKED;


/* resolve complex size type.
 */
static inline enum ud_operand_size
Mx_mem_size(enum ud_operand_size size)
{
    return (size >> 8) & 0xff;
}

static inline enum ud_operand_size
Mx_reg_size(enum ud_operand_size size)
{
    return size & 0xff;
}

/* A single operand of an entry in the instruction table. 
 * (internal use only)
 */
struct ud_itab_entry_operand 
{
  enum ud_operand_code type;
  enum ud_operand_size size;
};


/* A single entry in an instruction table. 
 *(internal use only)
 */
struct ud_itab_entry 
{
  enum ud_mnemonic_code         mnemonic;
  struct ud_itab_entry_operand  operand1;
  struct ud_itab_entry_operand  operand2;
  struct ud_itab_entry_operand  operand3;
  uint32_t                      prefix;
};

struct ud_lookup_table_list_entry {
    const uint16_t *table;
    enum ud_table_type type;
    const char *meta;
};
     


static inline int
ud_opcode_field_sext(uint8_t primary_opcode)
{
  return (primary_opcode & 0x02) != 0;
}

extern struct ud_itab_entry ud_itab[];
extern struct ud_lookup_table_list_entry ud_lookup_table_list[];

/* END decode.h */
/* BEGIN udint.h */

#define UD_ASSERT(_x)

#define UDERR(u, m) \
  do { \
    (u)->error = 1; \
  } while (0)

#define UD_RETURN_ON_ERROR(u) \
  do { \
    if ((u)->error != 0) { \
      return (u)->error; \
    } \
  } while (0)

#define UD_RETURN_WITH_ERROR(u, m) \
  do { \
    UDERR(u, m); \
    return (u)->error; \
  } while (0)

/* printf formatting int64 specifier */
#ifdef FMT64
# undef FMT64
#endif
#if defined(_MSC_VER) || defined(__BORLANDC__)
# define FMT64 "I64"
#else
# if defined(__APPLE__)
#  define FMT64 "ll"
# elif defined(__amd64__) || defined(__x86_64__)
#  define FMT64 "l"
# else 
#  define FMT64 "ll"
# endif /* !x64 */
#endif

/* END udint.h */
/* BEGIN syn.h */

extern const char* ud_reg_tab[];

uint64_t ud_syn_rel_target(struct ud*, struct ud_operand*);

#ifdef __GNUC__
int ud_asmprintf(struct ud *u, const char *fmt, ...)
    __attribute__ ((format (printf, 2, 3)));
#else
int ud_asmprintf(struct ud *u, const char *fmt, ...);
#endif

void ud_syn_print_addr(struct ud *u, uint64_t addr);
void ud_syn_print_imm(struct ud* u, const struct ud_operand *op);
void ud_syn_print_mem_disp(struct ud* u, const struct ud_operand *, int sign);

/* END syn.h */
/* BEGIN udis86.c */

static void ud_inp_init(struct ud *u);

/* =============================================================================
 * ud_init
 *    Initializes ud_t object.
 * =============================================================================
 */
extern void 
ud_init(struct ud* u)
{
  memset((void*)u, 0, sizeof(struct ud));
  ud_set_mode(u, 16);
  u->mnemonic = UD_Iinvalid;
  ud_set_pc(u, 0);

  ud_set_asm_buffer(u, u->asm_buf_int, sizeof(u->asm_buf_int));
}


/* =============================================================================
 * ud_disassemble
 *    Disassembles one instruction and returns the number of 
 *    bytes disassembled. A zero means end of disassembly.
 * =============================================================================
 */
extern unsigned int
ud_disassemble(struct ud* u)
{
  int len;
  if (u->inp_end) {
    return 0;
  }
  if ((len = ud_decode(u)) > 0) {
    if (u->translator != NULL) {
      u->asm_buf[0] = '\0';
      u->translator(u);
    }
  }
  return len;
}


/* =============================================================================
 * ud_set_mode() - Set Disassemly Mode.
 * =============================================================================
 */
extern void 
ud_set_mode(struct ud* u, uint8_t m)
{
  switch(m) {
  case 16:
  case 32:
  case 64: u->dis_mode = m ; return;
  default: u->dis_mode = 16; return;
  }
}

/* =============================================================================
 * ud_set_vendor() - Set vendor.
 * =============================================================================
 */
extern void 
ud_set_vendor(struct ud* u, unsigned v)
{
  switch(v) {
  case UD_VENDOR_INTEL:
    u->vendor = v;
    break;
  case UD_VENDOR_ANY:
    u->vendor = v;
    break;
  default:
    u->vendor = UD_VENDOR_AMD;
  }
}

/* =============================================================================
 * ud_set_pc() - Sets code origin. 
 * =============================================================================
 */
extern void 
ud_set_pc(struct ud* u, uint64_t o)
{
  u->pc = o;
}

/* =============================================================================
 * ud_set_syntax() - Sets the output syntax.
 * =============================================================================
 */
extern void 
ud_set_syntax(struct ud* u, void (*t)(struct ud*))
{
  u->translator = t;
}

/* =============================================================================
 * ud_insn() - returns the disassembled instruction
 * =============================================================================
 */
const char* 
ud_insn_asm(const struct ud* u) 
{
  return u->asm_buf;
}

/* =============================================================================
 * ud_insn_offset() - Returns the offset.
 * =============================================================================
 */
uint64_t
ud_insn_off(const struct ud* u) 
{
  return u->insn_offset;
}

/* =============================================================================
 * ud_insn_ptr
 *    Returns a pointer to buffer containing the bytes that were
 *    disassembled.
 * =============================================================================
 */
extern const uint8_t* 
ud_insn_ptr(const struct ud* u) 
{
  return (u->inp_buf == NULL) ? 
            u->inp_sess : u->inp_buf + (u->inp_buf_index - u->inp_ctr);
}


/* =============================================================================
 * ud_insn_len
 *    Returns the count of bytes disassembled.
 * =============================================================================
 */
extern unsigned int 
ud_insn_len(const struct ud* u) 
{
  return u->inp_ctr;
}


/* =============================================================================
 * ud_insn_get_opr
 *    Return the operand struct representing the nth operand of
 *    the currently disassembled instruction. Returns NULL if
 *    there's no such operand.
 * =============================================================================
 */
const struct ud_operand*
ud_insn_opr(const struct ud *u, unsigned int n)
{
  if (n > 2 || u->operand[n].type == UD_NONE) {
    return NULL; 
  } else {
    return &u->operand[n];
  }
}


/* =============================================================================
 * ud_opr_is_sreg
 *    Returns non-zero if the given operand is of a segment register type.
 * =============================================================================
 */
int
ud_opr_is_sreg(const struct ud_operand *opr)
{
  return opr->type == UD_OP_REG && 
         opr->base >= UD_R_ES   &&
         opr->base <= UD_R_GS;
}


/* =============================================================================
 * ud_opr_is_sreg
 *    Returns non-zero if the given operand is of a general purpose
 *    register type.
 * =============================================================================
 */
int
ud_opr_is_gpr(const struct ud_operand *opr)
{
  return opr->type == UD_OP_REG && 
         opr->base >= UD_R_AL   &&
         opr->base <= UD_R_R15;
}


/* =============================================================================
 * ud_set_user_opaque_data
 * ud_get_user_opaque_data
 *    Get/set user opaqute data pointer
 * =============================================================================
 */
void
ud_set_user_opaque_data(struct ud * u, void* opaque)
{
  u->user_opaque_data = opaque;
}

void*
ud_get_user_opaque_data(const struct ud *u)
{
  return u->user_opaque_data;
}


/* =============================================================================
 * ud_set_asm_buffer
 *    Allow the user to set an assembler output buffer. If `buf` is NULL,
 *    we switch back to the internal buffer.
 * =============================================================================
 */
void
ud_set_asm_buffer(struct ud *u, char *buf, size_t size)
{
  if (buf == NULL) {
    ud_set_asm_buffer(u, u->asm_buf_int, sizeof(u->asm_buf_int));
  } else {
    u->asm_buf = buf;
    u->asm_buf_size = size;
  }
}


/* =============================================================================
 * ud_set_sym_resolver
 *    Set symbol resolver for relative targets used in the translation
 *    phase.
 *
 *    The resolver is a function that takes a uint64_t address and returns a
 *    symbolic name for the that address. The function also takes a second
 *    argument pointing to an integer that the client can optionally set to a
 *    non-zero value for offsetted targets. (symbol+offset) The function may
 *    also return NULL, in which case the translator only prints the target
 *    address.
 *
 *    The function pointer maybe NULL which resets symbol resolution.
 * =============================================================================
 */
void
ud_set_sym_resolver(struct ud *u, const char* (*resolver)(struct ud*, 
                                                          uint64_t addr,
                                                          int64_t *offset))
{
  u->sym_resolver = resolver;
}


/* =============================================================================
 * ud_insn_mnemonic
 *    Return the current instruction mnemonic.
 * =============================================================================
 */
enum ud_mnemonic_code
ud_insn_mnemonic(const struct ud *u)
{
  return u->mnemonic;
}


/* =============================================================================
 * ud_lookup_mnemonic
 *    Looks up mnemonic code in the mnemonic string table.
 *    Returns NULL if the mnemonic code is invalid.
 * =============================================================================
 */
const char*
ud_lookup_mnemonic(enum ud_mnemonic_code c)
{
  if (c < UD_MAX_MNEMONIC_CODE) {
    return ud_mnemonics_str[c];
  } else {
    return NULL;
  }
}


/* 
 * ud_inp_init
 *    Initializes the input system.
 */
static void
ud_inp_init(struct ud *u)
{
  u->inp_hook      = NULL;
  u->inp_buf       = NULL;
  u->inp_buf_size  = 0;
  u->inp_buf_index = 0;
  u->inp_curr      = 0;
  u->inp_ctr       = 0;
  u->inp_end       = 0;
}


/* =============================================================================
 * ud_inp_set_hook
 *    Sets input hook.
 * =============================================================================
 */
void 
ud_set_input_hook(register struct ud* u, int (*hook)(struct ud*))
{
  ud_inp_init(u);
  u->inp_hook = hook;
}

/* =============================================================================
 * ud_inp_set_buffer
 *    Set buffer as input.
 * =============================================================================
 */
void 
ud_set_input_buffer(register struct ud* u, const uint8_t* buf, size_t len)
{
  ud_inp_init(u);
  u->inp_buf = buf;
  u->inp_buf_size = len;
  u->inp_buf_index = 0;
}


/* =============================================================================
 * ud_input_skip
 *    Skip n input bytes.
 * ============================================================================
 */
void 
ud_input_skip(struct ud* u, size_t n)
{
  if (u->inp_end) {
    return;
  }
  if (u->inp_buf == NULL) {
    while (n--) {
      int c = u->inp_hook(u);
      if (c == UD_EOI) {
        goto eoi;
      }
    }
    return;
  } else {
    if (n > u->inp_buf_size ||
        u->inp_buf_index > u->inp_buf_size - n) {
      u->inp_buf_index = u->inp_buf_size; 
      goto eoi;
    }
    u->inp_buf_index += n; 
    return;
  }
eoi:
  u->inp_end = 1;
  UDERR(u, "cannot skip, eoi received\b");
  return;
}


/* =============================================================================
 * ud_input_end
 *    Returns non-zero on end-of-input.
 * =============================================================================
 */
int
ud_input_end(const struct ud *u)
{
  return u->inp_end;
}

/* END udis96.c */
/* BEGIN syn-att.c */

/* -----------------------------------------------------------------------------
 * opr_cast() - Prints an operand cast.
 * -----------------------------------------------------------------------------
 */
static void 
opr_cast(struct ud* u, struct ud_operand* op)
{
  switch(op->size) {
  case 16 : case 32 :
    ud_asmprintf(u, "*");   break;
  default: break;
  }
}

/* -----------------------------------------------------------------------------
 * gen_operand() - Generates assembly output for each operand.
 * -----------------------------------------------------------------------------
 */
static void 
gen_operand(struct ud* u, struct ud_operand* op)
{
  switch(op->type) {
  case UD_OP_CONST:
    ud_asmprintf(u, "$0x%x", op->lval.udword);
    break;

  case UD_OP_REG:
    ud_asmprintf(u, "%%%s", ud_reg_tab[op->base - UD_R_AL]);
    break;

  case UD_OP_MEM:
    if (u->br_far) {
        opr_cast(u, op);
    }
    if (u->pfx_seg) {
      ud_asmprintf(u, "%%%s:", ud_reg_tab[u->pfx_seg - UD_R_AL]);
    }
    if (op->offset != 0) { 
      ud_syn_print_mem_disp(u, op, 0);
    }
    if (op->base) {
      ud_asmprintf(u, "(%%%s", ud_reg_tab[op->base - UD_R_AL]);
    }
    if (op->index) {
      if (op->base) {
        ud_asmprintf(u, ",");
      } else {
        ud_asmprintf(u, "(");
      }
      ud_asmprintf(u, "%%%s", ud_reg_tab[op->index - UD_R_AL]);
    }
    if (op->scale) {
      ud_asmprintf(u, ",%d", op->scale);
    }
    if (op->base || op->index) {
      ud_asmprintf(u, ")");
    }
    break;

  case UD_OP_IMM:
    ud_asmprintf(u, "$");
    ud_syn_print_imm(u, op);
    break;

  case UD_OP_JIMM:
    ud_syn_print_addr(u, ud_syn_rel_target(u, op));
    break;

  case UD_OP_PTR:
    switch (op->size) {
      case 32:
        ud_asmprintf(u, "$0x%x, $0x%x", op->lval.ptr.seg, 
          op->lval.ptr.off & 0xFFFF);
        break;
      case 48:
        ud_asmprintf(u, "$0x%x, $0x%x", op->lval.ptr.seg, 
          op->lval.ptr.off);
        break;
    }
    break;
      
  default: return;
  }
}

/* =============================================================================
 * translates to AT&T syntax 
 * =============================================================================
 */
extern void 
ud_translate_att(struct ud *u)
{
  int size = 0;
  int star = 0;

  /* check if P_OSO prefix is used */
  if (! P_OSO(u->itab_entry->prefix) && u->pfx_opr) {
  switch (u->dis_mode) {
    case 16: 
      ud_asmprintf(u, "o32 ");
      break;
    case 32:
    case 64:
      ud_asmprintf(u, "o16 ");
      break;
  }
  }

  /* check if P_ASO prefix was used */
  if (! P_ASO(u->itab_entry->prefix) && u->pfx_adr) {
  switch (u->dis_mode) {
    case 16: 
      ud_asmprintf(u, "a32 ");
      break;
    case 32:
      ud_asmprintf(u, "a16 ");
      break;
    case 64:
      ud_asmprintf(u, "a32 ");
      break;
  }
  }

  if (u->pfx_lock)
    ud_asmprintf(u,  "lock ");
  if (u->pfx_rep) {
    ud_asmprintf(u, "rep ");
  } else if (u->pfx_rep) {
    ud_asmprintf(u, "repe ");
  } else if (u->pfx_repne) {
    ud_asmprintf(u, "repne ");
  }

  /* special instructions */
  switch (u->mnemonic) {
  case UD_Iretf: 
    ud_asmprintf(u, "lret "); 
    break;
  case UD_Idb:
    ud_asmprintf(u, ".byte 0x%x", u->operand[0].lval.ubyte);
    return;
  case UD_Ijmp:
  case UD_Icall:
    if (u->br_far) ud_asmprintf(u,  "l");
        if (u->operand[0].type == UD_OP_REG) {
          star = 1;
        }
    ud_asmprintf(u, "%s", ud_lookup_mnemonic(u->mnemonic));
    break;
  case UD_Ibound:
  case UD_Ienter:
    if (u->operand[0].type != UD_NONE)
      gen_operand(u, &u->operand[0]);
    if (u->operand[1].type != UD_NONE) {
      ud_asmprintf(u, ",");
      gen_operand(u, &u->operand[1]);
    }
    return;
  default:
    ud_asmprintf(u, "%s", ud_lookup_mnemonic(u->mnemonic));
  }

  if (size == 8)
  ud_asmprintf(u, "b");
  else if (size == 16)
  ud_asmprintf(u, "w");
  else if (size == 64)
  ud_asmprintf(u, "q");

  if (star) {
    ud_asmprintf(u, " *");
  } else {
    ud_asmprintf(u, " ");
  }

  if (u->operand[2].type != UD_NONE) {
  gen_operand(u, &u->operand[2]);
  ud_asmprintf(u, ", ");
  }

  if (u->operand[1].type != UD_NONE) {
  gen_operand(u, &u->operand[1]);
  ud_asmprintf(u, ", ");
  }

  if (u->operand[0].type != UD_NONE)
  gen_operand(u, &u->operand[0]);
}

/* END syn-att.c */
/* BEGIN decode.c */

/* The max number of prefixes to an instruction */
#define MAX_PREFIXES    15

/* rex prefix bits */
#define REX_W(r)        ( ( 0xF & ( r ) )  >> 3 )
#define REX_R(r)        ( ( 0x7 & ( r ) )  >> 2 )
#define REX_X(r)        ( ( 0x3 & ( r ) )  >> 1 )
#define REX_B(r)        ( ( 0x1 & ( r ) )  >> 0 )
#define REX_PFX_MASK(n) ( ( P_REXW(n) << 3 ) | \
                          ( P_REXR(n) << 2 ) | \
                          ( P_REXX(n) << 1 ) | \
                          ( P_REXB(n) << 0 ) )

/* scable-index-base bits */
#define SIB_S(b)        ( ( b ) >> 6 )
#define SIB_I(b)        ( ( ( b ) >> 3 ) & 7 )
#define SIB_B(b)        ( ( b ) & 7 )

/* modrm bits */
#define MODRM_REG(b)    ( ( ( b ) >> 3 ) & 7 )
#define MODRM_NNN(b)    ( ( ( b ) >> 3 ) & 7 )
#define MODRM_MOD(b)    ( ( ( b ) >> 6 ) & 3 )
#define MODRM_RM(b)     ( ( b ) & 7 )

static int decode_ext(struct ud *u, uint16_t ptr);

enum reg_class { /* register classes */
  REGCLASS_GPR,
  REGCLASS_MMX,
  REGCLASS_CR,
  REGCLASS_DB,
  REGCLASS_SEG,
  REGCLASS_XMM
};

 /* 
 * inp_start
 *    Should be called before each de-code operation.
 */
static void
inp_start(struct ud *u)
{
  u->inp_ctr = 0;
}

   
static uint8_t
inp_next(struct ud *u)
{
  if (u->inp_end == 0) {
    if (u->inp_buf != NULL) {
      if (u->inp_buf_index < u->inp_buf_size) {
        u->inp_ctr++;
        return (u->inp_curr = u->inp_buf[u->inp_buf_index++]);
      }
    } else {
      int c;
      if ((c = u->inp_hook(u)) != UD_EOI) {
        u->inp_curr = c;
        u->inp_sess[u->inp_ctr++] = u->inp_curr;
        return u->inp_curr;
      }
    }
  }
  u->inp_end = 1;
  UDERR(u, "byte expected, eoi received\n");
  return 0;
}

static uint8_t
inp_curr(struct ud *u)
{
  return u->inp_curr;
}


/*
 * inp_uint8
 * int_uint16
 * int_uint32
 * int_uint64
 *    Load little-endian values from input
 */
static uint8_t 
inp_uint8(struct ud* u)
{
  return inp_next(u);
}

static uint16_t 
inp_uint16(struct ud* u)
{
  uint16_t r, ret;

  ret = inp_next(u);
  r = inp_next(u);
  return ret | (r << 8);
}

static uint32_t 
inp_uint32(struct ud* u)
{
  uint32_t r, ret;

  ret = inp_next(u);
  r = inp_next(u);
  ret = ret | (r << 8);
  r = inp_next(u);
  ret = ret | (r << 16);
  r = inp_next(u);
  return ret | (r << 24);
}

static uint64_t 
inp_uint64(struct ud* u)
{
  uint64_t r, ret;

  ret = inp_next(u);
  r = inp_next(u);
  ret = ret | (r << 8);
  r = inp_next(u);
  ret = ret | (r << 16);
  r = inp_next(u);
  ret = ret | (r << 24);
  r = inp_next(u);
  ret = ret | (r << 32);
  r = inp_next(u);
  ret = ret | (r << 40);
  r = inp_next(u);
  ret = ret | (r << 48);
  r = inp_next(u);
  return ret | (r << 56);
}


static inline int
eff_opr_mode(int dis_mode, int rex_w, int pfx_opr)
{
  if (dis_mode == 64) {
    return rex_w ? 64 : (pfx_opr ? 16 : 32);
  } else if (dis_mode == 32) {
    return pfx_opr ? 16 : 32;
  } else {
    UD_ASSERT(dis_mode == 16);
    return pfx_opr ? 32 : 16;
  }
}


static inline int
eff_adr_mode(int dis_mode, int pfx_adr)
{
  if (dis_mode == 64) {
    return pfx_adr ? 32 : 64;
  } else if (dis_mode == 32) {
    return pfx_adr ? 16 : 32;
  } else {
    UD_ASSERT(dis_mode == 16);
    return pfx_adr ? 32 : 16;
  }
}


/* 
 * decode_prefixes
 *
 *  Extracts instruction prefixes.
 */
static int 
decode_prefixes(struct ud *u)
{
  int done = 0;
  uint8_t curr = 0, last = 0;
  UD_RETURN_ON_ERROR(u);

  do {
    last = curr;
    curr = inp_next(u); 
    UD_RETURN_ON_ERROR(u);
    if (u->inp_ctr == MAX_INSN_LENGTH) {
      UD_RETURN_WITH_ERROR(u, "max instruction length");
    }
   
    switch (curr)  
    {
    case 0x2E: 
      u->pfx_seg = UD_R_CS; 
      break;
    case 0x36:     
      u->pfx_seg = UD_R_SS; 
      break;
    case 0x3E: 
      u->pfx_seg = UD_R_DS; 
      break;
    case 0x26: 
      u->pfx_seg = UD_R_ES; 
      break;
    case 0x64: 
      u->pfx_seg = UD_R_FS; 
      break;
    case 0x65: 
      u->pfx_seg = UD_R_GS; 
      break;
    case 0x67: /* adress-size override prefix */ 
      u->pfx_adr = 0x67;
      break;
    case 0xF0: 
      u->pfx_lock = 0xF0;
      break;
    case 0x66: 
      u->pfx_opr = 0x66;
      break;
    case 0xF2:
      u->pfx_str = 0xf2;
      break;
    case 0xF3:
      u->pfx_str = 0xf3;
      break;
    default:
      /* consume if rex */
      done = (u->dis_mode == 64 && (curr & 0xF0) == 0x40) ? 0 : 1;
      break;
    }
  } while (!done);
  /* rex prefixes in 64bit mode, must be the last prefix */
  if (u->dis_mode == 64 && (last & 0xF0) == 0x40) {
    u->pfx_rex = last;  
  }
  return 0;
}


static inline unsigned int modrm( struct ud * u )
{
    if ( !u->have_modrm ) {
        u->modrm = inp_next( u );
        u->have_modrm = 1;
    }
    return u->modrm;
}


static unsigned int
resolve_operand_size( const struct ud * u, unsigned int s )
{
    switch ( s ) 
    {
    case SZ_V:
        return ( u->opr_mode );
    case SZ_Z:  
        return ( u->opr_mode == 16 ) ? 16 : 32;
    case SZ_Y:
        return ( u->opr_mode == 16 ) ? 32 : u->opr_mode;
    case SZ_RDQ:
        return ( u->dis_mode == 64 ) ? 64 : 32;
    default:
        return s;
    }
}


static int resolve_mnemonic( struct ud* u )
{
  /* resolve 3dnow weirdness. */
  if ( u->mnemonic == UD_I3dnow ) {
    u->mnemonic = ud_itab[ u->le->table[ inp_curr( u )  ] ].mnemonic;
  }
  /* SWAPGS is only valid in 64bits mode */
  if ( u->mnemonic == UD_Iswapgs && u->dis_mode != 64 ) {
    UDERR(u, "swapgs invalid in 64bits mode\n");
    return -1;
  }

  if (u->mnemonic == UD_Ixchg) {
    if ((u->operand[0].type == UD_OP_REG && u->operand[0].base == UD_R_AX  &&
         u->operand[1].type == UD_OP_REG && u->operand[1].base == UD_R_AX) ||
        (u->operand[0].type == UD_OP_REG && u->operand[0].base == UD_R_EAX &&
         u->operand[1].type == UD_OP_REG && u->operand[1].base == UD_R_EAX)) {
      u->operand[0].type = UD_NONE;
      u->operand[1].type = UD_NONE;
      u->mnemonic = UD_Inop;
    }
  }

  if (u->mnemonic == UD_Inop && u->pfx_repe) {
    u->pfx_repe = 0;
    u->mnemonic = UD_Ipause;
  }
  return 0;
}


/* -----------------------------------------------------------------------------
 * decode_a()- Decodes operands of the type seg:offset
 * -----------------------------------------------------------------------------
 */
static void 
decode_a(struct ud* u, struct ud_operand *op)
{
  if (u->opr_mode == 16) {  
    /* seg16:off16 */
    op->type = UD_OP_PTR;
    op->size = 32;
    op->lval.ptr.off = inp_uint16(u);
    op->lval.ptr.seg = inp_uint16(u);
  } else {
    /* seg16:off32 */
    op->type = UD_OP_PTR;
    op->size = 48;
    op->lval.ptr.off = inp_uint32(u);
    op->lval.ptr.seg = inp_uint16(u);
  }
}

/* -----------------------------------------------------------------------------
 * decode_gpr() - Returns decoded General Purpose Register 
 * -----------------------------------------------------------------------------
 */
static enum ud_type 
decode_gpr(register struct ud* u, unsigned int s, unsigned char rm)
{
  switch (s) {
    case 64:
        return UD_R_RAX + rm;
    case 32:
        return UD_R_EAX + rm;
    case 16:
        return UD_R_AX  + rm;
    case  8:
        if (u->dis_mode == 64 && u->pfx_rex) {
            if (rm >= 4)
                return UD_R_SPL + (rm-4);
            return UD_R_AL + rm;
        } else return UD_R_AL + rm;
    case 0:
        /* invalid size in case of a decode error */
        UD_ASSERT(u->error);
        return UD_NONE;
    default:
        UD_ASSERT(!"invalid operand size");
        return UD_NONE;
  }
}

static void
decode_reg(struct ud *u, 
           struct ud_operand *opr,
           int type,
           int num,
           int size)
{
  int reg;
  size = resolve_operand_size(u, size);
  switch (type) {
    case REGCLASS_GPR : reg = decode_gpr(u, size, num); break;
    case REGCLASS_MMX : reg = UD_R_MM0  + (num & 7); break;
    case REGCLASS_XMM : reg = UD_R_XMM0 + num; break;
    case REGCLASS_CR : reg = UD_R_CR0  + num; break;
    case REGCLASS_DB : reg = UD_R_DR0  + num; break;
    case REGCLASS_SEG : {
      /*
       * Only 6 segment registers, anything else is an error.
       */
      if ((num & 7) > 5) {
        UDERR(u, "invalid segment register value\n");
        return;
      } else {
        reg = UD_R_ES + (num & 7);
      }
      break;
    }
    default:
      UD_ASSERT(!"invalid register type");
      return;
  }
  opr->type = UD_OP_REG;
  opr->base = reg;
  opr->size = size;
}


/*
 * decode_imm 
 *
 *    Decode Immediate values.
 */
static void 
decode_imm(struct ud* u, unsigned int size, struct ud_operand *op)
{
  op->size = resolve_operand_size(u, size);
  op->type = UD_OP_IMM;

  switch (op->size) {
  case  8: op->lval.sbyte = inp_uint8(u);   break;
  case 16: op->lval.uword = inp_uint16(u);  break;
  case 32: op->lval.udword = inp_uint32(u); break;
  case 64: op->lval.uqword = inp_uint64(u); break;
  default: return;
  }
}


/* 
 * decode_mem_disp
 *
 *    Decode mem address displacement.
 */
static void 
decode_mem_disp(struct ud* u, unsigned int size, struct ud_operand *op)
{
  switch (size) {
  case 8:
    op->offset = 8; 
    op->lval.ubyte  = inp_uint8(u);
    break;
  case 16:
    op->offset = 16; 
    op->lval.uword  = inp_uint16(u); 
    break;
  case 32:
    op->offset = 32; 
    op->lval.udword = inp_uint32(u); 
    break;
  case 64:
    op->offset = 64; 
    op->lval.uqword = inp_uint64(u); 
    break;
  default:
      return;
  }
}


/*
 * decode_modrm_reg
 *
 *    Decodes reg field of mod/rm byte
 * 
 */
static inline void
decode_modrm_reg(struct ud         *u, 
                 struct ud_operand *operand,
                 unsigned int       type,
                 unsigned int       size)
{
  uint8_t reg = (REX_R(u->pfx_rex) << 3) | MODRM_REG(modrm(u));
  decode_reg(u, operand, type, reg, size);
}


/*
 * decode_modrm_rm
 *
 *    Decodes rm field of mod/rm byte
 * 
 */
static void 
decode_modrm_rm(struct ud         *u, 
                struct ud_operand *op,
                unsigned char      type,    /* register type */
                unsigned int       size)    /* operand size */

{
  size_t offset = 0;
  unsigned char mod, rm;

  /* get mod, r/m and reg fields */
  mod = MODRM_MOD(modrm(u));
  rm  = (REX_B(u->pfx_rex) << 3) | MODRM_RM(modrm(u));

  /* 
   * If mod is 11b, then the modrm.rm specifies a register.
   *
   */
  if (mod == 3) {
    decode_reg(u, op, type, rm, size);
    return;
  }

  /* 
   * !11b => Memory Address
   */  
  op->type = UD_OP_MEM;
  op->size = resolve_operand_size(u, size);

  if (u->adr_mode == 64) {
    op->base = UD_R_RAX + rm;
    if (mod == 1) {
      offset = 8;
    } else if (mod == 2) {
      offset = 32;
    } else if (mod == 0 && (rm & 7) == 5) {           
      op->base = UD_R_RIP;
      offset = 32;
    } else {
      offset = 0;
    }
    /* 
     * Scale-Index-Base (SIB) 
     */
    if ((rm & 7) == 4) {
      inp_next(u);
      
      op->scale = (1 << SIB_S(inp_curr(u))) & ~1;
      op->index = UD_R_RAX + (SIB_I(inp_curr(u)) | (REX_X(u->pfx_rex) << 3));
      op->base  = UD_R_RAX + (SIB_B(inp_curr(u)) | (REX_B(u->pfx_rex) << 3));

      /* special conditions for base reference */
      if (op->index == UD_R_RSP) {
        op->index = UD_NONE;
        op->scale = UD_NONE;
      }

      if (op->base == UD_R_RBP || op->base == UD_R_R13) {
        if (mod == 0) {
          op->base = UD_NONE;
        } 
        if (mod == 1) {
          offset = 8;
        } else {
          offset = 32;
        }
      }
    }
  } else if (u->adr_mode == 32) {
    op->base = UD_R_EAX + rm;
    if (mod == 1) {
      offset = 8;
    } else if (mod == 2) {
      offset = 32;
    } else if (mod == 0 && rm == 5) {
      op->base = UD_NONE;
      offset = 32;
    } else {
      offset = 0;
    }

    /* Scale-Index-Base (SIB) */
    if ((rm & 7) == 4) {
      inp_next(u);

      op->scale = (1 << SIB_S(inp_curr(u))) & ~1;
      op->index = UD_R_EAX + (SIB_I(inp_curr(u)) | (REX_X(u->pfx_rex) << 3));
      op->base  = UD_R_EAX + (SIB_B(inp_curr(u)) | (REX_B(u->pfx_rex) << 3));

      if (op->index == UD_R_ESP) {
        op->index = UD_NONE;
        op->scale = UD_NONE;
      }

      /* special condition for base reference */
      if (op->base == UD_R_EBP) {
        if (mod == 0) {
          op->base = UD_NONE;
        } 
        if (mod == 1) {
          offset = 8;
        } else {
          offset = 32;
        }
      }
    }
  } else {
    const unsigned int bases[]   = { UD_R_BX, UD_R_BX, UD_R_BP, UD_R_BP,
                                     UD_R_SI, UD_R_DI, UD_R_BP, UD_R_BX };
    const unsigned int indices[] = { UD_R_SI, UD_R_DI, UD_R_SI, UD_R_DI,
                                     UD_NONE, UD_NONE, UD_NONE, UD_NONE };
    op->base  = bases[rm & 7];
    op->index = indices[rm & 7];
    if (mod == 0 && rm == 6) {
      offset = 16;
      op->base = UD_NONE;
    } else if (mod == 1) {
      offset = 8;
    } else if (mod == 2) { 
      offset = 16;
    }
  }

  if (offset) {
    decode_mem_disp(u, offset, op);
  }
}


/* 
 * decode_moffset
 *    Decode offset-only memory operand
 */
static void
decode_moffset(struct ud *u, unsigned int size, struct ud_operand *opr)
{
  opr->type = UD_OP_MEM;
  opr->size = resolve_operand_size(u, size);
  decode_mem_disp(u, u->adr_mode, opr);
}


/* -----------------------------------------------------------------------------
 * decode_operands() - Disassembles Operands.
 * -----------------------------------------------------------------------------
 */
static int
decode_operand(struct ud           *u, 
               struct ud_operand   *operand,
               enum ud_operand_code type,
               unsigned int         size)
{
  operand->_oprcode = type;

  switch (type) {
    case OP_A :
      decode_a(u, operand);
      break;
    case OP_MR:
      decode_modrm_rm(u, operand, REGCLASS_GPR, 
                      MODRM_MOD(modrm(u)) == 3 ? 
                        Mx_reg_size(size) : Mx_mem_size(size));
      break;
    case OP_F:
      u->br_far  = 1;
      /* intended fall through */
    case OP_M:
      if (MODRM_MOD(modrm(u)) == 3) {
        UDERR(u, "expected modrm.mod != 3\n");
      }
      /* intended fall through */
    case OP_E:
      decode_modrm_rm(u, operand, REGCLASS_GPR, size);
      break;
    case OP_G:
      decode_modrm_reg(u, operand, REGCLASS_GPR, size);
      break;
    case OP_sI:
    case OP_I:
      decode_imm(u, size, operand);
      break;
    case OP_I1:
      operand->type = UD_OP_CONST;
      operand->lval.udword = 1;
      break;
    case OP_N:
      if (MODRM_MOD(modrm(u)) != 3) {
        UDERR(u, "expected modrm.mod == 3\n");
      }
      /* intended fall through */
    case OP_Q:
      decode_modrm_rm(u, operand, REGCLASS_MMX, size);
      break;
    case OP_P:
      decode_modrm_reg(u, operand, REGCLASS_MMX, size);
      break;
    case OP_U:
      if (MODRM_MOD(modrm(u)) != 3) {
        UDERR(u, "expected modrm.mod == 3\n");
      }
      /* intended fall through */
    case OP_W:
      decode_modrm_rm(u, operand, REGCLASS_XMM, size);
      break;
    case OP_V:
      decode_modrm_reg(u, operand, REGCLASS_XMM, size);
      break;
    case OP_MU:
      decode_modrm_rm(u, operand, REGCLASS_XMM, 
                      MODRM_MOD(modrm(u)) == 3 ? 
                        Mx_reg_size(size) : Mx_mem_size(size));
      break;
    case OP_S:
      decode_modrm_reg(u, operand, REGCLASS_SEG, size);
      break;
    case OP_O:
      decode_moffset(u, size, operand);
      break;
    case OP_R0: 
    case OP_R1: 
    case OP_R2: 
    case OP_R3: 
    case OP_R4: 
    case OP_R5: 
    case OP_R6: 
    case OP_R7:
      decode_reg(u, operand, REGCLASS_GPR, 
                 (REX_B(u->pfx_rex) << 3) | (type - OP_R0), size);
      break;
    case OP_AL:
    case OP_AX:
    case OP_eAX:
    case OP_rAX:
      decode_reg(u, operand, REGCLASS_GPR, 0, size);
      break;
    case OP_CL:
    case OP_CX:
    case OP_eCX:
      decode_reg(u, operand, REGCLASS_GPR, 1, size);
      break;
    case OP_DL:
    case OP_DX:
    case OP_eDX:
      decode_reg(u, operand, REGCLASS_GPR, 2, size);
      break;
    case OP_ES: 
    case OP_CS: 
    case OP_DS:
    case OP_SS: 
    case OP_FS: 
    case OP_GS:
      /* in 64bits mode, only fs and gs are allowed */
      if (u->dis_mode == 64) {
        if (type != OP_FS && type != OP_GS) {
          UDERR(u, "invalid segment register in 64bits\n");
        }
      }
      operand->type = UD_OP_REG;
      operand->base = (type - OP_ES) + UD_R_ES;
      operand->size = 16;
      break;
    case OP_J :
      decode_imm(u, size, operand);
      operand->type = UD_OP_JIMM;
      break ;
    case OP_R :
      if (MODRM_MOD(modrm(u)) != 3) {
        UDERR(u, "expected modrm.mod == 3\n");
      }
      decode_modrm_rm(u, operand, REGCLASS_GPR, size);
      break;
    case OP_C:
      decode_modrm_reg(u, operand, REGCLASS_CR, size);
      break;
    case OP_D:
      decode_modrm_reg(u, operand, REGCLASS_DB, size);
      break;
    case OP_I3 :
      operand->type = UD_OP_CONST;
      operand->lval.sbyte = 3;
      break;
    case OP_ST0: 
    case OP_ST1: 
    case OP_ST2: 
    case OP_ST3:
    case OP_ST4:
    case OP_ST5: 
    case OP_ST6: 
    case OP_ST7:
      operand->type = UD_OP_REG;
      operand->base = (type - OP_ST0) + UD_R_ST0;
      operand->size = 80;
      break;
    default :
      break;
  }
  return 0;
}


/* 
 * decode_operands
 *
 *    Disassemble upto 3 operands of the current instruction being
 *    disassembled. By the end of the function, the operand fields
 *    of the ud structure will have been filled.
 */
static int
decode_operands(struct ud* u)
{
  decode_operand(u, &u->operand[0],
                    u->itab_entry->operand1.type,
                    u->itab_entry->operand1.size);
  decode_operand(u, &u->operand[1],
                    u->itab_entry->operand2.type,
                    u->itab_entry->operand2.size);
  decode_operand(u, &u->operand[2],
                    u->itab_entry->operand3.type,
                    u->itab_entry->operand3.size);
  return 0;
}
    
/* -----------------------------------------------------------------------------
 * clear_insn() - clear instruction structure
 * -----------------------------------------------------------------------------
 */
static void
clear_insn(register struct ud* u)
{
  u->error     = 0;
  u->pfx_seg   = 0;
  u->pfx_opr   = 0;
  u->pfx_adr   = 0;
  u->pfx_lock  = 0;
  u->pfx_repne = 0;
  u->pfx_rep   = 0;
  u->pfx_repe  = 0;
  u->pfx_rex   = 0;
  u->pfx_str   = 0;
  u->mnemonic  = UD_Inone;
  u->itab_entry = NULL;
  u->have_modrm = 0;
  u->br_far    = 0;

  memset( &u->operand[ 0 ], 0, sizeof( struct ud_operand ) );
  memset( &u->operand[ 1 ], 0, sizeof( struct ud_operand ) );
  memset( &u->operand[ 2 ], 0, sizeof( struct ud_operand ) );
}


static inline int
resolve_pfx_str(struct ud* u)
{
  if (u->pfx_str == 0xf3) {
    if (P_STR(u->itab_entry->prefix)) {
        u->pfx_rep  = 0xf3;
    } else {
        u->pfx_repe = 0xf3;
    }
  } else if (u->pfx_str == 0xf2) {
    u->pfx_repne = 0xf3;
  }
  return 0;
}


static int
resolve_mode( struct ud* u )
{
  int default64;
  /* if in error state, bail out */
  if ( u->error ) return -1; 

  /* propagate prefix effects */
  if ( u->dis_mode == 64 ) {  /* set 64bit-mode flags */

    /* Check validity of  instruction m64 */
    if ( P_INV64( u->itab_entry->prefix ) ) {
      UDERR(u, "instruction invalid in 64bits\n");
      return -1;
    }

    /* effective rex prefix is the  effective mask for the 
     * instruction hard-coded in the opcode map.
     */
    u->pfx_rex = ( u->pfx_rex & 0x40 ) | 
                 ( u->pfx_rex & REX_PFX_MASK( u->itab_entry->prefix ) ); 

    /* whether this instruction has a default operand size of 
     * 64bit, also hardcoded into the opcode map.
     */
    default64 = P_DEF64( u->itab_entry->prefix ); 
    /* calculate effective operand size */
    if ( REX_W( u->pfx_rex ) ) {
        u->opr_mode = 64;
    } else if ( u->pfx_opr ) {
        u->opr_mode = 16;
    } else {
        /* unless the default opr size of instruction is 64,
         * the effective operand size in the absence of rex.w
         * prefix is 32.
         */
        u->opr_mode = default64 ? 64 : 32;
    }

    /* calculate effective address size */
    u->adr_mode = (u->pfx_adr) ? 32 : 64;
  } else if ( u->dis_mode == 32 ) { /* set 32bit-mode flags */
    u->opr_mode = ( u->pfx_opr ) ? 16 : 32;
    u->adr_mode = ( u->pfx_adr ) ? 16 : 32;
  } else if ( u->dis_mode == 16 ) { /* set 16bit-mode flags */
    u->opr_mode = ( u->pfx_opr ) ? 32 : 16;
    u->adr_mode = ( u->pfx_adr ) ? 32 : 16;
  }

  return 0;
}


static inline int
decode_insn(struct ud *u, uint16_t ptr)
{
  UD_ASSERT((ptr & 0x8000) == 0);
  u->itab_entry = &ud_itab[ ptr ];
  u->mnemonic = u->itab_entry->mnemonic;
  return (resolve_pfx_str(u)  == 0 &&
          resolve_mode(u)     == 0 &&
          decode_operands(u)  == 0 &&
          resolve_mnemonic(u) == 0) ? 0 : -1;
}


/*
 * decode_3dnow()
 *
 *    Decoding 3dnow is a little tricky because of its strange opcode
 *    structure. The final opcode disambiguation depends on the last
 *    byte that comes after the operands have been decoded. Fortunately,
 *    all 3dnow instructions have the same set of operand types. So we
 *    go ahead and decode the instruction by picking an arbitrarily chosen
 *    valid entry in the table, decode the operands, and read the final
 *    byte to resolve the menmonic.
 */
static inline int
decode_3dnow(struct ud* u)
{
  uint16_t ptr;
  UD_ASSERT(u->le->type == UD_TAB__OPC_3DNOW);
  UD_ASSERT(u->le->table[0xc] != 0);
  decode_insn(u, u->le->table[0xc]);
  inp_next(u); 
  if (u->error) {
    return -1;
  }
  ptr = u->le->table[inp_curr(u)]; 
  UD_ASSERT((ptr & 0x8000) == 0);
  u->mnemonic = ud_itab[ptr].mnemonic;
  return 0;
}


static int
decode_ssepfx(struct ud *u)
{
  uint8_t idx;
  uint8_t pfx;
 
  /*
   * String prefixes (f2, f3) take precedence over operand
   * size prefix (66).
   */
  pfx = u->pfx_str;
  if (pfx == 0) {
    pfx = u->pfx_opr;
  }
  idx = ((pfx & 0xf) + 1) / 2;
  if (u->le->table[idx] == 0) {
    idx = 0;
  }
  if (idx && u->le->table[idx] != 0) {
    /*
     * "Consume" the prefix as a part of the opcode, so it is no
     * longer exported as an instruction prefix.
     */
    u->pfx_str = 0;
    if (pfx == 0x66) {
        /* 
         * consume "66" only if it was used for decoding, leaving
         * it to be used as an operands size override for some
         * simd instructions.
         */
        u->pfx_opr = 0;
    }
  }
  return decode_ext(u, u->le->table[idx]);
}


/*
 * decode_ext()
 *
 *    Decode opcode extensions (if any)
 */
static int
decode_ext(struct ud *u, uint16_t ptr)
{
  uint8_t idx = 0;
  if ((ptr & 0x8000) == 0) {
    return decode_insn(u, ptr); 
  }
  u->le = &ud_lookup_table_list[(~0x8000 & ptr)];
  if (u->le->type == UD_TAB__OPC_3DNOW) {
    return decode_3dnow(u);
  }

  switch (u->le->type) {
    case UD_TAB__OPC_MOD:
      /* !11 = 0, 11 = 1 */
      idx = (MODRM_MOD(modrm(u)) + 1) / 4;
      break;
      /* disassembly mode/operand size/address size based tables.
       * 16 = 0,, 32 = 1, 64 = 2
       */
    case UD_TAB__OPC_MODE:
      idx = u->dis_mode != 64 ? 0 : 1;
      break;
    case UD_TAB__OPC_OSIZE:
      idx = eff_opr_mode(u->dis_mode, REX_W(u->pfx_rex), u->pfx_opr) / 32;
      break;
    case UD_TAB__OPC_ASIZE:
      idx = eff_adr_mode(u->dis_mode, u->pfx_adr) / 32;
      break;
    case UD_TAB__OPC_X87:
      idx = modrm(u) - 0xC0;
      break;
    case UD_TAB__OPC_VENDOR:
      if (u->vendor == UD_VENDOR_ANY) {
        /* choose a valid entry */
        idx = (u->le->table[idx] != 0) ? 0 : 1;
      } else if (u->vendor == UD_VENDOR_AMD) {
        idx = 0;
      } else {
        idx = 1;
      }
      break;
    case UD_TAB__OPC_RM:
      idx = MODRM_RM(modrm(u));
      break;
    case UD_TAB__OPC_REG:
      idx = MODRM_REG(modrm(u));
      break;
    case UD_TAB__OPC_SSE:
      return decode_ssepfx(u);
    default:
      UD_ASSERT(!"not reached");
      break;
  }

  return decode_ext(u, u->le->table[idx]);
}


static int
decode_opcode(struct ud *u)
{
  uint16_t ptr;
  UD_ASSERT(u->le->type == UD_TAB__OPC_TABLE);
  UD_RETURN_ON_ERROR(u);
  u->primary_opcode = inp_curr(u);
  ptr = u->le->table[inp_curr(u)];
  if (ptr & 0x8000) {
    u->le = &ud_lookup_table_list[ptr & ~0x8000];
    if (u->le->type == UD_TAB__OPC_TABLE) {
      inp_next(u);
      return decode_opcode(u);
    }
  }
  return decode_ext(u, ptr);
}

 
/* =============================================================================
 * ud_decode() - Instruction decoder. Returns the number of bytes decoded.
 * =============================================================================
 */
unsigned int
ud_decode(struct ud *u)
{
  inp_start(u);
  clear_insn(u);
  u->le = &ud_lookup_table_list[0];
  u->error = decode_prefixes(u) == -1 || 
             decode_opcode(u)   == -1 ||
             u->error;
  /* Handle decode error. */
  if (u->error) {
    /* clear out the decode data. */
    clear_insn(u);
    /* mark the sequence of bytes as invalid. */
    u->itab_entry = &ud_itab[0]; /* entry 0 is invalid */
    u->mnemonic = u->itab_entry->mnemonic;
  } 

    /* maybe this stray segment override byte
     * should be spewed out?
     */
    if ( !P_SEG( u->itab_entry->prefix ) && 
            u->operand[0].type != UD_OP_MEM &&
            u->operand[1].type != UD_OP_MEM )
        u->pfx_seg = 0;

  u->insn_offset = u->pc; /* set offset of instruction */
  u->asm_buf_fill = 0;   /* set translation buffer index to 0 */
  u->pc += u->inp_ctr;    /* move program counter by bytes decoded */

  /* return number of bytes disassembled. */
  return u->inp_ctr;
}

/* END decode.c */
/* BEGIN syn.c */

/* -----------------------------------------------------------------------------
 * Intel Register Table - Order Matters (types.h)!
 * -----------------------------------------------------------------------------
 */
const char* ud_reg_tab[] = 
{
  "al",   "cl",   "dl",   "bl",
  "ah",   "ch",   "dh",   "bh",
  "spl",  "bpl",    "sil",    "dil",
  "r8b",  "r9b",    "r10b",   "r11b",
  "r12b", "r13b",   "r14b",   "r15b",

  "ax",   "cx",   "dx",   "bx",
  "sp",   "bp",   "si",   "di",
  "r8w",  "r9w",    "r10w",   "r11w",
  "r12w", "r13w"  , "r14w",   "r15w",
  
  "eax",  "ecx",    "edx",    "ebx",
  "esp",  "ebp",    "esi",    "edi",
  "r8d",  "r9d",    "r10d",   "r11d",
  "r12d", "r13d",   "r14d",   "r15d",
  
  "rax",  "rcx",    "rdx",    "rbx",
  "rsp",  "rbp",    "rsi",    "rdi",
  "r8",   "r9",   "r10",    "r11",
  "r12",  "r13",    "r14",    "r15",

  "es",   "cs",   "ss",   "ds",
  "fs",   "gs", 

  "cr0",  "cr1",    "cr2",    "cr3",
  "cr4",  "cr5",    "cr6",    "cr7",
  "cr8",  "cr9",    "cr10",   "cr11",
  "cr12", "cr13",   "cr14",   "cr15",
  
  "dr0",  "dr1",    "dr2",    "dr3",
  "dr4",  "dr5",    "dr6",    "dr7",
  "dr8",  "dr9",    "dr10",   "dr11",
  "dr12", "dr13",   "dr14",   "dr15",

  "mm0",  "mm1",    "mm2",    "mm3",
  "mm4",  "mm5",    "mm6",    "mm7",

  "st0",  "st1",    "st2",    "st3",
  "st4",  "st5",    "st6",    "st7", 

  "xmm0", "xmm1",   "xmm2",   "xmm3",
  "xmm4", "xmm5",   "xmm6",   "xmm7",
  "xmm8", "xmm9",   "xmm10",  "xmm11",
  "xmm12",  "xmm13",  "xmm14",  "xmm15",

  "rip"
};


uint64_t
ud_syn_rel_target(struct ud *u, struct ud_operand *opr)
{
  const uint64_t trunc_mask = 0xffffffffffffffffull >> (64 - u->opr_mode);
  switch (opr->size) {
  case 8 : return (u->pc + opr->lval.sbyte)  & trunc_mask;
  case 16: return (u->pc + opr->lval.sword)  & trunc_mask;
  case 32: return (u->pc + opr->lval.sdword) & trunc_mask;
  default: UD_ASSERT(!"invalid relative offset size.");
    return 0ull;
  }
}


/*
 * asmprintf
 *    Printf style function for printing translated assembly
 *    output. Returns the number of characters written and
 *    moves the buffer pointer forward. On an overflow,
 *    returns a negative number and truncates the output.
 */
int
ud_asmprintf(struct ud *u, const char *fmt, ...)
{
  int ret;
  int avail;
  va_list ap;
  va_start(ap, fmt);
  avail = u->asm_buf_size - u->asm_buf_fill - 1 /* nullchar */;
  ret = vsnprintf((char*) u->asm_buf + u->asm_buf_fill, avail, fmt, ap);
  if (ret < 0 || ret > avail) {
      u->asm_buf_fill = u->asm_buf_size - 1;
  } else {
      u->asm_buf_fill += ret;
  }
  va_end(ap);
  return ret;
}


void
ud_syn_print_addr(struct ud *u, uint64_t addr)
{
  const char *name = NULL;
  if (u->sym_resolver) {
    int64_t offset = 0;
    name = u->sym_resolver(u, addr, &offset);
    if (name) {
      if (offset) {
        ud_asmprintf(u, "%s%+" FMT64 "d", name, offset);
      } else {
        ud_asmprintf(u, "%s", name);
      }
      return;
    }
  }
  ud_asmprintf(u, "0x%" FMT64 "x", addr);
}


void
ud_syn_print_imm(struct ud* u, const struct ud_operand *op)
{
  uint64_t v;
  if (op->_oprcode == OP_sI && op->size != u->opr_mode) {
    if (op->size == 8) {
      v = (int64_t)op->lval.sbyte;
    } else {
      UD_ASSERT(op->size == 32);
      v = (int64_t)op->lval.sdword;
    }
    if (u->opr_mode < 64) {
      v = v & ((1ull << u->opr_mode) - 1ull);
    }
  } else {
    switch (op->size) {
    case 8 : v = op->lval.ubyte;  break;
    case 16: v = op->lval.uword;  break;
    case 32: v = op->lval.udword; break;
    case 64: v = op->lval.uqword; break;
    default: UD_ASSERT(!"invalid offset"); v = 0; /* keep cc happy */
    }
  }
  ud_asmprintf(u, "0x%" FMT64 "x", v);
}


void
ud_syn_print_mem_disp(struct ud* u, const struct ud_operand *op, int sign)
{
  UD_ASSERT(op->offset != 0);
 if (op->base == UD_NONE && op->index == UD_NONE) {
    uint64_t v;
    UD_ASSERT(op->scale == UD_NONE && op->offset != 8);
    /* unsigned mem-offset */
    switch (op->offset) {
    case 16: v = op->lval.uword;  break;
    case 32: v = op->lval.udword; break;
    case 64: v = op->lval.uqword; break;
    default: UD_ASSERT(!"invalid offset"); v = 0; /* keep cc happy */
    }
    ud_asmprintf(u, "0x%" FMT64 "x", v);
  } else {
    int64_t v;
    UD_ASSERT(op->offset != 64);
    switch (op->offset) {
    case 8 : v = op->lval.sbyte;  break;
    case 16: v = op->lval.sword;  break;
    case 32: v = op->lval.sdword; break;
    default: UD_ASSERT(!"invalid offset"); v = 0; /* keep cc happy */
    }
    if (v < 0) {
      ud_asmprintf(u, "-0x%" FMT64 "x", -v);
    } else if (v > 0) {
      ud_asmprintf(u, "%s0x%" FMT64 "x", sign? "+" : "", v);
    }
  }
}

/* END syn.c */
/* BEGIN itab.c */

/* itab.c -- generated by udis86:scripts/ud_itab.py, do no edit */

#define GROUP(n) (0x8000 | (n))


static const uint16_t ud_itab__1[] = {
  /*  0 */           7,           0,
};

static const uint16_t ud_itab__2[] = {
  /*  0 */           8,           0,
};

static const uint16_t ud_itab__3[] = {
  /*  0 */          15,           0,
};

static const uint16_t ud_itab__6[] = {
  /*  0 */          16,           0,           0,           0,
};

static const uint16_t ud_itab__7[] = {
  /*  0 */          17,           0,           0,           0,
};

static const uint16_t ud_itab__8[] = {
  /*  0 */          18,           0,           0,           0,
};

static const uint16_t ud_itab__9[] = {
  /*  0 */          19,           0,           0,           0,
};

static const uint16_t ud_itab__10[] = {
  /*  0 */          20,           0,           0,           0,
};

static const uint16_t ud_itab__11[] = {
  /*  0 */          21,           0,           0,           0,
};

static const uint16_t ud_itab__5[] = {
  /*  0 */    GROUP(6),    GROUP(7),    GROUP(8),    GROUP(9),
  /*  4 */   GROUP(10),   GROUP(11),           0,           0,
};

static const uint16_t ud_itab__15[] = {
  /*  0 */          22,           0,
};

static const uint16_t ud_itab__14[] = {
  /*  0 */   GROUP(15),           0,           0,           0,
};

static const uint16_t ud_itab__17[] = {
  /*  0 */          23,           0,
};

static const uint16_t ud_itab__16[] = {
  /*  0 */   GROUP(17),           0,           0,           0,
};

static const uint16_t ud_itab__19[] = {
  /*  0 */          24,           0,
};

static const uint16_t ud_itab__18[] = {
  /*  0 */   GROUP(19),           0,           0,           0,
};

static const uint16_t ud_itab__21[] = {
  /*  0 */          25,           0,
};

static const uint16_t ud_itab__20[] = {
  /*  0 */   GROUP(21),           0,           0,           0,
};

static const uint16_t ud_itab__23[] = {
  /*  0 */          26,           0,
};

static const uint16_t ud_itab__22[] = {
  /*  0 */   GROUP(23),           0,           0,           0,
};

static const uint16_t ud_itab__25[] = {
  /*  0 */          27,           0,
};

static const uint16_t ud_itab__24[] = {
  /*  0 */   GROUP(25),           0,           0,           0,
};

static const uint16_t ud_itab__27[] = {
  /*  0 */          28,           0,
};

static const uint16_t ud_itab__26[] = {
  /*  0 */   GROUP(27),           0,           0,           0,
};

static const uint16_t ud_itab__13[] = {
  /*  0 */   GROUP(14),   GROUP(16),   GROUP(18),   GROUP(20),
  /*  4 */   GROUP(22),           0,   GROUP(24),   GROUP(26),
};

static const uint16_t ud_itab__32[] = {
  /*  0 */           0,          29,           0,
};

static const uint16_t ud_itab__31[] = {
  /*  0 */           0,   GROUP(32),
};

static const uint16_t ud_itab__30[] = {
  /*  0 */   GROUP(31),           0,           0,           0,
};

static const uint16_t ud_itab__35[] = {
  /*  0 */           0,          30,           0,
};

static const uint16_t ud_itab__34[] = {
  /*  0 */           0,   GROUP(35),
};

static const uint16_t ud_itab__33[] = {
  /*  0 */   GROUP(34),           0,           0,           0,
};

static const uint16_t ud_itab__38[] = {
  /*  0 */           0,          31,           0,
};

static const uint16_t ud_itab__37[] = {
  /*  0 */           0,   GROUP(38),
};

static const uint16_t ud_itab__36[] = {
  /*  0 */   GROUP(37),           0,           0,           0,
};

static const uint16_t ud_itab__41[] = {
  /*  0 */           0,          32,           0,
};

static const uint16_t ud_itab__40[] = {
  /*  0 */           0,   GROUP(41),
};

static const uint16_t ud_itab__39[] = {
  /*  0 */   GROUP(40),           0,           0,           0,
};

static const uint16_t ud_itab__29[] = {
  /*  0 */           0,   GROUP(30),   GROUP(33),   GROUP(36),
  /*  4 */   GROUP(39),           0,           0,           0,
};

static const uint16_t ud_itab__44[] = {
  /*  0 */           0,          33,
};

static const uint16_t ud_itab__43[] = {
  /*  0 */   GROUP(44),           0,           0,           0,
};

static const uint16_t ud_itab__46[] = {
  /*  0 */           0,          34,
};

static const uint16_t ud_itab__45[] = {
  /*  0 */   GROUP(46),           0,           0,           0,
};

static const uint16_t ud_itab__42[] = {
  /*  0 */   GROUP(43),   GROUP(45),           0,           0,
  /*  4 */           0,           0,           0,           0,
};

static const uint16_t ud_itab__49[] = {
  /*  0 */           0,          35,
};

static const uint16_t ud_itab__48[] = {
  /*  0 */   GROUP(49),           0,           0,           0,
};

static const uint16_t ud_itab__51[] = {
  /*  0 */           0,          36,
};

static const uint16_t ud_itab__50[] = {
  /*  0 */   GROUP(51),           0,           0,           0,
};

static const uint16_t ud_itab__47[] = {
  /*  0 */   GROUP(48),   GROUP(50),           0,           0,
  /*  4 */           0,           0,           0,           0,
};

static const uint16_t ud_itab__55[] = {
  /*  0 */          37,           0,           0,
};

static const uint16_t ud_itab__54[] = {
  /*  0 */           0,   GROUP(55),
};

static const uint16_t ud_itab__53[] = {
  /*  0 */   GROUP(54),           0,           0,           0,
};

static const uint16_t ud_itab__58[] = {
  /*  0 */          38,           0,           0,
};

static const uint16_t ud_itab__57[] = {
  /*  0 */           0,   GROUP(58),
};

static const uint16_t ud_itab__56[] = {
  /*  0 */   GROUP(57),           0,           0,           0,
};

static const uint16_t ud_itab__61[] = {
  /*  0 */          39,           0,           0,
};

static const uint16_t ud_itab__60[] = {
  /*  0 */           0,   GROUP(61),
};

static const uint16_t ud_itab__59[] = {
  /*  0 */   GROUP(60),           0,           0,           0,
};

static const uint16_t ud_itab__64[] = {
  /*  0 */          40,           0,           0,
};

static const uint16_t ud_itab__63[] = {
  /*  0 */           0,   GROUP(64),
};

static const uint16_t ud_itab__62[] = {
  /*  0 */   GROUP(63),           0,           0,           0,
};

static const uint16_t ud_itab__67[] = {
  /*  0 */          41,           0,           0,
};

static const uint16_t ud_itab__66[] = {
  /*  0 */           0,   GROUP(67),
};

static const uint16_t ud_itab__65[] = {
  /*  0 */   GROUP(66),           0,           0,           0,
};

static const uint16_t ud_itab__70[] = {
  /*  0 */          42,           0,           0,
};

static const uint16_t ud_itab__69[] = {
  /*  0 */           0,   GROUP(70),
};

static const uint16_t ud_itab__68[] = {
  /*  0 */   GROUP(69),           0,           0,           0,
};

static const uint16_t ud_itab__73[] = {
  /*  0 */          43,           0,           0,
};

static const uint16_t ud_itab__72[] = {
  /*  0 */           0,   GROUP(73),
};

static const uint16_t ud_itab__71[] = {
  /*  0 */   GROUP(72),           0,           0,           0,
};

static const uint16_t ud_itab__76[] = {
  /*  0 */          44,           0,           0,
};

static const uint16_t ud_itab__75[] = {
  /*  0 */           0,   GROUP(76),
};

static const uint16_t ud_itab__74[] = {
  /*  0 */   GROUP(75),           0,           0,           0,
};

static const uint16_t ud_itab__52[] = {
  /*  0 */   GROUP(53),   GROUP(56),   GROUP(59),   GROUP(62),
  /*  4 */   GROUP(65),   GROUP(68),   GROUP(71),   GROUP(74),
};

static const uint16_t ud_itab__78[] = {
  /*  0 */           0,          45,
};

static const uint16_t ud_itab__77[] = {
  /*  0 */   GROUP(78),           0,           0,           0,
};

static const uint16_t ud_itab__80[] = {
  /*  0 */           0,          46,
};

static const uint16_t ud_itab__79[] = {
  /*  0 */   GROUP(80),           0,           0,           0,
};

static const uint16_t ud_itab__83[] = {
  /*  0 */           0,          47,
};

static const uint16_t ud_itab__82[] = {
  /*  0 */   GROUP(83),           0,           0,           0,
};

static const uint16_t ud_itab__86[] = {
  /*  0 */          48,           0,           0,
};

static const uint16_t ud_itab__85[] = {
  /*  0 */           0,   GROUP(86),
};

static const uint16_t ud_itab__84[] = {
  /*  0 */   GROUP(85),           0,           0,           0,
};

static const uint16_t ud_itab__81[] = {
  /*  0 */   GROUP(82),   GROUP(84),           0,           0,
  /*  4 */           0,           0,           0,           0,
};

static const uint16_t ud_itab__28[] = {
  /*  0 */   GROUP(29),   GROUP(42),   GROUP(47),   GROUP(52),
  /*  4 */   GROUP(77),           0,   GROUP(79),   GROUP(81),
};

static const uint16_t ud_itab__12[] = {
  /*  0 */   GROUP(13),   GROUP(28),
};

static const uint16_t ud_itab__87[] = {
  /*  0 */          49,           0,           0,           0,
};

static const uint16_t ud_itab__88[] = {
  /*  0 */          50,           0,           0,           0,
};

static const uint16_t ud_itab__89[] = {
  /*  0 */          51,           0,           0,           0,
};

static const uint16_t ud_itab__90[] = {
  /*  0 */          52,           0,           0,           0,
};

static const uint16_t ud_itab__91[] = {
  /*  0 */          53,           0,           0,           0,
};

static const uint16_t ud_itab__92[] = {
  /*  0 */          54,           0,           0,           0,
};

static const uint16_t ud_itab__93[] = {
  /*  0 */          55,           0,           0,           0,
};

static const uint16_t ud_itab__94[] = {
  /*  0 */          56,           0,           0,           0,
};

static const uint16_t ud_itab__96[] = {
  /*  0 */          57,           0,           0,           0,
};

static const uint16_t ud_itab__97[] = {
  /*  0 */          58,           0,           0,           0,
};

static const uint16_t ud_itab__98[] = {
  /*  0 */          59,           0,           0,           0,
};

static const uint16_t ud_itab__99[] = {
  /*  0 */          60,           0,           0,           0,
};

static const uint16_t ud_itab__100[] = {
  /*  0 */          61,           0,           0,           0,
};

static const uint16_t ud_itab__101[] = {
  /*  0 */          62,           0,           0,           0,
};

static const uint16_t ud_itab__102[] = {
  /*  0 */          63,           0,           0,           0,
};

static const uint16_t ud_itab__103[] = {
  /*  0 */          64,           0,           0,           0,
};

static const uint16_t ud_itab__95[] = {
  /*  0 */   GROUP(96),   GROUP(97),   GROUP(98),   GROUP(99),
  /*  4 */  GROUP(100),  GROUP(101),  GROUP(102),  GROUP(103),
};

static const uint16_t ud_itab__104[] = {
  /*  0 */          65,           0,           0,           0,
};

static const uint16_t ud_itab__105[] = {
  /*  0 */           0,           0,           0,           0,
  /*  4 */           0,           0,           0,           0,
  /*  8 */           0,           0,           0,           0,
  /*  c */          66,          67,           0,           0,
  /* 10 */           0,           0,           0,           0,
  /* 14 */           0,           0,           0,           0,
  /* 18 */           0,           0,           0,           0,
  /* 1c */          68,          69,           0,           0,
  /* 20 */           0,           0,           0,           0,
  /* 24 */           0,           0,           0,           0,
  /* 28 */           0,           0,           0,           0,
  /* 2c */           0,           0,           0,           0,
  /* 30 */           0,           0,           0,           0,
  /* 34 */           0,           0,           0,           0,
  /* 38 */           0,           0,           0,           0,
  /* 3c */           0,           0,           0,           0,
  /* 40 */           0,           0,           0,           0,
  /* 44 */           0,           0,           0,           0,
  /* 48 */           0,           0,           0,           0,
  /* 4c */           0,           0,           0,           0,
  /* 50 */           0,           0,           0,           0,
  /* 54 */           0,           0,           0,           0,
  /* 58 */           0,           0,           0,           0,
  /* 5c */           0,           0,           0,           0,
  /* 60 */           0,           0,           0,           0,
  /* 64 */           0,           0,           0,           0,
  /* 68 */           0,           0,           0,           0,
  /* 6c */           0,           0,           0,           0,
  /* 70 */           0,           0,           0,           0,
  /* 74 */           0,           0,           0,           0,
  /* 78 */           0,           0,           0,           0,
  /* 7c */           0,           0,           0,           0,
  /* 80 */           0,           0,           0,           0,
  /* 84 */           0,           0,           0,           0,
  /* 88 */           0,           0,          70,           0,
  /* 8c */           0,           0,          71,           0,
  /* 90 */          72,           0,           0,           0,
  /* 94 */          73,           0,          74,          75,
  /* 98 */           0,           0,          76,           0,
  /* 9c */           0,           0,          77,           0,
  /* a0 */          78,           0,           0,           0,
  /* a4 */          79,           0,          80,          81,
  /* a8 */           0,           0,          82,           0,
  /* ac */           0,           0,          83,           0,
  /* b0 */          84,           0,           0,           0,
  /* b4 */          85,           0,          86,          87,
  /* b8 */           0,           0,           0,          88,
  /* bc */           0,           0,           0,          89,
  /* c0 */           0,           0,           0,           0,
  /* c4 */           0,           0,           0,           0,
  /* c8 */           0,           0,           0,           0,
  /* cc */           0,           0,           0,           0,
  /* d0 */           0,           0,           0,           0,
  /* d4 */           0,           0,           0,           0,
  /* d8 */           0,           0,           0,           0,
  /* dc */           0,           0,           0,           0,
  /* e0 */           0,           0,           0,           0,
  /* e4 */           0,           0,           0,           0,
  /* e8 */           0,           0,           0,           0,
  /* ec */           0,           0,           0,           0,
  /* f0 */           0,           0,           0,           0,
  /* f4 */           0,           0,           0,           0,
  /* f8 */           0,           0,           0,           0,
  /* fc */           0,           0,           0,           0,
};

static const uint16_t ud_itab__106[] = {
  /*  0 */          90,          91,          92,          93,
};

static const uint16_t ud_itab__107[] = {
  /*  0 */          94,          95,          96,          97,
};

static const uint16_t ud_itab__110[] = {
  /*  0 */          98,           0,
};

static const uint16_t ud_itab__111[] = {
  /*  0 */          99,           0,
};

static const uint16_t ud_itab__112[] = {
  /*  0 */         100,           0,
};

static const uint16_t ud_itab__113[] = {
  /*  0 */         101,           0,
};

static const uint16_t ud_itab__109[] = {
  /*  0 */  GROUP(110),  GROUP(111),  GROUP(112),  GROUP(113),
};

static const uint16_t ud_itab__115[] = {
  /*  0 */           0,         102,
};

static const uint16_t ud_itab__116[] = {
  /*  0 */           0,         103,
};

static const uint16_t ud_itab__117[] = {
  /*  0 */           0,         104,
};

static const uint16_t ud_itab__114[] = {
  /*  0 */  GROUP(115),  GROUP(116),  GROUP(117),           0,
};

static const uint16_t ud_itab__108[] = {
  /*  0 */  GROUP(109),  GROUP(114),
};

static const uint16_t ud_itab__118[] = {
  /*  0 */         105,           0,           0,         106,
};

static const uint16_t ud_itab__119[] = {
  /*  0 */         107,           0,           0,         108,
};

static const uint16_t ud_itab__120[] = {
  /*  0 */         109,           0,           0,         110,
};

static const uint16_t ud_itab__123[] = {
  /*  0 */         111,           0,
};

static const uint16_t ud_itab__124[] = {
  /*  0 */         112,           0,
};

static const uint16_t ud_itab__125[] = {
  /*  0 */         113,           0,
};

static const uint16_t ud_itab__122[] = {
  /*  0 */  GROUP(123),           0,  GROUP(124),  GROUP(125),
};

static const uint16_t ud_itab__127[] = {
  /*  0 */           0,         114,
};

static const uint16_t ud_itab__128[] = {
  /*  0 */           0,         115,
};

static const uint16_t ud_itab__126[] = {
  /*  0 */  GROUP(127),           0,  GROUP(128),           0,
};

static const uint16_t ud_itab__121[] = {
  /*  0 */  GROUP(122),  GROUP(126),
};

static const uint16_t ud_itab__129[] = {
  /*  0 */         116,           0,           0,         117,
};

static const uint16_t ud_itab__131[] = {
  /*  0 */         118,           0,           0,           0,
};

static const uint16_t ud_itab__132[] = {
  /*  0 */         119,           0,           0,           0,
};

static const uint16_t ud_itab__133[] = {
  /*  0 */         120,           0,           0,           0,
};

static const uint16_t ud_itab__134[] = {
  /*  0 */         121,           0,           0,           0,
};

static const uint16_t ud_itab__130[] = {
  /*  0 */  GROUP(131),  GROUP(132),  GROUP(133),  GROUP(134),
  /*  4 */           0,           0,           0,           0,
};

static const uint16_t ud_itab__135[] = {
  /*  0 */         122,           0,           0,           0,
};

static const uint16_t ud_itab__136[] = {
  /*  0 */         123,           0,           0,           0,
};

static const uint16_t ud_itab__137[] = {
  /*  0 */         124,           0,           0,           0,
};

static const uint16_t ud_itab__138[] = {
  /*  0 */         125,           0,           0,           0,
};

static const uint16_t ud_itab__139[] = {
  /*  0 */         126,           0,           0,           0,
};

static const uint16_t ud_itab__140[] = {
  /*  0 */         127,           0,           0,           0,
};

static const uint16_t ud_itab__141[] = {
  /*  0 */         128,           0,           0,           0,
};

static const uint16_t ud_itab__142[] = {
  /*  0 */         129,           0,           0,           0,
};

static const uint16_t ud_itab__143[] = {
  /*  0 */         130,           0,           0,           0,
};

static const uint16_t ud_itab__144[] = {
  /*  0 */         131,           0,           0,           0,
};

static const uint16_t ud_itab__145[] = {
  /*  0 */         132,           0,           0,           0,
};

static const uint16_t ud_itab__146[] = {
  /*  0 */         133,           0,           0,         134,
};

static const uint16_t ud_itab__147[] = {
  /*  0 */         135,           0,           0,         136,
};

static const uint16_t ud_itab__148[] = {
  /*  0 */         137,         138,         139,         140,
};

static const uint16_t ud_itab__149[] = {
  /*  0 */         141,           0,           0,         142,
};

static const uint16_t ud_itab__150[] = {
  /*  0 */         143,         144,         145,         146,
};

static const uint16_t ud_itab__151[] = {
  /*  0 */         147,         148,         149,         150,
};

static const uint16_t ud_itab__152[] = {
  /*  0 */         151,           0,           0,         152,
};

static const uint16_t ud_itab__153[] = {
  /*  0 */         153,           0,           0,         154,
};

static const uint16_t ud_itab__154[] = {
  /*  0 */         155,           0,           0,           0,
};

static const uint16_t ud_itab__155[] = {
  /*  0 */         156,           0,           0,           0,
};

static const uint16_t ud_itab__156[] = {
  /*  0 */         157,           0,           0,           0,
};

static const uint16_t ud_itab__157[] = {
  /*  0 */         158,           0,           0,           0,
};

static const uint16_t ud_itab__160[] = {
  /*  0 */           0,         160,           0,
};

static const uint16_t ud_itab__159[] = {
  /*  0 */         159,  GROUP(160),
};

static const uint16_t ud_itab__158[] = {
  /*  0 */  GROUP(159),           0,           0,           0,
};

static const uint16_t ud_itab__163[] = {
  /*  0 */           0,         162,           0,
};

static const uint16_t ud_itab__162[] = {
  /*  0 */         161,  GROUP(163),
};

static const uint16_t ud_itab__161[] = {
  /*  0 */  GROUP(162),           0,           0,           0,
};

static const uint16_t ud_itab__164[] = {
  /*  0 */         163,           0,           0,           0,
};

static const uint16_t ud_itab__166[] = {
  /*  0 */         164,           0,           0,         165,
};

static const uint16_t ud_itab__167[] = {
  /*  0 */         166,           0,           0,         167,
};

static const uint16_t ud_itab__168[] = {
  /*  0 */         168,           0,           0,         169,
};

static const uint16_t ud_itab__169[] = {
  /*  0 */         170,           0,           0,         171,
};

static const uint16_t ud_itab__170[] = {
  /*  0 */         172,           0,           0,         173,
};

static const uint16_t ud_itab__171[] = {
  /*  0 */         174,           0,           0,         175,
};

static const uint16_t ud_itab__172[] = {
  /*  0 */         176,           0,           0,         177,
};

static const uint16_t ud_itab__173[] = {
  /*  0 */         178,           0,           0,         179,
};

static const uint16_t ud_itab__174[] = {
  /*  0 */         180,           0,           0,         181,
};

static const uint16_t ud_itab__175[] = {
  /*  0 */         182,           0,           0,         183,
};

static const uint16_t ud_itab__176[] = {
  /*  0 */         184,           0,           0,         185,
};

static const uint16_t ud_itab__177[] = {
  /*  0 */         186,           0,           0,         187,
};

static const uint16_t ud_itab__178[] = {
  /*  0 */           0,           0,           0,         188,
};

static const uint16_t ud_itab__179[] = {
  /*  0 */           0,           0,           0,         189,
};

static const uint16_t ud_itab__180[] = {
  /*  0 */           0,           0,           0,         190,
};

static const uint16_t ud_itab__181[] = {
  /*  0 */           0,           0,           0,         191,
};

static const uint16_t ud_itab__182[] = {
  /*  0 */         192,           0,           0,         193,
};

static const uint16_t ud_itab__183[] = {
  /*  0 */         194,           0,           0,         195,
};

static const uint16_t ud_itab__184[] = {
  /*  0 */         196,           0,           0,         197,
};

static const uint16_t ud_itab__185[] = {
  /*  0 */           0,           0,           0,         198,
};

static const uint16_t ud_itab__186[] = {
  /*  0 */           0,           0,           0,         199,
};

static const uint16_t ud_itab__187[] = {
  /*  0 */           0,           0,           0,         200,
};

static const uint16_t ud_itab__188[] = {
  /*  0 */           0,           0,           0,         201,
};

static const uint16_t ud_itab__189[] = {
  /*  0 */           0,           0,           0,         202,
};

static const uint16_t ud_itab__190[] = {
  /*  0 */           0,           0,           0,         203,
};

static const uint16_t ud_itab__191[] = {
  /*  0 */           0,           0,           0,         204,
};

static const uint16_t ud_itab__192[] = {
  /*  0 */           0,           0,           0,         205,
};

static const uint16_t ud_itab__193[] = {
  /*  0 */           0,           0,           0,         206,
};

static const uint16_t ud_itab__194[] = {
  /*  0 */           0,           0,           0,         207,
};

static const uint16_t ud_itab__195[] = {
  /*  0 */           0,           0,           0,         208,
};

static const uint16_t ud_itab__196[] = {
  /*  0 */           0,           0,           0,         209,
};

static const uint16_t ud_itab__197[] = {
  /*  0 */           0,           0,           0,         210,
};

static const uint16_t ud_itab__198[] = {
  /*  0 */           0,           0,           0,         211,
};

static const uint16_t ud_itab__199[] = {
  /*  0 */           0,           0,           0,         212,
};

static const uint16_t ud_itab__200[] = {
  /*  0 */           0,           0,           0,         213,
};

static const uint16_t ud_itab__201[] = {
  /*  0 */           0,           0,           0,         214,
};

static const uint16_t ud_itab__202[] = {
  /*  0 */           0,           0,           0,         215,
};

static const uint16_t ud_itab__203[] = {
  /*  0 */           0,           0,           0,         216,
};

static const uint16_t ud_itab__204[] = {
  /*  0 */           0,           0,           0,         217,
};

static const uint16_t ud_itab__205[] = {
  /*  0 */           0,           0,           0,         218,
};

static const uint16_t ud_itab__206[] = {
  /*  0 */           0,           0,           0,         219,
};

static const uint16_t ud_itab__207[] = {
  /*  0 */           0,           0,           0,         220,
};

static const uint16_t ud_itab__208[] = {
  /*  0 */           0,           0,           0,         221,
};

static const uint16_t ud_itab__209[] = {
  /*  0 */           0,           0,           0,         222,
};

static const uint16_t ud_itab__210[] = {
  /*  0 */           0,           0,           0,         223,
};

static const uint16_t ud_itab__211[] = {
  /*  0 */           0,           0,           0,         224,
};

static const uint16_t ud_itab__214[] = {
  /*  0 */           0,         225,           0,
};

static const uint16_t ud_itab__213[] = {
  /*  0 */           0,  GROUP(214),
};

static const uint16_t ud_itab__212[] = {
  /*  0 */           0,           0,           0,  GROUP(213),
};

static const uint16_t ud_itab__217[] = {
  /*  0 */           0,         226,           0,
};

static const uint16_t ud_itab__216[] = {
  /*  0 */           0,  GROUP(217),
};

static const uint16_t ud_itab__215[] = {
  /*  0 */           0,           0,           0,  GROUP(216),
};

static const uint16_t ud_itab__218[] = {
  /*  0 */           0,           0,           0,         227,
};

static const uint16_t ud_itab__219[] = {
  /*  0 */           0,           0,           0,         228,
};

static const uint16_t ud_itab__220[] = {
  /*  0 */           0,           0,           0,         229,
};

static const uint16_t ud_itab__221[] = {
  /*  0 */           0,           0,           0,         230,
};

static const uint16_t ud_itab__222[] = {
  /*  0 */           0,           0,           0,         231,
};

static const uint16_t ud_itab__223[] = {
  /*  0 */         232,         233,           0,           0,
};

static const uint16_t ud_itab__224[] = {
  /*  0 */         234,         235,           0,           0,
};

static const uint16_t ud_itab__165[] = {
  /*  0 */  GROUP(166),  GROUP(167),  GROUP(168),  GROUP(169),
  /*  4 */  GROUP(170),  GROUP(171),  GROUP(172),  GROUP(173),
  /*  8 */  GROUP(174),  GROUP(175),  GROUP(176),  GROUP(177),
  /*  c */           0,           0,           0,           0,
  /* 10 */  GROUP(178),           0,           0,           0,
  /* 14 */  GROUP(179),  GROUP(180),           0,  GROUP(181),
  /* 18 */           0,           0,           0,           0,
  /* 1c */  GROUP(182),  GROUP(183),  GROUP(184),           0,
  /* 20 */  GROUP(185),  GROUP(186),  GROUP(187),  GROUP(188),
  /* 24 */  GROUP(189),  GROUP(190),           0,           0,
  /* 28 */  GROUP(191),  GROUP(192),  GROUP(193),  GROUP(194),
  /* 2c */           0,           0,           0,           0,
  /* 30 */  GROUP(195),  GROUP(196),  GROUP(197),  GROUP(198),
  /* 34 */  GROUP(199),  GROUP(200),           0,  GROUP(201),
  /* 38 */  GROUP(202),  GROUP(203),  GROUP(204),  GROUP(205),
  /* 3c */  GROUP(206),  GROUP(207),  GROUP(208),  GROUP(209),
  /* 40 */  GROUP(210),  GROUP(211),           0,           0,
  /* 44 */           0,           0,           0,           0,
  /* 48 */           0,           0,           0,           0,
  /* 4c */           0,           0,           0,           0,
  /* 50 */           0,           0,           0,           0,
  /* 54 */           0,           0,           0,           0,
  /* 58 */           0,           0,           0,           0,
  /* 5c */           0,           0,           0,           0,
  /* 60 */           0,           0,           0,           0,
  /* 64 */           0,           0,           0,           0,
  /* 68 */           0,           0,           0,           0,
  /* 6c */           0,           0,           0,           0,
  /* 70 */           0,           0,           0,           0,
  /* 74 */           0,           0,           0,           0,
  /* 78 */           0,           0,           0,           0,
  /* 7c */           0,           0,           0,           0,
  /* 80 */  GROUP(212),  GROUP(215),           0,           0,
  /* 84 */           0,           0,           0,           0,
  /* 88 */           0,           0,           0,           0,
  /* 8c */           0,           0,           0,           0,
  /* 90 */           0,           0,           0,           0,
  /* 94 */           0,           0,           0,           0,
  /* 98 */           0,           0,           0,           0,
  /* 9c */           0,           0,           0,           0,
  /* a0 */           0,           0,           0,           0,
  /* a4 */           0,           0,           0,           0,
  /* a8 */           0,           0,           0,           0,
  /* ac */           0,           0,           0,           0,
  /* b0 */           0,           0,           0,           0,
  /* b4 */           0,           0,           0,           0,
  /* b8 */           0,           0,           0,           0,
  /* bc */           0,           0,           0,           0,
  /* c0 */           0,           0,           0,           0,
  /* c4 */           0,           0,           0,           0,
  /* c8 */           0,           0,           0,           0,
  /* cc */           0,           0,           0,           0,
  /* d0 */           0,           0,           0,           0,
  /* d4 */           0,           0,           0,           0,
  /* d8 */           0,           0,           0,  GROUP(218),
  /* dc */  GROUP(219),  GROUP(220),  GROUP(221),  GROUP(222),
  /* e0 */           0,           0,           0,           0,
  /* e4 */           0,           0,           0,           0,
  /* e8 */           0,           0,           0,           0,
  /* ec */           0,           0,           0,           0,
  /* f0 */  GROUP(223),  GROUP(224),           0,           0,
  /* f4 */           0,           0,           0,           0,
  /* f8 */           0,           0,           0,           0,
  /* fc */           0,           0,           0,           0,
};

static const uint16_t ud_itab__226[] = {
  /*  0 */           0,           0,           0,         236,
};

static const uint16_t ud_itab__227[] = {
  /*  0 */           0,           0,           0,         237,
};

static const uint16_t ud_itab__228[] = {
  /*  0 */           0,           0,           0,         238,
};

static const uint16_t ud_itab__229[] = {
  /*  0 */           0,           0,           0,         239,
};

static const uint16_t ud_itab__230[] = {
  /*  0 */           0,           0,           0,         240,
};

static const uint16_t ud_itab__231[] = {
  /*  0 */           0,           0,           0,         241,
};

static const uint16_t ud_itab__232[] = {
  /*  0 */           0,           0,           0,         242,
};

static const uint16_t ud_itab__233[] = {
  /*  0 */         243,           0,           0,         244,
};

static const uint16_t ud_itab__234[] = {
  /*  0 */           0,           0,           0,         245,
};

static const uint16_t ud_itab__235[] = {
  /*  0 */           0,           0,           0,         246,
};

static const uint16_t ud_itab__237[] = {
  /*  0 */         247,         248,         249,
};

static const uint16_t ud_itab__236[] = {
  /*  0 */           0,           0,           0,  GROUP(237),
};

static const uint16_t ud_itab__238[] = {
  /*  0 */           0,           0,           0,         250,
};

static const uint16_t ud_itab__239[] = {
  /*  0 */           0,           0,           0,         251,
};

static const uint16_t ud_itab__240[] = {
  /*  0 */           0,           0,           0,         252,
};

static const uint16_t ud_itab__242[] = {
  /*  0 */         253,         254,         255,
};

static const uint16_t ud_itab__241[] = {
  /*  0 */           0,           0,           0,  GROUP(242),
};

static const uint16_t ud_itab__243[] = {
  /*  0 */           0,           0,           0,         256,
};

static const uint16_t ud_itab__244[] = {
  /*  0 */           0,           0,           0,         257,
};

static const uint16_t ud_itab__245[] = {
  /*  0 */           0,           0,           0,         258,
};

static const uint16_t ud_itab__246[] = {
  /*  0 */           0,           0,           0,         259,
};

static const uint16_t ud_itab__247[] = {
  /*  0 */           0,           0,           0,         260,
};

static const uint16_t ud_itab__248[] = {
  /*  0 */           0,           0,           0,         261,
};

static const uint16_t ud_itab__249[] = {
  /*  0 */           0,           0,           0,         262,
};

static const uint16_t ud_itab__250[] = {
  /*  0 */           0,           0,           0,         263,
};

static const uint16_t ud_itab__251[] = {
  /*  0 */           0,           0,           0,         264,
};

static const uint16_t ud_itab__225[] = {
  /*  0 */           0,           0,           0,           0,
  /*  4 */           0,           0,           0,           0,
  /*  8 */  GROUP(226),  GROUP(227),  GROUP(228),  GROUP(229),
  /*  c */  GROUP(230),  GROUP(231),  GROUP(232),  GROUP(233),
  /* 10 */           0,           0,           0,           0,
  /* 14 */  GROUP(234),  GROUP(235),  GROUP(236),  GROUP(238),
  /* 18 */           0,           0,           0,           0,
  /* 1c */           0,           0,           0,           0,
  /* 20 */  GROUP(239),  GROUP(240),  GROUP(241),           0,
  /* 24 */           0,           0,           0,           0,
  /* 28 */           0,           0,           0,           0,
  /* 2c */           0,           0,           0,           0,
  /* 30 */           0,           0,           0,           0,
  /* 34 */           0,           0,           0,           0,
  /* 38 */           0,           0,           0,           0,
  /* 3c */           0,           0,           0,           0,
  /* 40 */  GROUP(243),  GROUP(244),  GROUP(245),           0,
  /* 44 */  GROUP(246),           0,           0,           0,
  /* 48 */           0,           0,           0,           0,
  /* 4c */           0,           0,           0,           0,
  /* 50 */           0,           0,           0,           0,
  /* 54 */           0,           0,           0,           0,
  /* 58 */           0,           0,           0,           0,
  /* 5c */           0,           0,           0,           0,
  /* 60 */  GROUP(247),  GROUP(248),  GROUP(249),  GROUP(250),
  /* 64 */           0,           0,           0,           0,
  /* 68 */           0,           0,           0,           0,
  /* 6c */           0,           0,           0,           0,
  /* 70 */           0,           0,           0,           0,
  /* 74 */           0,           0,           0,           0,
  /* 78 */           0,           0,           0,           0,
  /* 7c */           0,           0,           0,           0,
  /* 80 */           0,           0,           0,           0,
  /* 84 */           0,           0,           0,           0,
  /* 88 */           0,           0,           0,           0,
  /* 8c */           0,           0,           0,           0,
  /* 90 */           0,           0,           0,           0,
  /* 94 */           0,           0,           0,           0,
  /* 98 */           0,           0,           0,           0,
  /* 9c */           0,           0,           0,           0,
  /* a0 */           0,           0,           0,           0,
  /* a4 */           0,           0,           0,           0,
  /* a8 */           0,           0,           0,           0,
  /* ac */           0,           0,           0,           0,
  /* b0 */           0,           0,           0,           0,
  /* b4 */           0,           0,           0,           0,
  /* b8 */           0,           0,           0,           0,
  /* bc */           0,           0,           0,           0,
  /* c0 */           0,           0,           0,           0,
  /* c4 */           0,           0,           0,           0,
  /* c8 */           0,           0,           0,           0,
  /* cc */           0,           0,           0,           0,
  /* d0 */           0,           0,           0,           0,
  /* d4 */           0,           0,           0,           0,
  /* d8 */           0,           0,           0,           0,
  /* dc */           0,           0,           0,  GROUP(251),
  /* e0 */           0,           0,           0,           0,
  /* e4 */           0,           0,           0,           0,
  /* e8 */           0,           0,           0,           0,
  /* ec */           0,           0,           0,           0,
  /* f0 */           0,           0,           0,           0,
  /* f4 */           0,           0,           0,           0,
  /* f8 */           0,           0,           0,           0,
  /* fc */           0,           0,           0,           0,
};

static const uint16_t ud_itab__252[] = {
  /*  0 */         265,           0,           0,           0,
};

static const uint16_t ud_itab__253[] = {
  /*  0 */         266,           0,           0,           0,
};

static const uint16_t ud_itab__254[] = {
  /*  0 */         267,           0,           0,           0,
};

static const uint16_t ud_itab__255[] = {
  /*  0 */         268,           0,           0,           0,
};

static const uint16_t ud_itab__256[] = {
  /*  0 */         269,           0,           0,           0,
};

static const uint16_t ud_itab__257[] = {
  /*  0 */         270,           0,           0,           0,
};

static const uint16_t ud_itab__258[] = {
  /*  0 */         271,           0,           0,           0,
};

static const uint16_t ud_itab__259[] = {
  /*  0 */         272,           0,           0,           0,
};

static const uint16_t ud_itab__260[] = {
  /*  0 */         273,           0,           0,           0,
};

static const uint16_t ud_itab__261[] = {
  /*  0 */         274,           0,           0,           0,
};

static const uint16_t ud_itab__262[] = {
  /*  0 */         275,           0,           0,           0,
};

static const uint16_t ud_itab__263[] = {
  /*  0 */         276,           0,           0,           0,
};

static const uint16_t ud_itab__264[] = {
  /*  0 */         277,           0,           0,           0,
};

static const uint16_t ud_itab__265[] = {
  /*  0 */         278,           0,           0,           0,
};

static const uint16_t ud_itab__266[] = {
  /*  0 */         279,           0,           0,           0,
};

static const uint16_t ud_itab__267[] = {
  /*  0 */         280,           0,           0,           0,
};

static const uint16_t ud_itab__268[] = {
  /*  0 */         281,           0,           0,         282,
};

static const uint16_t ud_itab__269[] = {
  /*  0 */         283,         284,         285,         286,
};

static const uint16_t ud_itab__270[] = {
  /*  0 */         287,           0,         288,           0,
};

static const uint16_t ud_itab__271[] = {
  /*  0 */         289,           0,         290,           0,
};

static const uint16_t ud_itab__272[] = {
  /*  0 */         291,           0,           0,         292,
};

static const uint16_t ud_itab__273[] = {
  /*  0 */         293,           0,           0,         294,
};

static const uint16_t ud_itab__274[] = {
  /*  0 */         295,           0,           0,         296,
};

static const uint16_t ud_itab__275[] = {
  /*  0 */         297,           0,           0,         298,
};

static const uint16_t ud_itab__276[] = {
  /*  0 */         299,         300,         301,         302,
};

static const uint16_t ud_itab__277[] = {
  /*  0 */         303,         304,         305,         306,
};

static const uint16_t ud_itab__278[] = {
  /*  0 */         307,         308,         309,         310,
};

static const uint16_t ud_itab__279[] = {
  /*  0 */         311,           0,         312,         313,
};

static const uint16_t ud_itab__280[] = {
  /*  0 */         314,         315,         316,         317,
};

static const uint16_t ud_itab__281[] = {
  /*  0 */         318,         319,         320,         321,
};

static const uint16_t ud_itab__282[] = {
  /*  0 */         322,         323,         324,         325,
};

static const uint16_t ud_itab__283[] = {
  /*  0 */         326,         327,         328,         329,
};

static const uint16_t ud_itab__284[] = {
  /*  0 */         330,           0,           0,         331,
};

static const uint16_t ud_itab__285[] = {
  /*  0 */         332,           0,           0,         333,
};

static const uint16_t ud_itab__286[] = {
  /*  0 */         334,           0,           0,         335,
};

static const uint16_t ud_itab__287[] = {
  /*  0 */         336,           0,           0,         337,
};

static const uint16_t ud_itab__288[] = {
  /*  0 */         338,           0,           0,         339,
};

static const uint16_t ud_itab__289[] = {
  /*  0 */         340,           0,           0,         341,
};

static const uint16_t ud_itab__290[] = {
  /*  0 */         342,           0,           0,         343,
};

static const uint16_t ud_itab__291[] = {
  /*  0 */         344,           0,           0,         345,
};

static const uint16_t ud_itab__292[] = {
  /*  0 */         346,           0,           0,         347,
};

static const uint16_t ud_itab__293[] = {
  /*  0 */         348,           0,           0,         349,
};

static const uint16_t ud_itab__294[] = {
  /*  0 */         350,           0,           0,         351,
};

static const uint16_t ud_itab__295[] = {
  /*  0 */         352,           0,           0,         353,
};

static const uint16_t ud_itab__296[] = {
  /*  0 */           0,           0,           0,         354,
};

static const uint16_t ud_itab__297[] = {
  /*  0 */           0,           0,           0,         355,
};

static const uint16_t ud_itab__298[] = {
  /*  0 */         356,           0,           0,         357,
};

static const uint16_t ud_itab__299[] = {
  /*  0 */         358,           0,         359,         360,
};

static const uint16_t ud_itab__300[] = {
  /*  0 */         361,         362,         363,         364,
};

static const uint16_t ud_itab__302[] = {
  /*  0 */         365,           0,           0,         366,
};

static const uint16_t ud_itab__303[] = {
  /*  0 */         367,           0,           0,         368,
};

static const uint16_t ud_itab__304[] = {
  /*  0 */         369,           0,           0,         370,
};

static const uint16_t ud_itab__301[] = {
  /*  0 */           0,           0,  GROUP(302),           0,
  /*  4 */  GROUP(303),           0,  GROUP(304),           0,
};

static const uint16_t ud_itab__306[] = {
  /*  0 */         371,           0,           0,         372,
};

static const uint16_t ud_itab__307[] = {
  /*  0 */         373,           0,           0,         374,
};

static const uint16_t ud_itab__308[] = {
  /*  0 */         375,           0,           0,         376,
};

static const uint16_t ud_itab__305[] = {
  /*  0 */           0,           0,  GROUP(306),           0,
  /*  4 */  GROUP(307),           0,  GROUP(308),           0,
};

static const uint16_t ud_itab__310[] = {
  /*  0 */         377,           0,           0,         378,
};

static const uint16_t ud_itab__311[] = {
  /*  0 */           0,           0,           0,         379,
};

static const uint16_t ud_itab__312[] = {
  /*  0 */         380,           0,           0,         381,
};

static const uint16_t ud_itab__313[] = {
  /*  0 */           0,           0,           0,         382,
};

static const uint16_t ud_itab__309[] = {
  /*  0 */           0,           0,  GROUP(310),  GROUP(311),
  /*  4 */           0,           0,  GROUP(312),  GROUP(313),
};

static const uint16_t ud_itab__314[] = {
  /*  0 */         383,           0,           0,         384,
};

static const uint16_t ud_itab__315[] = {
  /*  0 */         385,           0,           0,         386,
};

static const uint16_t ud_itab__316[] = {
  /*  0 */         387,           0,           0,         388,
};

static const uint16_t ud_itab__317[] = {
  /*  0 */         389,           0,           0,           0,
};

static const uint16_t ud_itab__319[] = {
  /*  0 */           0,         390,           0,
};

static const uint16_t ud_itab__318[] = {
  /*  0 */  GROUP(319),           0,           0,           0,
};

static const uint16_t ud_itab__321[] = {
  /*  0 */           0,         391,           0,
};

static const uint16_t ud_itab__320[] = {
  /*  0 */  GROUP(321),           0,           0,           0,
};

static const uint16_t ud_itab__322[] = {
  /*  0 */           0,         392,           0,         393,
};

static const uint16_t ud_itab__323[] = {
  /*  0 */           0,         394,           0,         395,
};

static const uint16_t ud_itab__324[] = {
  /*  0 */         396,           0,         397,         398,
};

static const uint16_t ud_itab__325[] = {
  /*  0 */         399,           0,         400,         401,
};

static const uint16_t ud_itab__326[] = {
  /*  0 */         402,           0,           0,           0,
};

static const uint16_t ud_itab__327[] = {
  /*  0 */         403,           0,           0,           0,
};

static const uint16_t ud_itab__328[] = {
  /*  0 */         404,           0,           0,           0,
};

static const uint16_t ud_itab__329[] = {
  /*  0 */         405,           0,           0,           0,
};

static const uint16_t ud_itab__330[] = {
  /*  0 */         406,           0,           0,           0,
};

static const uint16_t ud_itab__331[] = {
  /*  0 */         407,           0,           0,           0,
};

static const uint16_t ud_itab__332[] = {
  /*  0 */         408,           0,           0,           0,
};

static const uint16_t ud_itab__333[] = {
  /*  0 */         409,           0,           0,           0,
};

static const uint16_t ud_itab__334[] = {
  /*  0 */         410,           0,           0,           0,
};

static const uint16_t ud_itab__335[] = {
  /*  0 */         411,           0,           0,           0,
};

static const uint16_t ud_itab__336[] = {
  /*  0 */         412,           0,           0,           0,
};

static const uint16_t ud_itab__337[] = {
  /*  0 */         413,           0,           0,           0,
};

static const uint16_t ud_itab__338[] = {
  /*  0 */         414,           0,           0,           0,
};

static const uint16_t ud_itab__339[] = {
  /*  0 */         415,           0,           0,           0,
};

static const uint16_t ud_itab__340[] = {
  /*  0 */         416,           0,           0,           0,
};

static const uint16_t ud_itab__341[] = {
  /*  0 */         417,           0,           0,           0,
};

static const uint16_t ud_itab__342[] = {
  /*  0 */         418,           0,           0,           0,
};

static const uint16_t ud_itab__343[] = {
  /*  0 */         419,           0,           0,           0,
};

static const uint16_t ud_itab__344[] = {
  /*  0 */         420,           0,           0,           0,
};

static const uint16_t ud_itab__345[] = {
  /*  0 */         421,           0,           0,           0,
};

static const uint16_t ud_itab__346[] = {
  /*  0 */         422,           0,           0,           0,
};

static const uint16_t ud_itab__347[] = {
  /*  0 */         423,           0,           0,           0,
};

static const uint16_t ud_itab__348[] = {
  /*  0 */         424,           0,           0,           0,
};

static const uint16_t ud_itab__349[] = {
  /*  0 */         425,           0,           0,           0,
};

static const uint16_t ud_itab__350[] = {
  /*  0 */         426,           0,           0,           0,
};

static const uint16_t ud_itab__351[] = {
  /*  0 */         427,           0,           0,           0,
};

static const uint16_t ud_itab__352[] = {
  /*  0 */         428,           0,           0,           0,
};

static const uint16_t ud_itab__353[] = {
  /*  0 */         429,           0,           0,           0,
};

static const uint16_t ud_itab__354[] = {
  /*  0 */         430,           0,           0,           0,
};

static const uint16_t ud_itab__355[] = {
  /*  0 */         431,           0,           0,           0,
};

static const uint16_t ud_itab__356[] = {
  /*  0 */         432,           0,           0,           0,
};

static const uint16_t ud_itab__357[] = {
  /*  0 */         433,           0,           0,           0,
};

static const uint16_t ud_itab__358[] = {
  /*  0 */         434,           0,           0,           0,
};

static const uint16_t ud_itab__359[] = {
  /*  0 */         435,           0,           0,           0,
};

static const uint16_t ud_itab__360[] = {
  /*  0 */         436,           0,           0,           0,
};

static const uint16_t ud_itab__361[] = {
  /*  0 */         437,           0,           0,           0,
};

static const uint16_t ud_itab__362[] = {
  /*  0 */         438,           0,           0,           0,
};

static const uint16_t ud_itab__363[] = {
  /*  0 */         439,           0,           0,           0,
};

static const uint16_t ud_itab__368[] = {
  /*  0 */           0,         440,
};

static const uint16_t ud_itab__367[] = {
  /*  0 */  GROUP(368),           0,           0,           0,
};

static const uint16_t ud_itab__366[] = {
  /*  0 */  GROUP(367),           0,           0,           0,
  /*  4 */           0,           0,           0,           0,
};

static const uint16_t ud_itab__371[] = {
  /*  0 */           0,         441,
};

static const uint16_t ud_itab__370[] = {
  /*  0 */  GROUP(371),           0,           0,           0,
};

static const uint16_t ud_itab__369[] = {
  /*  0 */  GROUP(370),           0,           0,           0,
  /*  4 */           0,           0,           0,           0,
};

static const uint16_t ud_itab__374[] = {
  /*  0 */           0,         442,
};

static const uint16_t ud_itab__373[] = {
  /*  0 */  GROUP(374),           0,           0,           0,
};

static const uint16_t ud_itab__372[] = {
  /*  0 */  GROUP(373),           0,           0,           0,
  /*  4 */           0,           0,           0,           0,
};

static const uint16_t ud_itab__365[] = {
  /*  0 */  GROUP(366),  GROUP(369),  GROUP(372),           0,
  /*  4 */           0,           0,           0,           0,
};

static const uint16_t ud_itab__364[] = {
  /*  0 */           0,  GROUP(365),
};

static const uint16_t ud_itab__379[] = {
  /*  0 */           0,         443,
};

static const uint16_t ud_itab__378[] = {
  /*  0 */  GROUP(379),           0,           0,           0,
};

static const uint16_t ud_itab__377[] = {
  /*  0 */  GROUP(378),           0,           0,           0,
  /*  4 */           0,           0,           0,           0,
};

static const uint16_t ud_itab__382[] = {
  /*  0 */           0,         444,
};

static const uint16_t ud_itab__381[] = {
  /*  0 */  GROUP(382),           0,           0,           0,
};

static const uint16_t ud_itab__380[] = {
  /*  0 */  GROUP(381),           0,           0,           0,
  /*  4 */           0,           0,           0,           0,
};

static const uint16_t ud_itab__385[] = {
  /*  0 */           0,         445,
};

static const uint16_t ud_itab__384[] = {
  /*  0 */  GROUP(385),           0,           0,           0,
};

static const uint16_t ud_itab__383[] = {
  /*  0 */  GROUP(384),           0,           0,           0,
  /*  4 */           0,           0,           0,           0,
};

static const uint16_t ud_itab__388[] = {
  /*  0 */           0,         446,
};

static const uint16_t ud_itab__387[] = {
  /*  0 */  GROUP(388),           0,           0,           0,
};

static const uint16_t ud_itab__386[] = {
  /*  0 */  GROUP(387),           0,           0,           0,
  /*  4 */           0,           0,           0,           0,
};

static const uint16_t ud_itab__391[] = {
  /*  0 */           0,         447,
};

static const uint16_t ud_itab__390[] = {
  /*  0 */  GROUP(391),           0,           0,           0,
};

static const uint16_t ud_itab__389[] = {
  /*  0 */  GROUP(390),           0,           0,           0,
  /*  4 */           0,           0,           0,           0,
};

static const uint16_t ud_itab__394[] = {
  /*  0 */           0,         448,
};

static const uint16_t ud_itab__393[] = {
  /*  0 */  GROUP(394),           0,           0,           0,
};

static const uint16_t ud_itab__392[] = {
  /*  0 */  GROUP(393),           0,           0,           0,
  /*  4 */           0,           0,           0,           0,
};

static const uint16_t ud_itab__376[] = {
  /*  0 */  GROUP(377),  GROUP(380),  GROUP(383),  GROUP(386),
  /*  4 */  GROUP(389),  GROUP(392),           0,           0,
};

static const uint16_t ud_itab__375[] = {
  /*  0 */           0,  GROUP(376),
};

static const uint16_t ud_itab__395[] = {
  /*  0 */         449,           0,           0,           0,
};

static const uint16_t ud_itab__396[] = {
  /*  0 */         450,           0,           0,           0,
};

static const uint16_t ud_itab__397[] = {
  /*  0 */         451,           0,           0,           0,
};

static const uint16_t ud_itab__398[] = {
  /*  0 */         452,           0,           0,           0,
};

static const uint16_t ud_itab__399[] = {
  /*  0 */         453,           0,           0,           0,
};

static const uint16_t ud_itab__400[] = {
  /*  0 */         454,           0,           0,           0,
};

static const uint16_t ud_itab__404[] = {
  /*  0 */         455,           0,
};

static const uint16_t ud_itab__403[] = {
  /*  0 */  GROUP(404),           0,           0,           0,
};

static const uint16_t ud_itab__406[] = {
  /*  0 */         456,           0,
};

static const uint16_t ud_itab__405[] = {
  /*  0 */  GROUP(406),           0,           0,           0,
};

static const uint16_t ud_itab__408[] = {
  /*  0 */         457,           0,
};

static const uint16_t ud_itab__407[] = {
  /*  0 */  GROUP(408),           0,           0,           0,
};

static const uint16_t ud_itab__410[] = {
  /*  0 */         458,           0,
};

static const uint16_t ud_itab__409[] = {
  /*  0 */  GROUP(410),           0,           0,           0,
};

static const uint16_t ud_itab__412[] = {
  /*  0 */         459,           0,
};

static const uint16_t ud_itab__411[] = {
  /*  0 */  GROUP(412),           0,           0,           0,
};

static const uint16_t ud_itab__414[] = {
  /*  0 */         460,           0,
};

static const uint16_t ud_itab__413[] = {
  /*  0 */  GROUP(414),           0,           0,           0,
};

static const uint16_t ud_itab__416[] = {
  /*  0 */         461,           0,
};

static const uint16_t ud_itab__415[] = {
  /*  0 */  GROUP(416),           0,           0,           0,
};

static const uint16_t ud_itab__402[] = {
  /*  0 */  GROUP(403),  GROUP(405),  GROUP(407),  GROUP(409),
  /*  4 */  GROUP(411),  GROUP(413),           0,  GROUP(415),
};

static const uint16_t ud_itab__420[] = {
  /*  0 */           0,         462,
};

static const uint16_t ud_itab__419[] = {
  /*  0 */  GROUP(420),           0,           0,           0,
};

static const uint16_t ud_itab__422[] = {
  /*  0 */           0,         463,
};

static const uint16_t ud_itab__421[] = {
  /*  0 */  GROUP(422),           0,           0,           0,
};

static const uint16_t ud_itab__424[] = {
  /*  0 */           0,         464,
};

static const uint16_t ud_itab__423[] = {
  /*  0 */  GROUP(424),           0,           0,           0,
};

static const uint16_t ud_itab__426[] = {
  /*  0 */           0,         465,
};

static const uint16_t ud_itab__425[] = {
  /*  0 */  GROUP(426),           0,           0,           0,
};

static const uint16_t ud_itab__428[] = {
  /*  0 */           0,         466,
};

static const uint16_t ud_itab__427[] = {
  /*  0 */  GROUP(428),           0,           0,           0,
};

static const uint16_t ud_itab__430[] = {
  /*  0 */           0,         467,
};

static const uint16_t ud_itab__429[] = {
  /*  0 */  GROUP(430),           0,           0,           0,
};

static const uint16_t ud_itab__432[] = {
  /*  0 */           0,         468,
};

static const uint16_t ud_itab__431[] = {
  /*  0 */  GROUP(432),           0,           0,           0,
};

static const uint16_t ud_itab__434[] = {
  /*  0 */           0,         469,
};

static const uint16_t ud_itab__433[] = {
  /*  0 */  GROUP(434),           0,           0,           0,
};

static const uint16_t ud_itab__418[] = {
  /*  0 */  GROUP(419),  GROUP(421),  GROUP(423),  GROUP(425),
  /*  4 */  GROUP(427),  GROUP(429),  GROUP(431),  GROUP(433),
};

static const uint16_t ud_itab__437[] = {
  /*  0 */           0,         470,
};

static const uint16_t ud_itab__436[] = {
  /*  0 */  GROUP(437),           0,           0,           0,
};

static const uint16_t ud_itab__439[] = {
  /*  0 */           0,         471,
};

static const uint16_t ud_itab__438[] = {
  /*  0 */  GROUP(439),           0,           0,           0,
};

static const uint16_t ud_itab__441[] = {
  /*  0 */           0,         472,
};

static const uint16_t ud_itab__440[] = {
  /*  0 */  GROUP(441),           0,           0,           0,
};

static const uint16_t ud_itab__443[] = {
  /*  0 */           0,         473,
};

static const uint16_t ud_itab__442[] = {
  /*  0 */  GROUP(443),           0,           0,           0,
};

static const uint16_t ud_itab__445[] = {
  /*  0 */           0,         474,
};

static const uint16_t ud_itab__444[] = {
  /*  0 */  GROUP(445),           0,           0,           0,
};

static const uint16_t ud_itab__447[] = {
  /*  0 */           0,         475,
};

static const uint16_t ud_itab__446[] = {
  /*  0 */  GROUP(447),           0,           0,           0,
};

static const uint16_t ud_itab__449[] = {
  /*  0 */           0,         476,
};

static const uint16_t ud_itab__448[] = {
  /*  0 */  GROUP(449),           0,           0,           0,
};

static const uint16_t ud_itab__451[] = {
  /*  0 */           0,         477,
};

static const uint16_t ud_itab__450[] = {
  /*  0 */  GROUP(451),           0,           0,           0,
};

static const uint16_t ud_itab__435[] = {
  /*  0 */  GROUP(436),  GROUP(438),  GROUP(440),  GROUP(442),
  /*  4 */  GROUP(444),  GROUP(446),  GROUP(448),  GROUP(450),
};

static const uint16_t ud_itab__454[] = {
  /*  0 */           0,         478,
};

static const uint16_t ud_itab__453[] = {
  /*  0 */  GROUP(454),           0,           0,           0,
};

static const uint16_t ud_itab__456[] = {
  /*  0 */           0,         479,
};

static const uint16_t ud_itab__455[] = {
  /*  0 */  GROUP(456),           0,           0,           0,
};

static const uint16_t ud_itab__458[] = {
  /*  0 */           0,         480,
};

static const uint16_t ud_itab__457[] = {
  /*  0 */  GROUP(458),           0,           0,           0,
};

static const uint16_t ud_itab__460[] = {
  /*  0 */           0,         481,
};

static const uint16_t ud_itab__459[] = {
  /*  0 */  GROUP(460),           0,           0,           0,
};

static const uint16_t ud_itab__462[] = {
  /*  0 */           0,         482,
};

static const uint16_t ud_itab__461[] = {
  /*  0 */  GROUP(462),           0,           0,           0,
};

static const uint16_t ud_itab__464[] = {
  /*  0 */           0,         483,
};

static const uint16_t ud_itab__463[] = {
  /*  0 */  GROUP(464),           0,           0,           0,
};

static const uint16_t ud_itab__466[] = {
  /*  0 */           0,         484,
};

static const uint16_t ud_itab__465[] = {
  /*  0 */  GROUP(466),           0,           0,           0,
};

static const uint16_t ud_itab__468[] = {
  /*  0 */           0,         485,
};

static const uint16_t ud_itab__467[] = {
  /*  0 */  GROUP(468),           0,           0,           0,
};

static const uint16_t ud_itab__452[] = {
  /*  0 */  GROUP(453),  GROUP(455),  GROUP(457),  GROUP(459),
  /*  4 */  GROUP(461),  GROUP(463),  GROUP(465),  GROUP(467),
};

static const uint16_t ud_itab__417[] = {
  /*  0 */           0,           0,           0,           0,
  /*  4 */           0,  GROUP(418),  GROUP(435),  GROUP(452),
};

static const uint16_t ud_itab__401[] = {
  /*  0 */  GROUP(402),  GROUP(417),
};

static const uint16_t ud_itab__469[] = {
  /*  0 */         486,           0,           0,           0,
};

static const uint16_t ud_itab__470[] = {
  /*  0 */         487,           0,           0,           0,
};

static const uint16_t ud_itab__471[] = {
  /*  0 */         488,           0,           0,           0,
};

static const uint16_t ud_itab__472[] = {
  /*  0 */         489,           0,           0,           0,
};

static const uint16_t ud_itab__473[] = {
  /*  0 */         490,           0,           0,           0,
};

static const uint16_t ud_itab__474[] = {
  /*  0 */         491,           0,           0,           0,
};

static const uint16_t ud_itab__475[] = {
  /*  0 */         492,           0,           0,           0,
};

static const uint16_t ud_itab__476[] = {
  /*  0 */         493,           0,           0,           0,
};

static const uint16_t ud_itab__477[] = {
  /*  0 */         494,           0,           0,           0,
};

static const uint16_t ud_itab__478[] = {
  /*  0 */           0,           0,         495,           0,
};

static const uint16_t ud_itab__480[] = {
  /*  0 */         496,           0,           0,           0,
};

static const uint16_t ud_itab__481[] = {
  /*  0 */         497,           0,           0,           0,
};

static const uint16_t ud_itab__482[] = {
  /*  0 */         498,           0,           0,           0,
};

static const uint16_t ud_itab__483[] = {
  /*  0 */         499,           0,           0,           0,
};

static const uint16_t ud_itab__479[] = {
  /*  0 */           0,           0,           0,           0,
  /*  4 */  GROUP(480),  GROUP(481),  GROUP(482),  GROUP(483),
};

static const uint16_t ud_itab__484[] = {
  /*  0 */         500,           0,           0,           0,
};

static const uint16_t ud_itab__485[] = {
  /*  0 */         501,           0,           0,           0,
};

static const uint16_t ud_itab__486[] = {
  /*  0 */         502,           0,           0,           0,
};

static const uint16_t ud_itab__487[] = {
  /*  0 */         503,           0,           0,           0,
};

static const uint16_t ud_itab__488[] = {
  /*  0 */         504,           0,           0,           0,
};

static const uint16_t ud_itab__489[] = {
  /*  0 */         505,           0,           0,           0,
};

static const uint16_t ud_itab__490[] = {
  /*  0 */         506,           0,           0,           0,
};

static const uint16_t ud_itab__491[] = {
  /*  0 */         507,         508,         509,         510,
};

static const uint16_t ud_itab__492[] = {
  /*  0 */         511,           0,           0,           0,
};

static const uint16_t ud_itab__493[] = {
  /*  0 */         512,           0,           0,         513,
};

static const uint16_t ud_itab__494[] = {
  /*  0 */         514,           0,           0,         515,
};

static const uint16_t ud_itab__495[] = {
  /*  0 */         516,           0,           0,         517,
};

static const uint16_t ud_itab__498[] = {
  /*  0 */         518,         519,         520,
};

static const uint16_t ud_itab__497[] = {
  /*  0 */  GROUP(498),           0,           0,           0,
};

static const uint16_t ud_itab__500[] = {
  /*  0 */           0,         521,           0,
};

static const uint16_t ud_itab__501[] = {
  /*  0 */           0,         522,           0,
};

static const uint16_t ud_itab__502[] = {
  /*  0 */           0,         523,           0,
};

static const uint16_t ud_itab__499[] = {
  /*  0 */  GROUP(500),           0,  GROUP(501),  GROUP(502),
};

static const uint16_t ud_itab__504[] = {
  /*  0 */           0,         524,           0,
};

static const uint16_t ud_itab__503[] = {
  /*  0 */  GROUP(504),           0,           0,           0,
};

static const uint16_t ud_itab__496[] = {
  /*  0 */           0,  GROUP(497),           0,           0,
  /*  4 */           0,           0,  GROUP(499),  GROUP(503),
};

static const uint16_t ud_itab__505[] = {
  /*  0 */         525,           0,           0,           0,
};

static const uint16_t ud_itab__506[] = {
  /*  0 */         526,           0,           0,           0,
};

static const uint16_t ud_itab__507[] = {
  /*  0 */         527,           0,           0,           0,
};

static const uint16_t ud_itab__508[] = {
  /*  0 */         528,           0,           0,           0,
};

static const uint16_t ud_itab__509[] = {
  /*  0 */         529,           0,           0,           0,
};

static const uint16_t ud_itab__510[] = {
  /*  0 */         530,           0,           0,           0,
};

static const uint16_t ud_itab__511[] = {
  /*  0 */         531,           0,           0,           0,
};

static const uint16_t ud_itab__512[] = {
  /*  0 */         532,           0,           0,           0,
};

static const uint16_t ud_itab__513[] = {
  /*  0 */           0,         533,           0,         534,
};

static const uint16_t ud_itab__514[] = {
  /*  0 */         535,           0,           0,         536,
};

static const uint16_t ud_itab__515[] = {
  /*  0 */         537,           0,           0,         538,
};

static const uint16_t ud_itab__516[] = {
  /*  0 */         539,           0,           0,         540,
};

static const uint16_t ud_itab__517[] = {
  /*  0 */         541,           0,           0,         542,
};

static const uint16_t ud_itab__518[] = {
  /*  0 */         543,           0,           0,         544,
};

static const uint16_t ud_itab__519[] = {
  /*  0 */           0,         545,         546,         547,
};

static const uint16_t ud_itab__520[] = {
  /*  0 */         548,           0,           0,         549,
};

static const uint16_t ud_itab__521[] = {
  /*  0 */         550,           0,           0,         551,
};

static const uint16_t ud_itab__522[] = {
  /*  0 */         552,           0,           0,         553,
};

static const uint16_t ud_itab__523[] = {
  /*  0 */         554,           0,           0,         555,
};

static const uint16_t ud_itab__524[] = {
  /*  0 */         556,           0,           0,         557,
};

static const uint16_t ud_itab__525[] = {
  /*  0 */         558,           0,           0,         559,
};

static const uint16_t ud_itab__526[] = {
  /*  0 */         560,           0,           0,         561,
};

static const uint16_t ud_itab__527[] = {
  /*  0 */         562,           0,           0,         563,
};

static const uint16_t ud_itab__528[] = {
  /*  0 */         564,           0,           0,         565,
};

static const uint16_t ud_itab__529[] = {
  /*  0 */         566,           0,           0,         567,
};

static const uint16_t ud_itab__530[] = {
  /*  0 */         568,           0,           0,         569,
};

static const uint16_t ud_itab__531[] = {
  /*  0 */         570,           0,           0,         571,
};

static const uint16_t ud_itab__532[] = {
  /*  0 */         572,           0,           0,         573,
};

static const uint16_t ud_itab__533[] = {
  /*  0 */         574,           0,           0,         575,
};

static const uint16_t ud_itab__534[] = {
  /*  0 */         576,           0,           0,         577,
};

static const uint16_t ud_itab__535[] = {
  /*  0 */           0,         578,         579,         580,
};

static const uint16_t ud_itab__536[] = {
  /*  0 */         581,           0,           0,         582,
};

static const uint16_t ud_itab__537[] = {
  /*  0 */         583,           0,           0,         584,
};

static const uint16_t ud_itab__538[] = {
  /*  0 */         585,           0,           0,         586,
};

static const uint16_t ud_itab__539[] = {
  /*  0 */         587,           0,           0,         588,
};

static const uint16_t ud_itab__540[] = {
  /*  0 */         589,           0,           0,         590,
};

static const uint16_t ud_itab__541[] = {
  /*  0 */         591,           0,           0,         592,
};

static const uint16_t ud_itab__542[] = {
  /*  0 */         593,           0,           0,         594,
};

static const uint16_t ud_itab__543[] = {
  /*  0 */         595,           0,           0,         596,
};

static const uint16_t ud_itab__544[] = {
  /*  0 */         597,           0,           0,         598,
};

static const uint16_t ud_itab__545[] = {
  /*  0 */           0,         599,           0,           0,
};

static const uint16_t ud_itab__546[] = {
  /*  0 */         600,           0,           0,         601,
};

static const uint16_t ud_itab__547[] = {
  /*  0 */         602,           0,           0,         603,
};

static const uint16_t ud_itab__548[] = {
  /*  0 */         604,           0,           0,         605,
};

static const uint16_t ud_itab__549[] = {
  /*  0 */         606,           0,           0,         607,
};

static const uint16_t ud_itab__550[] = {
  /*  0 */         608,           0,           0,         609,
};

static const uint16_t ud_itab__551[] = {
  /*  0 */         610,           0,           0,         611,
};

static const uint16_t ud_itab__554[] = {
  /*  0 */           0,         612,
};

static const uint16_t ud_itab__555[] = {
  /*  0 */           0,         613,
};

static const uint16_t ud_itab__553[] = {
  /*  0 */  GROUP(554),           0,           0,  GROUP(555),
};

static const uint16_t ud_itab__552[] = {
  /*  0 */           0,  GROUP(553),
};

static const uint16_t ud_itab__556[] = {
  /*  0 */         614,           0,           0,         615,
};

static const uint16_t ud_itab__557[] = {
  /*  0 */         616,           0,           0,         617,
};

static const uint16_t ud_itab__558[] = {
  /*  0 */         618,           0,           0,         619,
};

static const uint16_t ud_itab__559[] = {
  /*  0 */         620,           0,           0,         621,
};

static const uint16_t ud_itab__560[] = {
  /*  0 */         622,           0,           0,         623,
};

static const uint16_t ud_itab__561[] = {
  /*  0 */         624,           0,           0,         625,
};

static const uint16_t ud_itab__562[] = {
  /*  0 */         626,           0,           0,         627,
};

static const uint16_t ud_itab__4[] = {
  /*  0 */    GROUP(5),   GROUP(12),   GROUP(87),   GROUP(88),
  /*  4 */           0,   GROUP(89),   GROUP(90),   GROUP(91),
  /*  8 */   GROUP(92),   GROUP(93),           0,   GROUP(94),
  /*  c */           0,   GROUP(95),  GROUP(104),  GROUP(105),
  /* 10 */  GROUP(106),  GROUP(107),  GROUP(108),  GROUP(118),
  /* 14 */  GROUP(119),  GROUP(120),  GROUP(121),  GROUP(129),
  /* 18 */  GROUP(130),  GROUP(135),  GROUP(136),  GROUP(137),
  /* 1c */  GROUP(138),  GROUP(139),  GROUP(140),  GROUP(141),
  /* 20 */  GROUP(142),  GROUP(143),  GROUP(144),  GROUP(145),
  /* 24 */           0,           0,           0,           0,
  /* 28 */  GROUP(146),  GROUP(147),  GROUP(148),  GROUP(149),
  /* 2c */  GROUP(150),  GROUP(151),  GROUP(152),  GROUP(153),
  /* 30 */  GROUP(154),  GROUP(155),  GROUP(156),  GROUP(157),
  /* 34 */  GROUP(158),  GROUP(161),           0,  GROUP(164),
  /* 38 */  GROUP(165),           0,  GROUP(225),           0,
  /* 3c */           0,           0,           0,           0,
  /* 40 */  GROUP(252),  GROUP(253),  GROUP(254),  GROUP(255),
  /* 44 */  GROUP(256),  GROUP(257),  GROUP(258),  GROUP(259),
  /* 48 */  GROUP(260),  GROUP(261),  GROUP(262),  GROUP(263),
  /* 4c */  GROUP(264),  GROUP(265),  GROUP(266),  GROUP(267),
  /* 50 */  GROUP(268),  GROUP(269),  GROUP(270),  GROUP(271),
  /* 54 */  GROUP(272),  GROUP(273),  GROUP(274),  GROUP(275),
  /* 58 */  GROUP(276),  GROUP(277),  GROUP(278),  GROUP(279),
  /* 5c */  GROUP(280),  GROUP(281),  GROUP(282),  GROUP(283),
  /* 60 */  GROUP(284),  GROUP(285),  GROUP(286),  GROUP(287),
  /* 64 */  GROUP(288),  GROUP(289),  GROUP(290),  GROUP(291),
  /* 68 */  GROUP(292),  GROUP(293),  GROUP(294),  GROUP(295),
  /* 6c */  GROUP(296),  GROUP(297),  GROUP(298),  GROUP(299),
  /* 70 */  GROUP(300),  GROUP(301),  GROUP(305),  GROUP(309),
  /* 74 */  GROUP(314),  GROUP(315),  GROUP(316),  GROUP(317),
  /* 78 */  GROUP(318),  GROUP(320),           0,           0,
  /* 7c */  GROUP(322),  GROUP(323),  GROUP(324),  GROUP(325),
  /* 80 */  GROUP(326),  GROUP(327),  GROUP(328),  GROUP(329),
  /* 84 */  GROUP(330),  GROUP(331),  GROUP(332),  GROUP(333),
  /* 88 */  GROUP(334),  GROUP(335),  GROUP(336),  GROUP(337),
  /* 8c */  GROUP(338),  GROUP(339),  GROUP(340),  GROUP(341),
  /* 90 */  GROUP(342),  GROUP(343),  GROUP(344),  GROUP(345),
  /* 94 */  GROUP(346),  GROUP(347),  GROUP(348),  GROUP(349),
  /* 98 */  GROUP(350),  GROUP(351),  GROUP(352),  GROUP(353),
  /* 9c */  GROUP(354),  GROUP(355),  GROUP(356),  GROUP(357),
  /* a0 */  GROUP(358),  GROUP(359),  GROUP(360),  GROUP(361),
  /* a4 */  GROUP(362),  GROUP(363),  GROUP(364),  GROUP(375),
  /* a8 */  GROUP(395),  GROUP(396),  GROUP(397),  GROUP(398),
  /* ac */  GROUP(399),  GROUP(400),  GROUP(401),  GROUP(469),
  /* b0 */  GROUP(470),  GROUP(471),  GROUP(472),  GROUP(473),
  /* b4 */  GROUP(474),  GROUP(475),  GROUP(476),  GROUP(477),
  /* b8 */  GROUP(478),           0,  GROUP(479),  GROUP(484),
  /* bc */  GROUP(485),  GROUP(486),  GROUP(487),  GROUP(488),
  /* c0 */  GROUP(489),  GROUP(490),  GROUP(491),  GROUP(492),
  /* c4 */  GROUP(493),  GROUP(494),  GROUP(495),  GROUP(496),
  /* c8 */  GROUP(505),  GROUP(506),  GROUP(507),  GROUP(508),
  /* cc */  GROUP(509),  GROUP(510),  GROUP(511),  GROUP(512),
  /* d0 */  GROUP(513),  GROUP(514),  GROUP(515),  GROUP(516),
  /* d4 */  GROUP(517),  GROUP(518),  GROUP(519),  GROUP(520),
  /* d8 */  GROUP(521),  GROUP(522),  GROUP(523),  GROUP(524),
  /* dc */  GROUP(525),  GROUP(526),  GROUP(527),  GROUP(528),
  /* e0 */  GROUP(529),  GROUP(530),  GROUP(531),  GROUP(532),
  /* e4 */  GROUP(533),  GROUP(534),  GROUP(535),  GROUP(536),
  /* e8 */  GROUP(537),  GROUP(538),  GROUP(539),  GROUP(540),
  /* ec */  GROUP(541),  GROUP(542),  GROUP(543),  GROUP(544),
  /* f0 */  GROUP(545),  GROUP(546),  GROUP(547),  GROUP(548),
  /* f4 */  GROUP(549),  GROUP(550),  GROUP(551),  GROUP(552),
  /* f8 */  GROUP(556),  GROUP(557),  GROUP(558),  GROUP(559),
  /* fc */  GROUP(560),  GROUP(561),  GROUP(562),           0,
};

static const uint16_t ud_itab__563[] = {
  /*  0 */         634,           0,
};

static const uint16_t ud_itab__564[] = {
  /*  0 */         635,           0,
};

static const uint16_t ud_itab__565[] = {
  /*  0 */         642,           0,
};

static const uint16_t ud_itab__566[] = {
  /*  0 */         643,           0,
};

static const uint16_t ud_itab__567[] = {
  /*  0 */         650,           0,
};

static const uint16_t ud_itab__568[] = {
  /*  0 */         657,           0,
};

static const uint16_t ud_itab__569[] = {
  /*  0 */         664,           0,
};

static const uint16_t ud_itab__570[] = {
  /*  0 */         671,           0,
};

static const uint16_t ud_itab__572[] = {
  /*  0 */         704,           0,
};

static const uint16_t ud_itab__573[] = {
  /*  0 */         705,           0,
};

static const uint16_t ud_itab__571[] = {
  /*  0 */  GROUP(572),  GROUP(573),           0,
};

static const uint16_t ud_itab__575[] = {
  /*  0 */         706,           0,
};

static const uint16_t ud_itab__576[] = {
  /*  0 */         707,           0,
};

static const uint16_t ud_itab__574[] = {
  /*  0 */  GROUP(575),  GROUP(576),           0,
};

static const uint16_t ud_itab__577[] = {
  /*  0 */         708,           0,
};

static const uint16_t ud_itab__578[] = {
  /*  0 */         709,         710,
};

static const uint16_t ud_itab__579[] = {
  /*  0 */         716,         717,           0,
};

static const uint16_t ud_itab__580[] = {
  /*  0 */         719,         720,           0,
};

static const uint16_t ud_itab__581[] = {
  /*  0 */         737,         738,         739,         740,
  /*  4 */         741,         742,         743,         744,
};

static const uint16_t ud_itab__582[] = {
  /*  0 */         745,         746,         747,         748,
  /*  4 */         749,         750,         751,         752,
};

static const uint16_t ud_itab__584[] = {
  /*  0 */         753,           0,
};

static const uint16_t ud_itab__585[] = {
  /*  0 */         754,           0,
};

static const uint16_t ud_itab__586[] = {
  /*  0 */         755,           0,
};

static const uint16_t ud_itab__587[] = {
  /*  0 */         756,           0,
};

static const uint16_t ud_itab__588[] = {
  /*  0 */         757,           0,
};

static const uint16_t ud_itab__589[] = {
  /*  0 */         758,           0,
};

static const uint16_t ud_itab__590[] = {
  /*  0 */         759,           0,
};

static const uint16_t ud_itab__591[] = {
  /*  0 */         760,           0,
};

static const uint16_t ud_itab__583[] = {
  /*  0 */  GROUP(584),  GROUP(585),  GROUP(586),  GROUP(587),
  /*  4 */  GROUP(588),  GROUP(589),  GROUP(590),  GROUP(591),
};

static const uint16_t ud_itab__592[] = {
  /*  0 */         761,         762,         763,         764,
  /*  4 */         765,         766,         767,         768,
};

static const uint16_t ud_itab__593[] = {
  /*  0 */         780,           0,           0,           0,
  /*  4 */           0,           0,           0,           0,
};

static const uint16_t ud_itab__594[] = {
  /*  0 */         789,         790,         791,
};

static const uint16_t ud_itab__595[] = {
  /*  0 */         792,         793,         794,
};

static const uint16_t ud_itab__596[] = {
  /*  0 */         795,           0,
};

static const uint16_t ud_itab__598[] = {
  /*  0 */         797,         798,
};

static const uint16_t ud_itab__599[] = {
  /*  0 */         799,         800,
};

static const uint16_t ud_itab__600[] = {
  /*  0 */           0,         801,
};

static const uint16_t ud_itab__597[] = {
  /*  0 */  GROUP(598),  GROUP(599),  GROUP(600),
};

static const uint16_t ud_itab__602[] = {
  /*  0 */         802,           0,
};

static const uint16_t ud_itab__603[] = {
  /*  0 */         803,         804,
};

static const uint16_t ud_itab__604[] = {
  /*  0 */           0,         805,
};

static const uint16_t ud_itab__601[] = {
  /*  0 */  GROUP(602),  GROUP(603),  GROUP(604),
};

static const uint16_t ud_itab__605[] = {
  /*  0 */         813,         814,         815,
};

static const uint16_t ud_itab__606[] = {
  /*  0 */         817,         818,         819,
};

static const uint16_t ud_itab__607[] = {
  /*  0 */         823,         824,         825,
};

static const uint16_t ud_itab__608[] = {
  /*  0 */         827,         828,         829,
};

static const uint16_t ud_itab__609[] = {
  /*  0 */         831,         832,         833,
};

static const uint16_t ud_itab__610[] = {
  /*  0 */         850,         851,         852,         853,
  /*  4 */         854,         855,         856,         857,
};

static const uint16_t ud_itab__611[] = {
  /*  0 */         858,         859,         860,         861,
  /*  4 */         862,         863,         864,         865,
};

static const uint16_t ud_itab__612[] = {
  /*  0 */         868,           0,
};

static const uint16_t ud_itab__613[] = {
  /*  0 */         869,           0,
};

static const uint16_t ud_itab__614[] = {
  /*  0 */         870,           0,           0,           0,
  /*  4 */           0,           0,           0,           0,
};

static const uint16_t ud_itab__615[] = {
  /*  0 */         871,           0,           0,           0,
  /*  4 */           0,           0,           0,           0,
};

static const uint16_t ud_itab__616[] = {
  /*  0 */         878,           0,
};

static const uint16_t ud_itab__617[] = {
  /*  0 */         879,         880,         881,
};

static const uint16_t ud_itab__618[] = {
  /*  0 */         882,         883,         884,         885,
  /*  4 */         886,         887,         888,         889,
};

static const uint16_t ud_itab__619[] = {
  /*  0 */         890,         891,         892,         893,
  /*  4 */         894,         895,         896,         897,
};

static const uint16_t ud_itab__620[] = {
  /*  0 */         898,         899,         900,         901,
  /*  4 */         902,         903,         904,         905,
};

static const uint16_t ud_itab__621[] = {
  /*  0 */         906,         907,         908,         909,
  /*  4 */         910,         911,         912,         913,
};

static const uint16_t ud_itab__622[] = {
  /*  0 */         914,           0,
};

static const uint16_t ud_itab__623[] = {
  /*  0 */         915,           0,
};

static const uint16_t ud_itab__624[] = {
  /*  0 */         916,           0,
};

static const uint16_t ud_itab__627[] = {
  /*  0 */         918,           0,
};

static const uint16_t ud_itab__628[] = {
  /*  0 */         919,           0,
};

static const uint16_t ud_itab__629[] = {
  /*  0 */         920,           0,
};

static const uint16_t ud_itab__630[] = {
  /*  0 */         921,           0,
};

static const uint16_t ud_itab__631[] = {
  /*  0 */         922,           0,
};

static const uint16_t ud_itab__632[] = {
  /*  0 */         923,           0,
};

static const uint16_t ud_itab__633[] = {
  /*  0 */         924,           0,
};

static const uint16_t ud_itab__634[] = {
  /*  0 */         925,           0,
};

static const uint16_t ud_itab__626[] = {
  /*  0 */  GROUP(627),  GROUP(628),  GROUP(629),  GROUP(630),
  /*  4 */  GROUP(631),  GROUP(632),  GROUP(633),  GROUP(634),
};

static const uint16_t ud_itab__636[] = {
  /*  0 */           0,         926,
};

static const uint16_t ud_itab__637[] = {
  /*  0 */           0,         927,
};

static const uint16_t ud_itab__638[] = {
  /*  0 */           0,         928,
};

static const uint16_t ud_itab__639[] = {
  /*  0 */           0,         929,
};

static const uint16_t ud_itab__640[] = {
  /*  0 */           0,         930,
};

static const uint16_t ud_itab__641[] = {
  /*  0 */           0,         931,
};

static const uint16_t ud_itab__642[] = {
  /*  0 */           0,         932,
};

static const uint16_t ud_itab__643[] = {
  /*  0 */           0,         933,
};

static const uint16_t ud_itab__644[] = {
  /*  0 */           0,         934,
};

static const uint16_t ud_itab__645[] = {
  /*  0 */           0,         935,
};

static const uint16_t ud_itab__646[] = {
  /*  0 */           0,         936,
};

static const uint16_t ud_itab__647[] = {
  /*  0 */           0,         937,
};

static const uint16_t ud_itab__648[] = {
  /*  0 */           0,         938,
};

static const uint16_t ud_itab__649[] = {
  /*  0 */           0,         939,
};

static const uint16_t ud_itab__650[] = {
  /*  0 */           0,         940,
};

static const uint16_t ud_itab__651[] = {
  /*  0 */           0,         941,
};

static const uint16_t ud_itab__652[] = {
  /*  0 */           0,         942,
};

static const uint16_t ud_itab__653[] = {
  /*  0 */           0,         943,
};

static const uint16_t ud_itab__654[] = {
  /*  0 */           0,         944,
};

static const uint16_t ud_itab__655[] = {
  /*  0 */           0,         945,
};

static const uint16_t ud_itab__656[] = {
  /*  0 */           0,         946,
};

static const uint16_t ud_itab__657[] = {
  /*  0 */           0,         947,
};

static const uint16_t ud_itab__658[] = {
  /*  0 */           0,         948,
};

static const uint16_t ud_itab__659[] = {
  /*  0 */           0,         949,
};

static const uint16_t ud_itab__660[] = {
  /*  0 */           0,         950,
};

static const uint16_t ud_itab__661[] = {
  /*  0 */           0,         951,
};

static const uint16_t ud_itab__662[] = {
  /*  0 */           0,         952,
};

static const uint16_t ud_itab__663[] = {
  /*  0 */           0,         953,
};

static const uint16_t ud_itab__664[] = {
  /*  0 */           0,         954,
};

static const uint16_t ud_itab__665[] = {
  /*  0 */           0,         955,
};

static const uint16_t ud_itab__666[] = {
  /*  0 */           0,         956,
};

static const uint16_t ud_itab__667[] = {
  /*  0 */           0,         957,
};

static const uint16_t ud_itab__668[] = {
  /*  0 */           0,         958,
};

static const uint16_t ud_itab__669[] = {
  /*  0 */           0,         959,
};

static const uint16_t ud_itab__670[] = {
  /*  0 */           0,         960,
};

static const uint16_t ud_itab__671[] = {
  /*  0 */           0,         961,
};

static const uint16_t ud_itab__672[] = {
  /*  0 */           0,         962,
};

static const uint16_t ud_itab__673[] = {
  /*  0 */           0,         963,
};

static const uint16_t ud_itab__674[] = {
  /*  0 */           0,         964,
};

static const uint16_t ud_itab__675[] = {
  /*  0 */           0,         965,
};

static const uint16_t ud_itab__676[] = {
  /*  0 */           0,         966,
};

static const uint16_t ud_itab__677[] = {
  /*  0 */           0,         967,
};

static const uint16_t ud_itab__678[] = {
  /*  0 */           0,         968,
};

static const uint16_t ud_itab__679[] = {
  /*  0 */           0,         969,
};

static const uint16_t ud_itab__680[] = {
  /*  0 */           0,         970,
};

static const uint16_t ud_itab__681[] = {
  /*  0 */           0,         971,
};

static const uint16_t ud_itab__682[] = {
  /*  0 */           0,         972,
};

static const uint16_t ud_itab__683[] = {
  /*  0 */           0,         973,
};

static const uint16_t ud_itab__684[] = {
  /*  0 */           0,         974,
};

static const uint16_t ud_itab__685[] = {
  /*  0 */           0,         975,
};

static const uint16_t ud_itab__686[] = {
  /*  0 */           0,         976,
};

static const uint16_t ud_itab__687[] = {
  /*  0 */           0,         977,
};

static const uint16_t ud_itab__688[] = {
  /*  0 */           0,         978,
};

static const uint16_t ud_itab__689[] = {
  /*  0 */           0,         979,
};

static const uint16_t ud_itab__690[] = {
  /*  0 */           0,         980,
};

static const uint16_t ud_itab__691[] = {
  /*  0 */           0,         981,
};

static const uint16_t ud_itab__692[] = {
  /*  0 */           0,         982,
};

static const uint16_t ud_itab__693[] = {
  /*  0 */           0,         983,
};

static const uint16_t ud_itab__694[] = {
  /*  0 */           0,         984,
};

static const uint16_t ud_itab__695[] = {
  /*  0 */           0,         985,
};

static const uint16_t ud_itab__696[] = {
  /*  0 */           0,         986,
};

static const uint16_t ud_itab__697[] = {
  /*  0 */           0,         987,
};

static const uint16_t ud_itab__698[] = {
  /*  0 */           0,         988,
};

static const uint16_t ud_itab__699[] = {
  /*  0 */           0,         989,
};

static const uint16_t ud_itab__635[] = {
  /*  0 */  GROUP(636),  GROUP(637),  GROUP(638),  GROUP(639),
  /*  4 */  GROUP(640),  GROUP(641),  GROUP(642),  GROUP(643),
  /*  8 */  GROUP(644),  GROUP(645),  GROUP(646),  GROUP(647),
  /*  c */  GROUP(648),  GROUP(649),  GROUP(650),  GROUP(651),
  /* 10 */  GROUP(652),  GROUP(653),  GROUP(654),  GROUP(655),
  /* 14 */  GROUP(656),  GROUP(657),  GROUP(658),  GROUP(659),
  /* 18 */  GROUP(660),  GROUP(661),  GROUP(662),  GROUP(663),
  /* 1c */  GROUP(664),  GROUP(665),  GROUP(666),  GROUP(667),
  /* 20 */  GROUP(668),  GROUP(669),  GROUP(670),  GROUP(671),
  /* 24 */  GROUP(672),  GROUP(673),  GROUP(674),  GROUP(675),
  /* 28 */  GROUP(676),  GROUP(677),  GROUP(678),  GROUP(679),
  /* 2c */  GROUP(680),  GROUP(681),  GROUP(682),  GROUP(683),
  /* 30 */  GROUP(684),  GROUP(685),  GROUP(686),  GROUP(687),
  /* 34 */  GROUP(688),  GROUP(689),  GROUP(690),  GROUP(691),
  /* 38 */  GROUP(692),  GROUP(693),  GROUP(694),  GROUP(695),
  /* 3c */  GROUP(696),  GROUP(697),  GROUP(698),  GROUP(699),
};

static const uint16_t ud_itab__625[] = {
  /*  0 */  GROUP(626),  GROUP(635),
};

static const uint16_t ud_itab__702[] = {
  /*  0 */         990,           0,
};

static const uint16_t ud_itab__703[] = {
  /*  0 */         991,           0,
};

static const uint16_t ud_itab__704[] = {
  /*  0 */         992,           0,
};

static const uint16_t ud_itab__705[] = {
  /*  0 */         993,           0,
};

static const uint16_t ud_itab__706[] = {
  /*  0 */         994,           0,
};

static const uint16_t ud_itab__707[] = {
  /*  0 */         995,           0,
};

static const uint16_t ud_itab__708[] = {
  /*  0 */         996,           0,
};

static const uint16_t ud_itab__701[] = {
  /*  0 */  GROUP(702),           0,  GROUP(703),  GROUP(704),
  /*  4 */  GROUP(705),  GROUP(706),  GROUP(707),  GROUP(708),
};

static const uint16_t ud_itab__710[] = {
  /*  0 */           0,         997,
};

static const uint16_t ud_itab__711[] = {
  /*  0 */           0,         998,
};

static const uint16_t ud_itab__712[] = {
  /*  0 */           0,         999,
};

static const uint16_t ud_itab__713[] = {
  /*  0 */           0,        1000,
};

static const uint16_t ud_itab__714[] = {
  /*  0 */           0,        1001,
};

static const uint16_t ud_itab__715[] = {
  /*  0 */           0,        1002,
};

static const uint16_t ud_itab__716[] = {
  /*  0 */           0,        1003,
};

static const uint16_t ud_itab__717[] = {
  /*  0 */           0,        1004,
};

static const uint16_t ud_itab__718[] = {
  /*  0 */           0,        1005,
};

static const uint16_t ud_itab__719[] = {
  /*  0 */           0,        1006,
};

static const uint16_t ud_itab__720[] = {
  /*  0 */           0,        1007,
};

static const uint16_t ud_itab__721[] = {
  /*  0 */           0,        1008,
};

static const uint16_t ud_itab__722[] = {
  /*  0 */           0,        1009,
};

static const uint16_t ud_itab__723[] = {
  /*  0 */           0,        1010,
};

static const uint16_t ud_itab__724[] = {
  /*  0 */           0,        1011,
};

static const uint16_t ud_itab__725[] = {
  /*  0 */           0,        1012,
};

static const uint16_t ud_itab__726[] = {
  /*  0 */           0,        1013,
};

static const uint16_t ud_itab__727[] = {
  /*  0 */           0,        1014,
};

static const uint16_t ud_itab__728[] = {
  /*  0 */           0,        1015,
};

static const uint16_t ud_itab__729[] = {
  /*  0 */           0,        1016,
};

static const uint16_t ud_itab__730[] = {
  /*  0 */           0,        1017,
};

static const uint16_t ud_itab__731[] = {
  /*  0 */           0,        1018,
};

static const uint16_t ud_itab__732[] = {
  /*  0 */           0,        1019,
};

static const uint16_t ud_itab__733[] = {
  /*  0 */           0,        1020,
};

static const uint16_t ud_itab__734[] = {
  /*  0 */           0,        1021,
};

static const uint16_t ud_itab__735[] = {
  /*  0 */           0,        1022,
};

static const uint16_t ud_itab__736[] = {
  /*  0 */           0,        1023,
};

static const uint16_t ud_itab__737[] = {
  /*  0 */           0,        1024,
};

static const uint16_t ud_itab__738[] = {
  /*  0 */           0,        1025,
};

static const uint16_t ud_itab__739[] = {
  /*  0 */           0,        1026,
};

static const uint16_t ud_itab__740[] = {
  /*  0 */           0,        1027,
};

static const uint16_t ud_itab__741[] = {
  /*  0 */           0,        1028,
};

static const uint16_t ud_itab__742[] = {
  /*  0 */           0,        1029,
};

static const uint16_t ud_itab__743[] = {
  /*  0 */           0,        1030,
};

static const uint16_t ud_itab__744[] = {
  /*  0 */           0,        1031,
};

static const uint16_t ud_itab__745[] = {
  /*  0 */           0,        1032,
};

static const uint16_t ud_itab__746[] = {
  /*  0 */           0,        1033,
};

static const uint16_t ud_itab__747[] = {
  /*  0 */           0,        1034,
};

static const uint16_t ud_itab__748[] = {
  /*  0 */           0,        1035,
};

static const uint16_t ud_itab__749[] = {
  /*  0 */           0,        1036,
};

static const uint16_t ud_itab__750[] = {
  /*  0 */           0,        1037,
};

static const uint16_t ud_itab__751[] = {
  /*  0 */           0,        1038,
};

static const uint16_t ud_itab__752[] = {
  /*  0 */           0,        1039,
};

static const uint16_t ud_itab__753[] = {
  /*  0 */           0,        1040,
};

static const uint16_t ud_itab__754[] = {
  /*  0 */           0,        1041,
};

static const uint16_t ud_itab__755[] = {
  /*  0 */           0,        1042,
};

static const uint16_t ud_itab__756[] = {
  /*  0 */           0,        1043,
};

static const uint16_t ud_itab__757[] = {
  /*  0 */           0,        1044,
};

static const uint16_t ud_itab__758[] = {
  /*  0 */           0,        1045,
};

static const uint16_t ud_itab__759[] = {
  /*  0 */           0,        1046,
};

static const uint16_t ud_itab__760[] = {
  /*  0 */           0,        1047,
};

static const uint16_t ud_itab__761[] = {
  /*  0 */           0,        1048,
};

static const uint16_t ud_itab__709[] = {
  /*  0 */  GROUP(710),  GROUP(711),  GROUP(712),  GROUP(713),
  /*  4 */  GROUP(714),  GROUP(715),  GROUP(716),  GROUP(717),
  /*  8 */  GROUP(718),  GROUP(719),  GROUP(720),  GROUP(721),
  /*  c */  GROUP(722),  GROUP(723),  GROUP(724),  GROUP(725),
  /* 10 */  GROUP(726),           0,           0,           0,
  /* 14 */           0,           0,           0,           0,
  /* 18 */  GROUP(727),  GROUP(728),  GROUP(729),  GROUP(730),
  /* 1c */  GROUP(731),  GROUP(732),  GROUP(733),  GROUP(734),
  /* 20 */  GROUP(735),  GROUP(736),           0,           0,
  /* 24 */  GROUP(737),  GROUP(738),           0,           0,
  /* 28 */  GROUP(739),  GROUP(740),  GROUP(741),  GROUP(742),
  /* 2c */  GROUP(743),  GROUP(744),  GROUP(745),           0,
  /* 30 */  GROUP(746),  GROUP(747),  GROUP(748),  GROUP(749),
  /* 34 */  GROUP(750),  GROUP(751),  GROUP(752),  GROUP(753),
  /* 38 */  GROUP(754),  GROUP(755),  GROUP(756),  GROUP(757),
  /* 3c */  GROUP(758),  GROUP(759),  GROUP(760),  GROUP(761),
};

static const uint16_t ud_itab__700[] = {
  /*  0 */  GROUP(701),  GROUP(709),
};

static const uint16_t ud_itab__764[] = {
  /*  0 */        1049,           0,
};

static const uint16_t ud_itab__765[] = {
  /*  0 */        1050,           0,
};

static const uint16_t ud_itab__766[] = {
  /*  0 */        1051,           0,
};

static const uint16_t ud_itab__767[] = {
  /*  0 */        1052,           0,
};

static const uint16_t ud_itab__768[] = {
  /*  0 */        1053,           0,
};

static const uint16_t ud_itab__769[] = {
  /*  0 */        1054,           0,
};

static const uint16_t ud_itab__770[] = {
  /*  0 */        1055,           0,
};

static const uint16_t ud_itab__771[] = {
  /*  0 */        1056,           0,
};

static const uint16_t ud_itab__763[] = {
  /*  0 */  GROUP(764),  GROUP(765),  GROUP(766),  GROUP(767),
  /*  4 */  GROUP(768),  GROUP(769),  GROUP(770),  GROUP(771),
};

static const uint16_t ud_itab__773[] = {
  /*  0 */           0,        1057,
};

static const uint16_t ud_itab__774[] = {
  /*  0 */           0,        1058,
};

static const uint16_t ud_itab__775[] = {
  /*  0 */           0,        1059,
};

static const uint16_t ud_itab__776[] = {
  /*  0 */           0,        1060,
};

static const uint16_t ud_itab__777[] = {
  /*  0 */           0,        1061,
};

static const uint16_t ud_itab__778[] = {
  /*  0 */           0,        1062,
};

static const uint16_t ud_itab__779[] = {
  /*  0 */           0,        1063,
};

static const uint16_t ud_itab__780[] = {
  /*  0 */           0,        1064,
};

static const uint16_t ud_itab__781[] = {
  /*  0 */           0,        1065,
};

static const uint16_t ud_itab__782[] = {
  /*  0 */           0,        1066,
};

static const uint16_t ud_itab__783[] = {
  /*  0 */           0,        1067,
};

static const uint16_t ud_itab__784[] = {
  /*  0 */           0,        1068,
};

static const uint16_t ud_itab__785[] = {
  /*  0 */           0,        1069,
};

static const uint16_t ud_itab__786[] = {
  /*  0 */           0,        1070,
};

static const uint16_t ud_itab__787[] = {
  /*  0 */           0,        1071,
};

static const uint16_t ud_itab__788[] = {
  /*  0 */           0,        1072,
};

static const uint16_t ud_itab__789[] = {
  /*  0 */           0,        1073,
};

static const uint16_t ud_itab__790[] = {
  /*  0 */           0,        1074,
};

static const uint16_t ud_itab__791[] = {
  /*  0 */           0,        1075,
};

static const uint16_t ud_itab__792[] = {
  /*  0 */           0,        1076,
};

static const uint16_t ud_itab__793[] = {
  /*  0 */           0,        1077,
};

static const uint16_t ud_itab__794[] = {
  /*  0 */           0,        1078,
};

static const uint16_t ud_itab__795[] = {
  /*  0 */           0,        1079,
};

static const uint16_t ud_itab__796[] = {
  /*  0 */           0,        1080,
};

static const uint16_t ud_itab__797[] = {
  /*  0 */           0,        1081,
};

static const uint16_t ud_itab__798[] = {
  /*  0 */           0,        1082,
};

static const uint16_t ud_itab__799[] = {
  /*  0 */           0,        1083,
};

static const uint16_t ud_itab__800[] = {
  /*  0 */           0,        1084,
};

static const uint16_t ud_itab__801[] = {
  /*  0 */           0,        1085,
};

static const uint16_t ud_itab__802[] = {
  /*  0 */           0,        1086,
};

static const uint16_t ud_itab__803[] = {
  /*  0 */           0,        1087,
};

static const uint16_t ud_itab__804[] = {
  /*  0 */           0,        1088,
};

static const uint16_t ud_itab__805[] = {
  /*  0 */           0,        1089,
};

static const uint16_t ud_itab__772[] = {
  /*  0 */  GROUP(773),  GROUP(774),  GROUP(775),  GROUP(776),
  /*  4 */  GROUP(777),  GROUP(778),  GROUP(779),  GROUP(780),
  /*  8 */  GROUP(781),  GROUP(782),  GROUP(783),  GROUP(784),
  /*  c */  GROUP(785),  GROUP(786),  GROUP(787),  GROUP(788),
  /* 10 */  GROUP(789),  GROUP(790),  GROUP(791),  GROUP(792),
  /* 14 */  GROUP(793),  GROUP(794),  GROUP(795),  GROUP(796),
  /* 18 */  GROUP(797),  GROUP(798),  GROUP(799),  GROUP(800),
  /* 1c */  GROUP(801),  GROUP(802),  GROUP(803),  GROUP(804),
  /* 20 */           0,           0,           0,           0,
  /* 24 */           0,           0,           0,           0,
  /* 28 */           0,  GROUP(805),           0,           0,
  /* 2c */           0,           0,           0,           0,
  /* 30 */           0,           0,           0,           0,
  /* 34 */           0,           0,           0,           0,
  /* 38 */           0,           0,           0,           0,
  /* 3c */           0,           0,           0,           0,
};

static const uint16_t ud_itab__762[] = {
  /*  0 */  GROUP(763),  GROUP(772),
};

static const uint16_t ud_itab__808[] = {
  /*  0 */        1090,           0,
};

static const uint16_t ud_itab__809[] = {
  /*  0 */        1091,           0,
};

static const uint16_t ud_itab__810[] = {
  /*  0 */        1092,           0,
};

static const uint16_t ud_itab__811[] = {
  /*  0 */        1093,           0,
};

static const uint16_t ud_itab__812[] = {
  /*  0 */        1094,           0,
};

static const uint16_t ud_itab__813[] = {
  /*  0 */        1095,           0,
};

static const uint16_t ud_itab__807[] = {
  /*  0 */  GROUP(808),  GROUP(809),  GROUP(810),  GROUP(811),
  /*  4 */           0,  GROUP(812),           0,  GROUP(813),
};

static const uint16_t ud_itab__815[] = {
  /*  0 */           0,        1096,
};

static const uint16_t ud_itab__816[] = {
  /*  0 */           0,        1097,
};

static const uint16_t ud_itab__817[] = {
  /*  0 */           0,        1098,
};

static const uint16_t ud_itab__818[] = {
  /*  0 */           0,        1099,
};

static const uint16_t ud_itab__819[] = {
  /*  0 */           0,        1100,
};

static const uint16_t ud_itab__820[] = {
  /*  0 */           0,        1101,
};

static const uint16_t ud_itab__821[] = {
  /*  0 */           0,        1102,
};

static const uint16_t ud_itab__822[] = {
  /*  0 */           0,        1103,
};

static const uint16_t ud_itab__823[] = {
  /*  0 */           0,        1104,
};

static const uint16_t ud_itab__824[] = {
  /*  0 */           0,        1105,
};

static const uint16_t ud_itab__825[] = {
  /*  0 */           0,        1106,
};

static const uint16_t ud_itab__826[] = {
  /*  0 */           0,        1107,
};

static const uint16_t ud_itab__827[] = {
  /*  0 */           0,        1108,
};

static const uint16_t ud_itab__828[] = {
  /*  0 */           0,        1109,
};

static const uint16_t ud_itab__829[] = {
  /*  0 */           0,        1110,
};

static const uint16_t ud_itab__830[] = {
  /*  0 */           0,        1111,
};

static const uint16_t ud_itab__831[] = {
  /*  0 */           0,        1112,
};

static const uint16_t ud_itab__832[] = {
  /*  0 */           0,        1113,
};

static const uint16_t ud_itab__833[] = {
  /*  0 */           0,        1114,
};

static const uint16_t ud_itab__834[] = {
  /*  0 */           0,        1115,
};

static const uint16_t ud_itab__835[] = {
  /*  0 */           0,        1116,
};

static const uint16_t ud_itab__836[] = {
  /*  0 */           0,        1117,
};

static const uint16_t ud_itab__837[] = {
  /*  0 */           0,        1118,
};

static const uint16_t ud_itab__838[] = {
  /*  0 */           0,        1119,
};

static const uint16_t ud_itab__839[] = {
  /*  0 */           0,        1120,
};

static const uint16_t ud_itab__840[] = {
  /*  0 */           0,        1121,
};

static const uint16_t ud_itab__841[] = {
  /*  0 */           0,        1122,
};

static const uint16_t ud_itab__842[] = {
  /*  0 */           0,        1123,
};

static const uint16_t ud_itab__843[] = {
  /*  0 */           0,        1124,
};

static const uint16_t ud_itab__844[] = {
  /*  0 */           0,        1125,
};

static const uint16_t ud_itab__845[] = {
  /*  0 */           0,        1126,
};

static const uint16_t ud_itab__846[] = {
  /*  0 */           0,        1127,
};

static const uint16_t ud_itab__847[] = {
  /*  0 */           0,        1128,
};

static const uint16_t ud_itab__848[] = {
  /*  0 */           0,        1129,
};

static const uint16_t ud_itab__849[] = {
  /*  0 */           0,        1130,
};

static const uint16_t ud_itab__850[] = {
  /*  0 */           0,        1131,
};

static const uint16_t ud_itab__851[] = {
  /*  0 */           0,        1132,
};

static const uint16_t ud_itab__852[] = {
  /*  0 */           0,        1133,
};

static const uint16_t ud_itab__853[] = {
  /*  0 */           0,        1134,
};

static const uint16_t ud_itab__854[] = {
  /*  0 */           0,        1135,
};

static const uint16_t ud_itab__855[] = {
  /*  0 */           0,        1136,
};

static const uint16_t ud_itab__856[] = {
  /*  0 */           0,        1137,
};

static const uint16_t ud_itab__857[] = {
  /*  0 */           0,        1138,
};

static const uint16_t ud_itab__858[] = {
  /*  0 */           0,        1139,
};

static const uint16_t ud_itab__859[] = {
  /*  0 */           0,        1140,
};

static const uint16_t ud_itab__860[] = {
  /*  0 */           0,        1141,
};

static const uint16_t ud_itab__861[] = {
  /*  0 */           0,        1142,
};

static const uint16_t ud_itab__862[] = {
  /*  0 */           0,        1143,
};

static const uint16_t ud_itab__863[] = {
  /*  0 */           0,        1144,
};

static const uint16_t ud_itab__864[] = {
  /*  0 */           0,        1145,
};

static const uint16_t ud_itab__814[] = {
  /*  0 */  GROUP(815),  GROUP(816),  GROUP(817),  GROUP(818),
  /*  4 */  GROUP(819),  GROUP(820),  GROUP(821),  GROUP(822),
  /*  8 */  GROUP(823),  GROUP(824),  GROUP(825),  GROUP(826),
  /*  c */  GROUP(827),  GROUP(828),  GROUP(829),  GROUP(830),
  /* 10 */  GROUP(831),  GROUP(832),  GROUP(833),  GROUP(834),
  /* 14 */  GROUP(835),  GROUP(836),  GROUP(837),  GROUP(838),
  /* 18 */  GROUP(839),  GROUP(840),  GROUP(841),  GROUP(842),
  /* 1c */  GROUP(843),  GROUP(844),  GROUP(845),  GROUP(846),
  /* 20 */           0,           0,  GROUP(847),  GROUP(848),
  /* 24 */           0,           0,           0,           0,
  /* 28 */  GROUP(849),  GROUP(850),  GROUP(851),  GROUP(852),
  /* 2c */  GROUP(853),  GROUP(854),  GROUP(855),  GROUP(856),
  /* 30 */  GROUP(857),  GROUP(858),  GROUP(859),  GROUP(860),
  /* 34 */  GROUP(861),  GROUP(862),  GROUP(863),  GROUP(864),
  /* 38 */           0,           0,           0,           0,
  /* 3c */           0,           0,           0,           0,
};

static const uint16_t ud_itab__806[] = {
  /*  0 */  GROUP(807),  GROUP(814),
};

static const uint16_t ud_itab__867[] = {
  /*  0 */        1146,           0,
};

static const uint16_t ud_itab__868[] = {
  /*  0 */        1147,           0,
};

static const uint16_t ud_itab__869[] = {
  /*  0 */        1148,           0,
};

static const uint16_t ud_itab__870[] = {
  /*  0 */        1149,           0,
};

static const uint16_t ud_itab__871[] = {
  /*  0 */        1150,           0,
};

static const uint16_t ud_itab__872[] = {
  /*  0 */        1151,           0,
};

static const uint16_t ud_itab__873[] = {
  /*  0 */        1152,           0,
};

static const uint16_t ud_itab__874[] = {
  /*  0 */        1153,           0,
};

static const uint16_t ud_itab__866[] = {
  /*  0 */  GROUP(867),  GROUP(868),  GROUP(869),  GROUP(870),
  /*  4 */  GROUP(871),  GROUP(872),  GROUP(873),  GROUP(874),
};

static const uint16_t ud_itab__876[] = {
  /*  0 */           0,        1154,
};

static const uint16_t ud_itab__877[] = {
  /*  0 */           0,        1155,
};

static const uint16_t ud_itab__878[] = {
  /*  0 */           0,        1156,
};

static const uint16_t ud_itab__879[] = {
  /*  0 */           0,        1157,
};

static const uint16_t ud_itab__880[] = {
  /*  0 */           0,        1158,
};

static const uint16_t ud_itab__881[] = {
  /*  0 */           0,        1159,
};

static const uint16_t ud_itab__882[] = {
  /*  0 */           0,        1160,
};

static const uint16_t ud_itab__883[] = {
  /*  0 */           0,        1161,
};

static const uint16_t ud_itab__884[] = {
  /*  0 */           0,        1162,
};

static const uint16_t ud_itab__885[] = {
  /*  0 */           0,        1163,
};

static const uint16_t ud_itab__886[] = {
  /*  0 */           0,        1164,
};

static const uint16_t ud_itab__887[] = {
  /*  0 */           0,        1165,
};

static const uint16_t ud_itab__888[] = {
  /*  0 */           0,        1166,
};

static const uint16_t ud_itab__889[] = {
  /*  0 */           0,        1167,
};

static const uint16_t ud_itab__890[] = {
  /*  0 */           0,        1168,
};

static const uint16_t ud_itab__891[] = {
  /*  0 */           0,        1169,
};

static const uint16_t ud_itab__892[] = {
  /*  0 */           0,        1170,
};

static const uint16_t ud_itab__893[] = {
  /*  0 */           0,        1171,
};

static const uint16_t ud_itab__894[] = {
  /*  0 */           0,        1172,
};

static const uint16_t ud_itab__895[] = {
  /*  0 */           0,        1173,
};

static const uint16_t ud_itab__896[] = {
  /*  0 */           0,        1174,
};

static const uint16_t ud_itab__897[] = {
  /*  0 */           0,        1175,
};

static const uint16_t ud_itab__898[] = {
  /*  0 */           0,        1176,
};

static const uint16_t ud_itab__899[] = {
  /*  0 */           0,        1177,
};

static const uint16_t ud_itab__900[] = {
  /*  0 */           0,        1178,
};

static const uint16_t ud_itab__901[] = {
  /*  0 */           0,        1179,
};

static const uint16_t ud_itab__902[] = {
  /*  0 */           0,        1180,
};

static const uint16_t ud_itab__903[] = {
  /*  0 */           0,        1181,
};

static const uint16_t ud_itab__904[] = {
  /*  0 */           0,        1182,
};

static const uint16_t ud_itab__905[] = {
  /*  0 */           0,        1183,
};

static const uint16_t ud_itab__906[] = {
  /*  0 */           0,        1184,
};

static const uint16_t ud_itab__907[] = {
  /*  0 */           0,        1185,
};

static const uint16_t ud_itab__908[] = {
  /*  0 */           0,        1186,
};

static const uint16_t ud_itab__909[] = {
  /*  0 */           0,        1187,
};

static const uint16_t ud_itab__910[] = {
  /*  0 */           0,        1188,
};

static const uint16_t ud_itab__911[] = {
  /*  0 */           0,        1189,
};

static const uint16_t ud_itab__912[] = {
  /*  0 */           0,        1190,
};

static const uint16_t ud_itab__913[] = {
  /*  0 */           0,        1191,
};

static const uint16_t ud_itab__914[] = {
  /*  0 */           0,        1192,
};

static const uint16_t ud_itab__915[] = {
  /*  0 */           0,        1193,
};

static const uint16_t ud_itab__916[] = {
  /*  0 */           0,        1194,
};

static const uint16_t ud_itab__917[] = {
  /*  0 */           0,        1195,
};

static const uint16_t ud_itab__918[] = {
  /*  0 */           0,        1196,
};

static const uint16_t ud_itab__919[] = {
  /*  0 */           0,        1197,
};

static const uint16_t ud_itab__920[] = {
  /*  0 */           0,        1198,
};

static const uint16_t ud_itab__921[] = {
  /*  0 */           0,        1199,
};

static const uint16_t ud_itab__922[] = {
  /*  0 */           0,        1200,
};

static const uint16_t ud_itab__923[] = {
  /*  0 */           0,        1201,
};

static const uint16_t ud_itab__924[] = {
  /*  0 */           0,        1202,
};

static const uint16_t ud_itab__925[] = {
  /*  0 */           0,        1203,
};

static const uint16_t ud_itab__926[] = {
  /*  0 */           0,        1204,
};

static const uint16_t ud_itab__927[] = {
  /*  0 */           0,        1205,
};

static const uint16_t ud_itab__928[] = {
  /*  0 */           0,        1206,
};

static const uint16_t ud_itab__929[] = {
  /*  0 */           0,        1207,
};

static const uint16_t ud_itab__930[] = {
  /*  0 */           0,        1208,
};

static const uint16_t ud_itab__931[] = {
  /*  0 */           0,        1209,
};

static const uint16_t ud_itab__932[] = {
  /*  0 */           0,        1210,
};

static const uint16_t ud_itab__933[] = {
  /*  0 */           0,        1211,
};

static const uint16_t ud_itab__934[] = {
  /*  0 */           0,        1212,
};

static const uint16_t ud_itab__935[] = {
  /*  0 */           0,        1213,
};

static const uint16_t ud_itab__936[] = {
  /*  0 */           0,        1214,
};

static const uint16_t ud_itab__937[] = {
  /*  0 */           0,        1215,
};

static const uint16_t ud_itab__938[] = {
  /*  0 */           0,        1216,
};

static const uint16_t ud_itab__939[] = {
  /*  0 */           0,        1217,
};

static const uint16_t ud_itab__875[] = {
  /*  0 */  GROUP(876),  GROUP(877),  GROUP(878),  GROUP(879),
  /*  4 */  GROUP(880),  GROUP(881),  GROUP(882),  GROUP(883),
  /*  8 */  GROUP(884),  GROUP(885),  GROUP(886),  GROUP(887),
  /*  c */  GROUP(888),  GROUP(889),  GROUP(890),  GROUP(891),
  /* 10 */  GROUP(892),  GROUP(893),  GROUP(894),  GROUP(895),
  /* 14 */  GROUP(896),  GROUP(897),  GROUP(898),  GROUP(899),
  /* 18 */  GROUP(900),  GROUP(901),  GROUP(902),  GROUP(903),
  /* 1c */  GROUP(904),  GROUP(905),  GROUP(906),  GROUP(907),
  /* 20 */  GROUP(908),  GROUP(909),  GROUP(910),  GROUP(911),
  /* 24 */  GROUP(912),  GROUP(913),  GROUP(914),  GROUP(915),
  /* 28 */  GROUP(916),  GROUP(917),  GROUP(918),  GROUP(919),
  /* 2c */  GROUP(920),  GROUP(921),  GROUP(922),  GROUP(923),
  /* 30 */  GROUP(924),  GROUP(925),  GROUP(926),  GROUP(927),
  /* 34 */  GROUP(928),  GROUP(929),  GROUP(930),  GROUP(931),
  /* 38 */  GROUP(932),  GROUP(933),  GROUP(934),  GROUP(935),
  /* 3c */  GROUP(936),  GROUP(937),  GROUP(938),  GROUP(939),
};

static const uint16_t ud_itab__865[] = {
  /*  0 */  GROUP(866),  GROUP(875),
};

static const uint16_t ud_itab__942[] = {
  /*  0 */        1218,           0,
};

static const uint16_t ud_itab__943[] = {
  /*  0 */        1219,           0,
};

static const uint16_t ud_itab__944[] = {
  /*  0 */        1220,           0,
};

static const uint16_t ud_itab__945[] = {
  /*  0 */        1221,           0,
};

static const uint16_t ud_itab__946[] = {
  /*  0 */        1222,           0,
};

static const uint16_t ud_itab__947[] = {
  /*  0 */        1223,           0,
};

static const uint16_t ud_itab__948[] = {
  /*  0 */        1224,           0,
};

static const uint16_t ud_itab__941[] = {
  /*  0 */  GROUP(942),  GROUP(943),  GROUP(944),  GROUP(945),
  /*  4 */  GROUP(946),           0,  GROUP(947),  GROUP(948),
};

static const uint16_t ud_itab__950[] = {
  /*  0 */           0,        1225,
};

static const uint16_t ud_itab__951[] = {
  /*  0 */           0,        1226,
};

static const uint16_t ud_itab__952[] = {
  /*  0 */           0,        1227,
};

static const uint16_t ud_itab__953[] = {
  /*  0 */           0,        1228,
};

static const uint16_t ud_itab__954[] = {
  /*  0 */           0,        1229,
};

static const uint16_t ud_itab__955[] = {
  /*  0 */           0,        1230,
};

static const uint16_t ud_itab__956[] = {
  /*  0 */           0,        1231,
};

static const uint16_t ud_itab__957[] = {
  /*  0 */           0,        1232,
};

static const uint16_t ud_itab__958[] = {
  /*  0 */           0,        1233,
};

static const uint16_t ud_itab__959[] = {
  /*  0 */           0,        1234,
};

static const uint16_t ud_itab__960[] = {
  /*  0 */           0,        1235,
};

static const uint16_t ud_itab__961[] = {
  /*  0 */           0,        1236,
};

static const uint16_t ud_itab__962[] = {
  /*  0 */           0,        1237,
};

static const uint16_t ud_itab__963[] = {
  /*  0 */           0,        1238,
};

static const uint16_t ud_itab__964[] = {
  /*  0 */           0,        1239,
};

static const uint16_t ud_itab__965[] = {
  /*  0 */           0,        1240,
};

static const uint16_t ud_itab__966[] = {
  /*  0 */           0,        1241,
};

static const uint16_t ud_itab__967[] = {
  /*  0 */           0,        1242,
};

static const uint16_t ud_itab__968[] = {
  /*  0 */           0,        1243,
};

static const uint16_t ud_itab__969[] = {
  /*  0 */           0,        1244,
};

static const uint16_t ud_itab__970[] = {
  /*  0 */           0,        1245,
};

static const uint16_t ud_itab__971[] = {
  /*  0 */           0,        1246,
};

static const uint16_t ud_itab__972[] = {
  /*  0 */           0,        1247,
};

static const uint16_t ud_itab__973[] = {
  /*  0 */           0,        1248,
};

static const uint16_t ud_itab__974[] = {
  /*  0 */           0,        1249,
};

static const uint16_t ud_itab__975[] = {
  /*  0 */           0,        1250,
};

static const uint16_t ud_itab__976[] = {
  /*  0 */           0,        1251,
};

static const uint16_t ud_itab__977[] = {
  /*  0 */           0,        1252,
};

static const uint16_t ud_itab__978[] = {
  /*  0 */           0,        1253,
};

static const uint16_t ud_itab__979[] = {
  /*  0 */           0,        1254,
};

static const uint16_t ud_itab__980[] = {
  /*  0 */           0,        1255,
};

static const uint16_t ud_itab__981[] = {
  /*  0 */           0,        1256,
};

static const uint16_t ud_itab__982[] = {
  /*  0 */           0,        1257,
};

static const uint16_t ud_itab__983[] = {
  /*  0 */           0,        1258,
};

static const uint16_t ud_itab__984[] = {
  /*  0 */           0,        1259,
};

static const uint16_t ud_itab__985[] = {
  /*  0 */           0,        1260,
};

static const uint16_t ud_itab__986[] = {
  /*  0 */           0,        1261,
};

static const uint16_t ud_itab__987[] = {
  /*  0 */           0,        1262,
};

static const uint16_t ud_itab__988[] = {
  /*  0 */           0,        1263,
};

static const uint16_t ud_itab__989[] = {
  /*  0 */           0,        1264,
};

static const uint16_t ud_itab__990[] = {
  /*  0 */           0,        1265,
};

static const uint16_t ud_itab__991[] = {
  /*  0 */           0,        1266,
};

static const uint16_t ud_itab__992[] = {
  /*  0 */           0,        1267,
};

static const uint16_t ud_itab__993[] = {
  /*  0 */           0,        1268,
};

static const uint16_t ud_itab__994[] = {
  /*  0 */           0,        1269,
};

static const uint16_t ud_itab__995[] = {
  /*  0 */           0,        1270,
};

static const uint16_t ud_itab__996[] = {
  /*  0 */           0,        1271,
};

static const uint16_t ud_itab__997[] = {
  /*  0 */           0,        1272,
};

static const uint16_t ud_itab__949[] = {
  /*  0 */  GROUP(950),  GROUP(951),  GROUP(952),  GROUP(953),
  /*  4 */  GROUP(954),  GROUP(955),  GROUP(956),  GROUP(957),
  /*  8 */  GROUP(958),  GROUP(959),  GROUP(960),  GROUP(961),
  /*  c */  GROUP(962),  GROUP(963),  GROUP(964),  GROUP(965),
  /* 10 */  GROUP(966),  GROUP(967),  GROUP(968),  GROUP(969),
  /* 14 */  GROUP(970),  GROUP(971),  GROUP(972),  GROUP(973),
  /* 18 */  GROUP(974),  GROUP(975),  GROUP(976),  GROUP(977),
  /* 1c */  GROUP(978),  GROUP(979),  GROUP(980),  GROUP(981),
  /* 20 */  GROUP(982),  GROUP(983),  GROUP(984),  GROUP(985),
  /* 24 */  GROUP(986),  GROUP(987),  GROUP(988),  GROUP(989),
  /* 28 */  GROUP(990),  GROUP(991),  GROUP(992),  GROUP(993),
  /* 2c */  GROUP(994),  GROUP(995),  GROUP(996),  GROUP(997),
  /* 30 */           0,           0,           0,           0,
  /* 34 */           0,           0,           0,           0,
  /* 38 */           0,           0,           0,           0,
  /* 3c */           0,           0,           0,           0,
};

static const uint16_t ud_itab__940[] = {
  /*  0 */  GROUP(941),  GROUP(949),
};

static const uint16_t ud_itab__1000[] = {
  /*  0 */        1273,           0,
};

static const uint16_t ud_itab__1001[] = {
  /*  0 */        1274,           0,
};

static const uint16_t ud_itab__1002[] = {
  /*  0 */        1275,           0,
};

static const uint16_t ud_itab__1003[] = {
  /*  0 */        1276,           0,
};

static const uint16_t ud_itab__1004[] = {
  /*  0 */        1277,           0,
};

static const uint16_t ud_itab__1005[] = {
  /*  0 */        1278,           0,
};

static const uint16_t ud_itab__1006[] = {
  /*  0 */        1279,           0,
};

static const uint16_t ud_itab__1007[] = {
  /*  0 */        1280,           0,
};

static const uint16_t ud_itab__999[] = {
  /*  0 */ GROUP(1000), GROUP(1001), GROUP(1002), GROUP(1003),
  /*  4 */ GROUP(1004), GROUP(1005), GROUP(1006), GROUP(1007),
};

static const uint16_t ud_itab__1009[] = {
  /*  0 */           0,        1281,
};

static const uint16_t ud_itab__1010[] = {
  /*  0 */           0,        1282,
};

static const uint16_t ud_itab__1011[] = {
  /*  0 */           0,        1283,
};

static const uint16_t ud_itab__1012[] = {
  /*  0 */           0,        1284,
};

static const uint16_t ud_itab__1013[] = {
  /*  0 */           0,        1285,
};

static const uint16_t ud_itab__1014[] = {
  /*  0 */           0,        1286,
};

static const uint16_t ud_itab__1015[] = {
  /*  0 */           0,        1287,
};

static const uint16_t ud_itab__1016[] = {
  /*  0 */           0,        1288,
};

static const uint16_t ud_itab__1017[] = {
  /*  0 */           0,        1289,
};

static const uint16_t ud_itab__1018[] = {
  /*  0 */           0,        1290,
};

static const uint16_t ud_itab__1019[] = {
  /*  0 */           0,        1291,
};

static const uint16_t ud_itab__1020[] = {
  /*  0 */           0,        1292,
};

static const uint16_t ud_itab__1021[] = {
  /*  0 */           0,        1293,
};

static const uint16_t ud_itab__1022[] = {
  /*  0 */           0,        1294,
};

static const uint16_t ud_itab__1023[] = {
  /*  0 */           0,        1295,
};

static const uint16_t ud_itab__1024[] = {
  /*  0 */           0,        1296,
};

static const uint16_t ud_itab__1025[] = {
  /*  0 */           0,        1297,
};

static const uint16_t ud_itab__1026[] = {
  /*  0 */           0,        1298,
};

static const uint16_t ud_itab__1027[] = {
  /*  0 */           0,        1299,
};

static const uint16_t ud_itab__1028[] = {
  /*  0 */           0,        1300,
};

static const uint16_t ud_itab__1029[] = {
  /*  0 */           0,        1301,
};

static const uint16_t ud_itab__1030[] = {
  /*  0 */           0,        1302,
};

static const uint16_t ud_itab__1031[] = {
  /*  0 */           0,        1303,
};

static const uint16_t ud_itab__1032[] = {
  /*  0 */           0,        1304,
};

static const uint16_t ud_itab__1033[] = {
  /*  0 */           0,        1305,
};

static const uint16_t ud_itab__1034[] = {
  /*  0 */           0,        1306,
};

static const uint16_t ud_itab__1035[] = {
  /*  0 */           0,        1307,
};

static const uint16_t ud_itab__1036[] = {
  /*  0 */           0,        1308,
};

static const uint16_t ud_itab__1037[] = {
  /*  0 */           0,        1309,
};

static const uint16_t ud_itab__1038[] = {
  /*  0 */           0,        1310,
};

static const uint16_t ud_itab__1039[] = {
  /*  0 */           0,        1311,
};

static const uint16_t ud_itab__1040[] = {
  /*  0 */           0,        1312,
};

static const uint16_t ud_itab__1041[] = {
  /*  0 */           0,        1313,
};

static const uint16_t ud_itab__1042[] = {
  /*  0 */           0,        1314,
};

static const uint16_t ud_itab__1043[] = {
  /*  0 */           0,        1315,
};

static const uint16_t ud_itab__1044[] = {
  /*  0 */           0,        1316,
};

static const uint16_t ud_itab__1045[] = {
  /*  0 */           0,        1317,
};

static const uint16_t ud_itab__1046[] = {
  /*  0 */           0,        1318,
};

static const uint16_t ud_itab__1047[] = {
  /*  0 */           0,        1319,
};

static const uint16_t ud_itab__1048[] = {
  /*  0 */           0,        1320,
};

static const uint16_t ud_itab__1049[] = {
  /*  0 */           0,        1321,
};

static const uint16_t ud_itab__1050[] = {
  /*  0 */           0,        1322,
};

static const uint16_t ud_itab__1051[] = {
  /*  0 */           0,        1323,
};

static const uint16_t ud_itab__1052[] = {
  /*  0 */           0,        1324,
};

static const uint16_t ud_itab__1053[] = {
  /*  0 */           0,        1325,
};

static const uint16_t ud_itab__1054[] = {
  /*  0 */           0,        1326,
};

static const uint16_t ud_itab__1055[] = {
  /*  0 */           0,        1327,
};

static const uint16_t ud_itab__1056[] = {
  /*  0 */           0,        1328,
};

static const uint16_t ud_itab__1057[] = {
  /*  0 */           0,        1329,
};

static const uint16_t ud_itab__1058[] = {
  /*  0 */           0,        1330,
};

static const uint16_t ud_itab__1059[] = {
  /*  0 */           0,        1331,
};

static const uint16_t ud_itab__1060[] = {
  /*  0 */           0,        1332,
};

static const uint16_t ud_itab__1061[] = {
  /*  0 */           0,        1333,
};

static const uint16_t ud_itab__1062[] = {
  /*  0 */           0,        1334,
};

static const uint16_t ud_itab__1063[] = {
  /*  0 */           0,        1335,
};

static const uint16_t ud_itab__1064[] = {
  /*  0 */           0,        1336,
};

static const uint16_t ud_itab__1065[] = {
  /*  0 */           0,        1337,
};

static const uint16_t ud_itab__1008[] = {
  /*  0 */ GROUP(1009), GROUP(1010), GROUP(1011), GROUP(1012),
  /*  4 */ GROUP(1013), GROUP(1014), GROUP(1015), GROUP(1016),
  /*  8 */ GROUP(1017), GROUP(1018), GROUP(1019), GROUP(1020),
  /*  c */ GROUP(1021), GROUP(1022), GROUP(1023), GROUP(1024),
  /* 10 */ GROUP(1025), GROUP(1026), GROUP(1027), GROUP(1028),
  /* 14 */ GROUP(1029), GROUP(1030), GROUP(1031), GROUP(1032),
  /* 18 */           0, GROUP(1033),           0,           0,
  /* 1c */           0,           0,           0,           0,
  /* 20 */ GROUP(1034), GROUP(1035), GROUP(1036), GROUP(1037),
  /* 24 */ GROUP(1038), GROUP(1039), GROUP(1040), GROUP(1041),
  /* 28 */ GROUP(1042), GROUP(1043), GROUP(1044), GROUP(1045),
  /* 2c */ GROUP(1046), GROUP(1047), GROUP(1048), GROUP(1049),
  /* 30 */ GROUP(1050), GROUP(1051), GROUP(1052), GROUP(1053),
  /* 34 */ GROUP(1054), GROUP(1055), GROUP(1056), GROUP(1057),
  /* 38 */ GROUP(1058), GROUP(1059), GROUP(1060), GROUP(1061),
  /* 3c */ GROUP(1062), GROUP(1063), GROUP(1064), GROUP(1065),
};

static const uint16_t ud_itab__998[] = {
  /*  0 */  GROUP(999), GROUP(1008),
};

static const uint16_t ud_itab__1068[] = {
  /*  0 */        1338,           0,
};

static const uint16_t ud_itab__1069[] = {
  /*  0 */        1339,           0,
};

static const uint16_t ud_itab__1070[] = {
  /*  0 */        1340,           0,
};

static const uint16_t ud_itab__1071[] = {
  /*  0 */        1341,           0,
};

static const uint16_t ud_itab__1072[] = {
  /*  0 */        1342,           0,
};

static const uint16_t ud_itab__1073[] = {
  /*  0 */        1343,           0,
};

static const uint16_t ud_itab__1074[] = {
  /*  0 */        1344,           0,
};

static const uint16_t ud_itab__1075[] = {
  /*  0 */        1345,           0,
};

static const uint16_t ud_itab__1067[] = {
  /*  0 */ GROUP(1068), GROUP(1069), GROUP(1070), GROUP(1071),
  /*  4 */ GROUP(1072), GROUP(1073), GROUP(1074), GROUP(1075),
};

static const uint16_t ud_itab__1077[] = {
  /*  0 */           0,        1346,
};

static const uint16_t ud_itab__1078[] = {
  /*  0 */           0,        1347,
};

static const uint16_t ud_itab__1079[] = {
  /*  0 */           0,        1348,
};

static const uint16_t ud_itab__1080[] = {
  /*  0 */           0,        1349,
};

static const uint16_t ud_itab__1081[] = {
  /*  0 */           0,        1350,
};

static const uint16_t ud_itab__1082[] = {
  /*  0 */           0,        1351,
};

static const uint16_t ud_itab__1083[] = {
  /*  0 */           0,        1352,
};

static const uint16_t ud_itab__1084[] = {
  /*  0 */           0,        1353,
};

static const uint16_t ud_itab__1085[] = {
  /*  0 */           0,        1354,
};

static const uint16_t ud_itab__1086[] = {
  /*  0 */           0,        1355,
};

static const uint16_t ud_itab__1087[] = {
  /*  0 */           0,        1356,
};

static const uint16_t ud_itab__1088[] = {
  /*  0 */           0,        1357,
};

static const uint16_t ud_itab__1089[] = {
  /*  0 */           0,        1358,
};

static const uint16_t ud_itab__1090[] = {
  /*  0 */           0,        1359,
};

static const uint16_t ud_itab__1091[] = {
  /*  0 */           0,        1360,
};

static const uint16_t ud_itab__1092[] = {
  /*  0 */           0,        1361,
};

static const uint16_t ud_itab__1093[] = {
  /*  0 */           0,        1362,
};

static const uint16_t ud_itab__1094[] = {
  /*  0 */           0,        1363,
};

static const uint16_t ud_itab__1095[] = {
  /*  0 */           0,        1364,
};

static const uint16_t ud_itab__1096[] = {
  /*  0 */           0,        1365,
};

static const uint16_t ud_itab__1097[] = {
  /*  0 */           0,        1366,
};

static const uint16_t ud_itab__1098[] = {
  /*  0 */           0,        1367,
};

static const uint16_t ud_itab__1099[] = {
  /*  0 */           0,        1368,
};

static const uint16_t ud_itab__1100[] = {
  /*  0 */           0,        1369,
};

static const uint16_t ud_itab__1101[] = {
  /*  0 */           0,        1370,
};

static const uint16_t ud_itab__1102[] = {
  /*  0 */           0,        1371,
};

static const uint16_t ud_itab__1103[] = {
  /*  0 */           0,        1372,
};

static const uint16_t ud_itab__1104[] = {
  /*  0 */           0,        1373,
};

static const uint16_t ud_itab__1105[] = {
  /*  0 */           0,        1374,
};

static const uint16_t ud_itab__1106[] = {
  /*  0 */           0,        1375,
};

static const uint16_t ud_itab__1107[] = {
  /*  0 */           0,        1376,
};

static const uint16_t ud_itab__1108[] = {
  /*  0 */           0,        1377,
};

static const uint16_t ud_itab__1109[] = {
  /*  0 */           0,        1378,
};

static const uint16_t ud_itab__1110[] = {
  /*  0 */           0,        1379,
};

static const uint16_t ud_itab__1111[] = {
  /*  0 */           0,        1380,
};

static const uint16_t ud_itab__1112[] = {
  /*  0 */           0,        1381,
};

static const uint16_t ud_itab__1113[] = {
  /*  0 */           0,        1382,
};

static const uint16_t ud_itab__1114[] = {
  /*  0 */           0,        1383,
};

static const uint16_t ud_itab__1115[] = {
  /*  0 */           0,        1384,
};

static const uint16_t ud_itab__1116[] = {
  /*  0 */           0,        1385,
};

static const uint16_t ud_itab__1117[] = {
  /*  0 */           0,        1386,
};

static const uint16_t ud_itab__1118[] = {
  /*  0 */           0,        1387,
};

static const uint16_t ud_itab__1119[] = {
  /*  0 */           0,        1388,
};

static const uint16_t ud_itab__1120[] = {
  /*  0 */           0,        1389,
};

static const uint16_t ud_itab__1121[] = {
  /*  0 */           0,        1390,
};

static const uint16_t ud_itab__1122[] = {
  /*  0 */           0,        1391,
};

static const uint16_t ud_itab__1123[] = {
  /*  0 */           0,        1392,
};

static const uint16_t ud_itab__1124[] = {
  /*  0 */           0,        1393,
};

static const uint16_t ud_itab__1125[] = {
  /*  0 */           0,        1394,
};

static const uint16_t ud_itab__1076[] = {
  /*  0 */ GROUP(1077), GROUP(1078), GROUP(1079), GROUP(1080),
  /*  4 */ GROUP(1081), GROUP(1082), GROUP(1083), GROUP(1084),
  /*  8 */ GROUP(1085), GROUP(1086), GROUP(1087), GROUP(1088),
  /*  c */ GROUP(1089), GROUP(1090), GROUP(1091), GROUP(1092),
  /* 10 */ GROUP(1093), GROUP(1094), GROUP(1095), GROUP(1096),
  /* 14 */ GROUP(1097), GROUP(1098), GROUP(1099), GROUP(1100),
  /* 18 */ GROUP(1101), GROUP(1102), GROUP(1103), GROUP(1104),
  /* 1c */ GROUP(1105), GROUP(1106), GROUP(1107), GROUP(1108),
  /* 20 */ GROUP(1109),           0,           0,           0,
  /* 24 */           0,           0,           0,           0,
  /* 28 */ GROUP(1110), GROUP(1111), GROUP(1112), GROUP(1113),
  /* 2c */ GROUP(1114), GROUP(1115), GROUP(1116), GROUP(1117),
  /* 30 */ GROUP(1118), GROUP(1119), GROUP(1120), GROUP(1121),
  /* 34 */ GROUP(1122), GROUP(1123), GROUP(1124), GROUP(1125),
  /* 38 */           0,           0,           0,           0,
  /* 3c */           0,           0,           0,           0,
};

static const uint16_t ud_itab__1066[] = {
  /*  0 */ GROUP(1067), GROUP(1076),
};

static const uint16_t ud_itab__1126[] = {
  /*  0 */        1398,        1399,        1400,
};

static const uint16_t ud_itab__1127[] = {
  /*  0 */        1407,           0,
};

static const uint16_t ud_itab__1128[] = {
  /*  0 */        1419,        1420,        1421,        1422,
  /*  4 */        1423,        1424,        1425,        1426,
};

static const uint16_t ud_itab__1129[] = {
  /*  0 */        1427,        1428,        1429,        1430,
  /*  4 */        1431,        1432,        1433,        1434,
};

static const uint16_t ud_itab__1130[] = {
  /*  0 */        1441,        1442,           0,           0,
  /*  4 */           0,           0,           0,           0,
};

static const uint16_t ud_itab__1132[] = {
  /*  0 */        1445,        1446,
};

static const uint16_t ud_itab__1131[] = {
  /*  0 */        1443,        1444, GROUP(1132),        1447,
  /*  4 */        1448,        1449,        1450,           0,
};

const uint16_t ud_itab__0[] = {
  /*  0 */           1,           2,           3,           4,
  /*  4 */           5,           6,    GROUP(1),    GROUP(2),
  /*  8 */           9,          10,          11,          12,
  /*  c */          13,          14,    GROUP(3),    GROUP(4),
  /* 10 */         628,         629,         630,         631,
  /* 14 */         632,         633,  GROUP(563),  GROUP(564),
  /* 18 */         636,         637,         638,         639,
  /* 1c */         640,         641,  GROUP(565),  GROUP(566),
  /* 20 */         644,         645,         646,         647,
  /* 24 */         648,         649,           0,  GROUP(567),
  /* 28 */         651,         652,         653,         654,
  /* 2c */         655,         656,           0,  GROUP(568),
  /* 30 */         658,         659,         660,         661,
  /* 34 */         662,         663,           0,  GROUP(569),
  /* 38 */         665,         666,         667,         668,
  /* 3c */         669,         670,           0,  GROUP(570),
  /* 40 */         672,         673,         674,         675,
  /* 44 */         676,         677,         678,         679,
  /* 48 */         680,         681,         682,         683,
  /* 4c */         684,         685,         686,         687,
  /* 50 */         688,         689,         690,         691,
  /* 54 */         692,         693,         694,         695,
  /* 58 */         696,         697,         698,         699,
  /* 5c */         700,         701,         702,         703,
  /* 60 */  GROUP(571),  GROUP(574),  GROUP(577),  GROUP(578),
  /* 64 */           0,           0,           0,           0,
  /* 68 */         711,         712,         713,         714,
  /* 6c */         715,  GROUP(579),         718,  GROUP(580),
  /* 70 */         721,         722,         723,         724,
  /* 74 */         725,         726,         727,         728,
  /* 78 */         729,         730,         731,         732,
  /* 7c */         733,         734,         735,         736,
  /* 80 */  GROUP(581),  GROUP(582),  GROUP(583),  GROUP(592),
  /* 84 */         769,         770,         771,         772,
  /* 88 */         773,         774,         775,         776,
  /* 8c */         777,         778,         779,  GROUP(593),
  /* 90 */         781,         782,         783,         784,
  /* 94 */         785,         786,         787,         788,
  /* 98 */  GROUP(594),  GROUP(595),  GROUP(596),         796,
  /* 9c */  GROUP(597),  GROUP(601),         806,         807,
  /* a0 */         808,         809,         810,         811,
  /* a4 */         812,  GROUP(605),         816,  GROUP(606),
  /* a8 */         820,         821,         822,  GROUP(607),
  /* ac */         826,  GROUP(608),         830,  GROUP(609),
  /* b0 */         834,         835,         836,         837,
  /* b4 */         838,         839,         840,         841,
  /* b8 */         842,         843,         844,         845,
  /* bc */         846,         847,         848,         849,
  /* c0 */  GROUP(610),  GROUP(611),         866,         867,
  /* c4 */  GROUP(612),  GROUP(613),  GROUP(614),  GROUP(615),
  /* c8 */         872,         873,         874,         875,
  /* cc */         876,         877,  GROUP(616),  GROUP(617),
  /* d0 */  GROUP(618),  GROUP(619),  GROUP(620),  GROUP(621),
  /* d4 */  GROUP(622),  GROUP(623),  GROUP(624),         917,
  /* d8 */  GROUP(625),  GROUP(700),  GROUP(762),  GROUP(806),
  /* dc */  GROUP(865),  GROUP(940),  GROUP(998), GROUP(1066),
  /* e0 */        1395,        1396,        1397, GROUP(1126),
  /* e4 */        1401,        1402,        1403,        1404,
  /* e8 */        1405,        1406, GROUP(1127),        1408,
  /* ec */        1409,        1410,        1411,        1412,
  /* f0 */        1413,        1414,        1415,        1416,
  /* f4 */        1417,        1418, GROUP(1128), GROUP(1129),
  /* f8 */        1435,        1436,        1437,        1438,
  /* fc */        1439,        1440, GROUP(1130), GROUP(1131),
};


struct ud_lookup_table_list_entry ud_lookup_table_list[] = {
    /* 000 */ { ud_itab__0, UD_TAB__OPC_TABLE, "table0" },
    /* 001 */ { ud_itab__1, UD_TAB__OPC_MODE, "/m" },
    /* 002 */ { ud_itab__2, UD_TAB__OPC_MODE, "/m" },
    /* 003 */ { ud_itab__3, UD_TAB__OPC_MODE, "/m" },
    /* 004 */ { ud_itab__4, UD_TAB__OPC_TABLE, "0f" },
    /* 005 */ { ud_itab__5, UD_TAB__OPC_REG, "/reg" },
    /* 006 */ { ud_itab__6, UD_TAB__OPC_SSE, "/sse" },
    /* 007 */ { ud_itab__7, UD_TAB__OPC_SSE, "/sse" },
    /* 008 */ { ud_itab__8, UD_TAB__OPC_SSE, "/sse" },
    /* 009 */ { ud_itab__9, UD_TAB__OPC_SSE, "/sse" },
    /* 010 */ { ud_itab__10, UD_TAB__OPC_SSE, "/sse" },
    /* 011 */ { ud_itab__11, UD_TAB__OPC_SSE, "/sse" },
    /* 012 */ { ud_itab__12, UD_TAB__OPC_MOD, "/mod" },
    /* 013 */ { ud_itab__13, UD_TAB__OPC_REG, "/reg" },
    /* 014 */ { ud_itab__14, UD_TAB__OPC_SSE, "/sse" },
    /* 015 */ { ud_itab__15, UD_TAB__OPC_MOD, "/mod" },
    /* 016 */ { ud_itab__16, UD_TAB__OPC_SSE, "/sse" },
    /* 017 */ { ud_itab__17, UD_TAB__OPC_MOD, "/mod" },
    /* 018 */ { ud_itab__18, UD_TAB__OPC_SSE, "/sse" },
    /* 019 */ { ud_itab__19, UD_TAB__OPC_MOD, "/mod" },
    /* 020 */ { ud_itab__20, UD_TAB__OPC_SSE, "/sse" },
    /* 021 */ { ud_itab__21, UD_TAB__OPC_MOD, "/mod" },
    /* 022 */ { ud_itab__22, UD_TAB__OPC_SSE, "/sse" },
    /* 023 */ { ud_itab__23, UD_TAB__OPC_MOD, "/mod" },
    /* 024 */ { ud_itab__24, UD_TAB__OPC_SSE, "/sse" },
    /* 025 */ { ud_itab__25, UD_TAB__OPC_MOD, "/mod" },
    /* 026 */ { ud_itab__26, UD_TAB__OPC_SSE, "/sse" },
    /* 027 */ { ud_itab__27, UD_TAB__OPC_MOD, "/mod" },
    /* 028 */ { ud_itab__28, UD_TAB__OPC_REG, "/reg" },
    /* 029 */ { ud_itab__29, UD_TAB__OPC_RM, "/rm" },
    /* 030 */ { ud_itab__30, UD_TAB__OPC_SSE, "/sse" },
    /* 031 */ { ud_itab__31, UD_TAB__OPC_MOD, "/mod" },
    /* 032 */ { ud_itab__32, UD_TAB__OPC_VENDOR, "intel" },
    /* 033 */ { ud_itab__33, UD_TAB__OPC_SSE, "/sse" },
    /* 034 */ { ud_itab__34, UD_TAB__OPC_MOD, "/mod" },
    /* 035 */ { ud_itab__35, UD_TAB__OPC_VENDOR, "intel" },
    /* 036 */ { ud_itab__36, UD_TAB__OPC_SSE, "/sse" },
    /* 037 */ { ud_itab__37, UD_TAB__OPC_MOD, "/mod" },
    /* 038 */ { ud_itab__38, UD_TAB__OPC_VENDOR, "intel" },
    /* 039 */ { ud_itab__39, UD_TAB__OPC_SSE, "/sse" },
    /* 040 */ { ud_itab__40, UD_TAB__OPC_MOD, "/mod" },
    /* 041 */ { ud_itab__41, UD_TAB__OPC_VENDOR, "intel" },
    /* 042 */ { ud_itab__42, UD_TAB__OPC_RM, "/rm" },
    /* 043 */ { ud_itab__43, UD_TAB__OPC_SSE, "/sse" },
    /* 044 */ { ud_itab__44, UD_TAB__OPC_MOD, "/mod" },
    /* 045 */ { ud_itab__45, UD_TAB__OPC_SSE, "/sse" },
    /* 046 */ { ud_itab__46, UD_TAB__OPC_MOD, "/mod" },
    /* 047 */ { ud_itab__47, UD_TAB__OPC_RM, "/rm" },
    /* 048 */ { ud_itab__48, UD_TAB__OPC_SSE, "/sse" },
    /* 049 */ { ud_itab__49, UD_TAB__OPC_MOD, "/mod" },
    /* 050 */ { ud_itab__50, UD_TAB__OPC_SSE, "/sse" },
    /* 051 */ { ud_itab__51, UD_TAB__OPC_MOD, "/mod" },
    /* 052 */ { ud_itab__52, UD_TAB__OPC_RM, "/rm" },
    /* 053 */ { ud_itab__53, UD_TAB__OPC_SSE, "/sse" },
    /* 054 */ { ud_itab__54, UD_TAB__OPC_MOD, "/mod" },
    /* 055 */ { ud_itab__55, UD_TAB__OPC_VENDOR, "amd" },
    /* 056 */ { ud_itab__56, UD_TAB__OPC_SSE, "/sse" },
    /* 057 */ { ud_itab__57, UD_TAB__OPC_MOD, "/mod" },
    /* 058 */ { ud_itab__58, UD_TAB__OPC_VENDOR, "amd" },
    /* 059 */ { ud_itab__59, UD_TAB__OPC_SSE, "/sse" },
    /* 060 */ { ud_itab__60, UD_TAB__OPC_MOD, "/mod" },
    /* 061 */ { ud_itab__61, UD_TAB__OPC_VENDOR, "amd" },
    /* 062 */ { ud_itab__62, UD_TAB__OPC_SSE, "/sse" },
    /* 063 */ { ud_itab__63, UD_TAB__OPC_MOD, "/mod" },
    /* 064 */ { ud_itab__64, UD_TAB__OPC_VENDOR, "amd" },
    /* 065 */ { ud_itab__65, UD_TAB__OPC_SSE, "/sse" },
    /* 066 */ { ud_itab__66, UD_TAB__OPC_MOD, "/mod" },
    /* 067 */ { ud_itab__67, UD_TAB__OPC_VENDOR, "amd" },
    /* 068 */ { ud_itab__68, UD_TAB__OPC_SSE, "/sse" },
    /* 069 */ { ud_itab__69, UD_TAB__OPC_MOD, "/mod" },
    /* 070 */ { ud_itab__70, UD_TAB__OPC_VENDOR, "amd" },
    /* 071 */ { ud_itab__71, UD_TAB__OPC_SSE, "/sse" },
    /* 072 */ { ud_itab__72, UD_TAB__OPC_MOD, "/mod" },
    /* 073 */ { ud_itab__73, UD_TAB__OPC_VENDOR, "amd" },
    /* 074 */ { ud_itab__74, UD_TAB__OPC_SSE, "/sse" },
    /* 075 */ { ud_itab__75, UD_TAB__OPC_MOD, "/mod" },
    /* 076 */ { ud_itab__76, UD_TAB__OPC_VENDOR, "amd" },
    /* 077 */ { ud_itab__77, UD_TAB__OPC_SSE, "/sse" },
    /* 078 */ { ud_itab__78, UD_TAB__OPC_MOD, "/mod" },
    /* 079 */ { ud_itab__79, UD_TAB__OPC_SSE, "/sse" },
    /* 080 */ { ud_itab__80, UD_TAB__OPC_MOD, "/mod" },
    /* 081 */ { ud_itab__81, UD_TAB__OPC_RM, "/rm" },
    /* 082 */ { ud_itab__82, UD_TAB__OPC_SSE, "/sse" },
    /* 083 */ { ud_itab__83, UD_TAB__OPC_MOD, "/mod" },
    /* 084 */ { ud_itab__84, UD_TAB__OPC_SSE, "/sse" },
    /* 085 */ { ud_itab__85, UD_TAB__OPC_MOD, "/mod" },
    /* 086 */ { ud_itab__86, UD_TAB__OPC_VENDOR, "amd" },
    /* 087 */ { ud_itab__87, UD_TAB__OPC_SSE, "/sse" },
    /* 088 */ { ud_itab__88, UD_TAB__OPC_SSE, "/sse" },
    /* 089 */ { ud_itab__89, UD_TAB__OPC_SSE, "/sse" },
    /* 090 */ { ud_itab__90, UD_TAB__OPC_SSE, "/sse" },
    /* 091 */ { ud_itab__91, UD_TAB__OPC_SSE, "/sse" },
    /* 092 */ { ud_itab__92, UD_TAB__OPC_SSE, "/sse" },
    /* 093 */ { ud_itab__93, UD_TAB__OPC_SSE, "/sse" },
    /* 094 */ { ud_itab__94, UD_TAB__OPC_SSE, "/sse" },
    /* 095 */ { ud_itab__95, UD_TAB__OPC_REG, "/reg" },
    /* 096 */ { ud_itab__96, UD_TAB__OPC_SSE, "/sse" },
    /* 097 */ { ud_itab__97, UD_TAB__OPC_SSE, "/sse" },
    /* 098 */ { ud_itab__98, UD_TAB__OPC_SSE, "/sse" },
    /* 099 */ { ud_itab__99, UD_TAB__OPC_SSE, "/sse" },
    /* 100 */ { ud_itab__100, UD_TAB__OPC_SSE, "/sse" },
    /* 101 */ { ud_itab__101, UD_TAB__OPC_SSE, "/sse" },
    /* 102 */ { ud_itab__102, UD_TAB__OPC_SSE, "/sse" },
    /* 103 */ { ud_itab__103, UD_TAB__OPC_SSE, "/sse" },
    /* 104 */ { ud_itab__104, UD_TAB__OPC_SSE, "/sse" },
    /* 105 */ { ud_itab__105, UD_TAB__OPC_3DNOW, "/3dnow" },
    /* 106 */ { ud_itab__106, UD_TAB__OPC_SSE, "/sse" },
    /* 107 */ { ud_itab__107, UD_TAB__OPC_SSE, "/sse" },
    /* 108 */ { ud_itab__108, UD_TAB__OPC_MOD, "/mod" },
    /* 109 */ { ud_itab__109, UD_TAB__OPC_SSE, "/sse" },
    /* 110 */ { ud_itab__110, UD_TAB__OPC_MOD, "/mod" },
    /* 111 */ { ud_itab__111, UD_TAB__OPC_MOD, "/mod" },
    /* 112 */ { ud_itab__112, UD_TAB__OPC_MOD, "/mod" },
    /* 113 */ { ud_itab__113, UD_TAB__OPC_MOD, "/mod" },
    /* 114 */ { ud_itab__114, UD_TAB__OPC_SSE, "/sse" },
    /* 115 */ { ud_itab__115, UD_TAB__OPC_MOD, "/mod" },
    /* 116 */ { ud_itab__116, UD_TAB__OPC_MOD, "/mod" },
    /* 117 */ { ud_itab__117, UD_TAB__OPC_MOD, "/mod" },
    /* 118 */ { ud_itab__118, UD_TAB__OPC_SSE, "/sse" },
    /* 119 */ { ud_itab__119, UD_TAB__OPC_SSE, "/sse" },
    /* 120 */ { ud_itab__120, UD_TAB__OPC_SSE, "/sse" },
    /* 121 */ { ud_itab__121, UD_TAB__OPC_MOD, "/mod" },
    /* 122 */ { ud_itab__122, UD_TAB__OPC_SSE, "/sse" },
    /* 123 */ { ud_itab__123, UD_TAB__OPC_MOD, "/mod" },
    /* 124 */ { ud_itab__124, UD_TAB__OPC_MOD, "/mod" },
    /* 125 */ { ud_itab__125, UD_TAB__OPC_MOD, "/mod" },
    /* 126 */ { ud_itab__126, UD_TAB__OPC_SSE, "/sse" },
    /* 127 */ { ud_itab__127, UD_TAB__OPC_MOD, "/mod" },
    /* 128 */ { ud_itab__128, UD_TAB__OPC_MOD, "/mod" },
    /* 129 */ { ud_itab__129, UD_TAB__OPC_SSE, "/sse" },
    /* 130 */ { ud_itab__130, UD_TAB__OPC_REG, "/reg" },
    /* 131 */ { ud_itab__131, UD_TAB__OPC_SSE, "/sse" },
    /* 132 */ { ud_itab__132, UD_TAB__OPC_SSE, "/sse" },
    /* 133 */ { ud_itab__133, UD_TAB__OPC_SSE, "/sse" },
    /* 134 */ { ud_itab__134, UD_TAB__OPC_SSE, "/sse" },
    /* 135 */ { ud_itab__135, UD_TAB__OPC_SSE, "/sse" },
    /* 136 */ { ud_itab__136, UD_TAB__OPC_SSE, "/sse" },
    /* 137 */ { ud_itab__137, UD_TAB__OPC_SSE, "/sse" },
    /* 138 */ { ud_itab__138, UD_TAB__OPC_SSE, "/sse" },
    /* 139 */ { ud_itab__139, UD_TAB__OPC_SSE, "/sse" },
    /* 140 */ { ud_itab__140, UD_TAB__OPC_SSE, "/sse" },
    /* 141 */ { ud_itab__141, UD_TAB__OPC_SSE, "/sse" },
    /* 142 */ { ud_itab__142, UD_TAB__OPC_SSE, "/sse" },
    /* 143 */ { ud_itab__143, UD_TAB__OPC_SSE, "/sse" },
    /* 144 */ { ud_itab__144, UD_TAB__OPC_SSE, "/sse" },
    /* 145 */ { ud_itab__145, UD_TAB__OPC_SSE, "/sse" },
    /* 146 */ { ud_itab__146, UD_TAB__OPC_SSE, "/sse" },
    /* 147 */ { ud_itab__147, UD_TAB__OPC_SSE, "/sse" },
    /* 148 */ { ud_itab__148, UD_TAB__OPC_SSE, "/sse" },
    /* 149 */ { ud_itab__149, UD_TAB__OPC_SSE, "/sse" },
    /* 150 */ { ud_itab__150, UD_TAB__OPC_SSE, "/sse" },
    /* 151 */ { ud_itab__151, UD_TAB__OPC_SSE, "/sse" },
    /* 152 */ { ud_itab__152, UD_TAB__OPC_SSE, "/sse" },
    /* 153 */ { ud_itab__153, UD_TAB__OPC_SSE, "/sse" },
    /* 154 */ { ud_itab__154, UD_TAB__OPC_SSE, "/sse" },
    /* 155 */ { ud_itab__155, UD_TAB__OPC_SSE, "/sse" },
    /* 156 */ { ud_itab__156, UD_TAB__OPC_SSE, "/sse" },
    /* 157 */ { ud_itab__157, UD_TAB__OPC_SSE, "/sse" },
    /* 158 */ { ud_itab__158, UD_TAB__OPC_SSE, "/sse" },
    /* 159 */ { ud_itab__159, UD_TAB__OPC_MODE, "/m" },
    /* 160 */ { ud_itab__160, UD_TAB__OPC_VENDOR, "intel" },
    /* 161 */ { ud_itab__161, UD_TAB__OPC_SSE, "/sse" },
    /* 162 */ { ud_itab__162, UD_TAB__OPC_MODE, "/m" },
    /* 163 */ { ud_itab__163, UD_TAB__OPC_VENDOR, "intel" },
    /* 164 */ { ud_itab__164, UD_TAB__OPC_SSE, "/sse" },
    /* 165 */ { ud_itab__165, UD_TAB__OPC_TABLE, "38" },
    /* 166 */ { ud_itab__166, UD_TAB__OPC_SSE, "/sse" },
    /* 167 */ { ud_itab__167, UD_TAB__OPC_SSE, "/sse" },
    /* 168 */ { ud_itab__168, UD_TAB__OPC_SSE, "/sse" },
    /* 169 */ { ud_itab__169, UD_TAB__OPC_SSE, "/sse" },
    /* 170 */ { ud_itab__170, UD_TAB__OPC_SSE, "/sse" },
    /* 171 */ { ud_itab__171, UD_TAB__OPC_SSE, "/sse" },
    /* 172 */ { ud_itab__172, UD_TAB__OPC_SSE, "/sse" },
    /* 173 */ { ud_itab__173, UD_TAB__OPC_SSE, "/sse" },
    /* 174 */ { ud_itab__174, UD_TAB__OPC_SSE, "/sse" },
    /* 175 */ { ud_itab__175, UD_TAB__OPC_SSE, "/sse" },
    /* 176 */ { ud_itab__176, UD_TAB__OPC_SSE, "/sse" },
    /* 177 */ { ud_itab__177, UD_TAB__OPC_SSE, "/sse" },
    /* 178 */ { ud_itab__178, UD_TAB__OPC_SSE, "/sse" },
    /* 179 */ { ud_itab__179, UD_TAB__OPC_SSE, "/sse" },
    /* 180 */ { ud_itab__180, UD_TAB__OPC_SSE, "/sse" },
    /* 181 */ { ud_itab__181, UD_TAB__OPC_SSE, "/sse" },
    /* 182 */ { ud_itab__182, UD_TAB__OPC_SSE, "/sse" },
    /* 183 */ { ud_itab__183, UD_TAB__OPC_SSE, "/sse" },
    /* 184 */ { ud_itab__184, UD_TAB__OPC_SSE, "/sse" },
    /* 185 */ { ud_itab__185, UD_TAB__OPC_SSE, "/sse" },
    /* 186 */ { ud_itab__186, UD_TAB__OPC_SSE, "/sse" },
    /* 187 */ { ud_itab__187, UD_TAB__OPC_SSE, "/sse" },
    /* 188 */ { ud_itab__188, UD_TAB__OPC_SSE, "/sse" },
    /* 189 */ { ud_itab__189, UD_TAB__OPC_SSE, "/sse" },
    /* 190 */ { ud_itab__190, UD_TAB__OPC_SSE, "/sse" },
    /* 191 */ { ud_itab__191, UD_TAB__OPC_SSE, "/sse" },
    /* 192 */ { ud_itab__192, UD_TAB__OPC_SSE, "/sse" },
    /* 193 */ { ud_itab__193, UD_TAB__OPC_SSE, "/sse" },
    /* 194 */ { ud_itab__194, UD_TAB__OPC_SSE, "/sse" },
    /* 195 */ { ud_itab__195, UD_TAB__OPC_SSE, "/sse" },
    /* 196 */ { ud_itab__196, UD_TAB__OPC_SSE, "/sse" },
    /* 197 */ { ud_itab__197, UD_TAB__OPC_SSE, "/sse" },
    /* 198 */ { ud_itab__198, UD_TAB__OPC_SSE, "/sse" },
    /* 199 */ { ud_itab__199, UD_TAB__OPC_SSE, "/sse" },
    /* 200 */ { ud_itab__200, UD_TAB__OPC_SSE, "/sse" },
    /* 201 */ { ud_itab__201, UD_TAB__OPC_SSE, "/sse" },
    /* 202 */ { ud_itab__202, UD_TAB__OPC_SSE, "/sse" },
    /* 203 */ { ud_itab__203, UD_TAB__OPC_SSE, "/sse" },
    /* 204 */ { ud_itab__204, UD_TAB__OPC_SSE, "/sse" },
    /* 205 */ { ud_itab__205, UD_TAB__OPC_SSE, "/sse" },
    /* 206 */ { ud_itab__206, UD_TAB__OPC_SSE, "/sse" },
    /* 207 */ { ud_itab__207, UD_TAB__OPC_SSE, "/sse" },
    /* 208 */ { ud_itab__208, UD_TAB__OPC_SSE, "/sse" },
    /* 209 */ { ud_itab__209, UD_TAB__OPC_SSE, "/sse" },
    /* 210 */ { ud_itab__210, UD_TAB__OPC_SSE, "/sse" },
    /* 211 */ { ud_itab__211, UD_TAB__OPC_SSE, "/sse" },
    /* 212 */ { ud_itab__212, UD_TAB__OPC_SSE, "/sse" },
    /* 213 */ { ud_itab__213, UD_TAB__OPC_MODE, "/m" },
    /* 214 */ { ud_itab__214, UD_TAB__OPC_VENDOR, "intel" },
    /* 215 */ { ud_itab__215, UD_TAB__OPC_SSE, "/sse" },
    /* 216 */ { ud_itab__216, UD_TAB__OPC_MODE, "/m" },
    /* 217 */ { ud_itab__217, UD_TAB__OPC_VENDOR, "intel" },
    /* 218 */ { ud_itab__218, UD_TAB__OPC_SSE, "/sse" },
    /* 219 */ { ud_itab__219, UD_TAB__OPC_SSE, "/sse" },
    /* 220 */ { ud_itab__220, UD_TAB__OPC_SSE, "/sse" },
    /* 221 */ { ud_itab__221, UD_TAB__OPC_SSE, "/sse" },
    /* 222 */ { ud_itab__222, UD_TAB__OPC_SSE, "/sse" },
    /* 223 */ { ud_itab__223, UD_TAB__OPC_SSE, "/sse" },
    /* 224 */ { ud_itab__224, UD_TAB__OPC_SSE, "/sse" },
    /* 225 */ { ud_itab__225, UD_TAB__OPC_TABLE, "3a" },
    /* 226 */ { ud_itab__226, UD_TAB__OPC_SSE, "/sse" },
    /* 227 */ { ud_itab__227, UD_TAB__OPC_SSE, "/sse" },
    /* 228 */ { ud_itab__228, UD_TAB__OPC_SSE, "/sse" },
    /* 229 */ { ud_itab__229, UD_TAB__OPC_SSE, "/sse" },
    /* 230 */ { ud_itab__230, UD_TAB__OPC_SSE, "/sse" },
    /* 231 */ { ud_itab__231, UD_TAB__OPC_SSE, "/sse" },
    /* 232 */ { ud_itab__232, UD_TAB__OPC_SSE, "/sse" },
    /* 233 */ { ud_itab__233, UD_TAB__OPC_SSE, "/sse" },
    /* 234 */ { ud_itab__234, UD_TAB__OPC_SSE, "/sse" },
    /* 235 */ { ud_itab__235, UD_TAB__OPC_SSE, "/sse" },
    /* 236 */ { ud_itab__236, UD_TAB__OPC_SSE, "/sse" },
    /* 237 */ { ud_itab__237, UD_TAB__OPC_OSIZE, "/o" },
    /* 238 */ { ud_itab__238, UD_TAB__OPC_SSE, "/sse" },
    /* 239 */ { ud_itab__239, UD_TAB__OPC_SSE, "/sse" },
    /* 240 */ { ud_itab__240, UD_TAB__OPC_SSE, "/sse" },
    /* 241 */ { ud_itab__241, UD_TAB__OPC_SSE, "/sse" },
    /* 242 */ { ud_itab__242, UD_TAB__OPC_OSIZE, "/o" },
    /* 243 */ { ud_itab__243, UD_TAB__OPC_SSE, "/sse" },
    /* 244 */ { ud_itab__244, UD_TAB__OPC_SSE, "/sse" },
    /* 245 */ { ud_itab__245, UD_TAB__OPC_SSE, "/sse" },
    /* 246 */ { ud_itab__246, UD_TAB__OPC_SSE, "/sse" },
    /* 247 */ { ud_itab__247, UD_TAB__OPC_SSE, "/sse" },
    /* 248 */ { ud_itab__248, UD_TAB__OPC_SSE, "/sse" },
    /* 249 */ { ud_itab__249, UD_TAB__OPC_SSE, "/sse" },
    /* 250 */ { ud_itab__250, UD_TAB__OPC_SSE, "/sse" },
    /* 251 */ { ud_itab__251, UD_TAB__OPC_SSE, "/sse" },
    /* 252 */ { ud_itab__252, UD_TAB__OPC_SSE, "/sse" },
    /* 253 */ { ud_itab__253, UD_TAB__OPC_SSE, "/sse" },
    /* 254 */ { ud_itab__254, UD_TAB__OPC_SSE, "/sse" },
    /* 255 */ { ud_itab__255, UD_TAB__OPC_SSE, "/sse" },
    /* 256 */ { ud_itab__256, UD_TAB__OPC_SSE, "/sse" },
    /* 257 */ { ud_itab__257, UD_TAB__OPC_SSE, "/sse" },
    /* 258 */ { ud_itab__258, UD_TAB__OPC_SSE, "/sse" },
    /* 259 */ { ud_itab__259, UD_TAB__OPC_SSE, "/sse" },
    /* 260 */ { ud_itab__260, UD_TAB__OPC_SSE, "/sse" },
    /* 261 */ { ud_itab__261, UD_TAB__OPC_SSE, "/sse" },
    /* 262 */ { ud_itab__262, UD_TAB__OPC_SSE, "/sse" },
    /* 263 */ { ud_itab__263, UD_TAB__OPC_SSE, "/sse" },
    /* 264 */ { ud_itab__264, UD_TAB__OPC_SSE, "/sse" },
    /* 265 */ { ud_itab__265, UD_TAB__OPC_SSE, "/sse" },
    /* 266 */ { ud_itab__266, UD_TAB__OPC_SSE, "/sse" },
    /* 267 */ { ud_itab__267, UD_TAB__OPC_SSE, "/sse" },
    /* 268 */ { ud_itab__268, UD_TAB__OPC_SSE, "/sse" },
    /* 269 */ { ud_itab__269, UD_TAB__OPC_SSE, "/sse" },
    /* 270 */ { ud_itab__270, UD_TAB__OPC_SSE, "/sse" },
    /* 271 */ { ud_itab__271, UD_TAB__OPC_SSE, "/sse" },
    /* 272 */ { ud_itab__272, UD_TAB__OPC_SSE, "/sse" },
    /* 273 */ { ud_itab__273, UD_TAB__OPC_SSE, "/sse" },
    /* 274 */ { ud_itab__274, UD_TAB__OPC_SSE, "/sse" },
    /* 275 */ { ud_itab__275, UD_TAB__OPC_SSE, "/sse" },
    /* 276 */ { ud_itab__276, UD_TAB__OPC_SSE, "/sse" },
    /* 277 */ { ud_itab__277, UD_TAB__OPC_SSE, "/sse" },
    /* 278 */ { ud_itab__278, UD_TAB__OPC_SSE, "/sse" },
    /* 279 */ { ud_itab__279, UD_TAB__OPC_SSE, "/sse" },
    /* 280 */ { ud_itab__280, UD_TAB__OPC_SSE, "/sse" },
    /* 281 */ { ud_itab__281, UD_TAB__OPC_SSE, "/sse" },
    /* 282 */ { ud_itab__282, UD_TAB__OPC_SSE, "/sse" },
    /* 283 */ { ud_itab__283, UD_TAB__OPC_SSE, "/sse" },
    /* 284 */ { ud_itab__284, UD_TAB__OPC_SSE, "/sse" },
    /* 285 */ { ud_itab__285, UD_TAB__OPC_SSE, "/sse" },
    /* 286 */ { ud_itab__286, UD_TAB__OPC_SSE, "/sse" },
    /* 287 */ { ud_itab__287, UD_TAB__OPC_SSE, "/sse" },
    /* 288 */ { ud_itab__288, UD_TAB__OPC_SSE, "/sse" },
    /* 289 */ { ud_itab__289, UD_TAB__OPC_SSE, "/sse" },
    /* 290 */ { ud_itab__290, UD_TAB__OPC_SSE, "/sse" },
    /* 291 */ { ud_itab__291, UD_TAB__OPC_SSE, "/sse" },
    /* 292 */ { ud_itab__292, UD_TAB__OPC_SSE, "/sse" },
    /* 293 */ { ud_itab__293, UD_TAB__OPC_SSE, "/sse" },
    /* 294 */ { ud_itab__294, UD_TAB__OPC_SSE, "/sse" },
    /* 295 */ { ud_itab__295, UD_TAB__OPC_SSE, "/sse" },
    /* 296 */ { ud_itab__296, UD_TAB__OPC_SSE, "/sse" },
    /* 297 */ { ud_itab__297, UD_TAB__OPC_SSE, "/sse" },
    /* 298 */ { ud_itab__298, UD_TAB__OPC_SSE, "/sse" },
    /* 299 */ { ud_itab__299, UD_TAB__OPC_SSE, "/sse" },
    /* 300 */ { ud_itab__300, UD_TAB__OPC_SSE, "/sse" },
    /* 301 */ { ud_itab__301, UD_TAB__OPC_REG, "/reg" },
    /* 302 */ { ud_itab__302, UD_TAB__OPC_SSE, "/sse" },
    /* 303 */ { ud_itab__303, UD_TAB__OPC_SSE, "/sse" },
    /* 304 */ { ud_itab__304, UD_TAB__OPC_SSE, "/sse" },
    /* 305 */ { ud_itab__305, UD_TAB__OPC_REG, "/reg" },
    /* 306 */ { ud_itab__306, UD_TAB__OPC_SSE, "/sse" },
    /* 307 */ { ud_itab__307, UD_TAB__OPC_SSE, "/sse" },
    /* 308 */ { ud_itab__308, UD_TAB__OPC_SSE, "/sse" },
    /* 309 */ { ud_itab__309, UD_TAB__OPC_REG, "/reg" },
    /* 310 */ { ud_itab__310, UD_TAB__OPC_SSE, "/sse" },
    /* 311 */ { ud_itab__311, UD_TAB__OPC_SSE, "/sse" },
    /* 312 */ { ud_itab__312, UD_TAB__OPC_SSE, "/sse" },
    /* 313 */ { ud_itab__313, UD_TAB__OPC_SSE, "/sse" },
    /* 314 */ { ud_itab__314, UD_TAB__OPC_SSE, "/sse" },
    /* 315 */ { ud_itab__315, UD_TAB__OPC_SSE, "/sse" },
    /* 316 */ { ud_itab__316, UD_TAB__OPC_SSE, "/sse" },
    /* 317 */ { ud_itab__317, UD_TAB__OPC_SSE, "/sse" },
    /* 318 */ { ud_itab__318, UD_TAB__OPC_SSE, "/sse" },
    /* 319 */ { ud_itab__319, UD_TAB__OPC_VENDOR, "intel" },
    /* 320 */ { ud_itab__320, UD_TAB__OPC_SSE, "/sse" },
    /* 321 */ { ud_itab__321, UD_TAB__OPC_VENDOR, "intel" },
    /* 322 */ { ud_itab__322, UD_TAB__OPC_SSE, "/sse" },
    /* 323 */ { ud_itab__323, UD_TAB__OPC_SSE, "/sse" },
    /* 324 */ { ud_itab__324, UD_TAB__OPC_SSE, "/sse" },
    /* 325 */ { ud_itab__325, UD_TAB__OPC_SSE, "/sse" },
    /* 326 */ { ud_itab__326, UD_TAB__OPC_SSE, "/sse" },
    /* 327 */ { ud_itab__327, UD_TAB__OPC_SSE, "/sse" },
    /* 328 */ { ud_itab__328, UD_TAB__OPC_SSE, "/sse" },
    /* 329 */ { ud_itab__329, UD_TAB__OPC_SSE, "/sse" },
    /* 330 */ { ud_itab__330, UD_TAB__OPC_SSE, "/sse" },
    /* 331 */ { ud_itab__331, UD_TAB__OPC_SSE, "/sse" },
    /* 332 */ { ud_itab__332, UD_TAB__OPC_SSE, "/sse" },
    /* 333 */ { ud_itab__333, UD_TAB__OPC_SSE, "/sse" },
    /* 334 */ { ud_itab__334, UD_TAB__OPC_SSE, "/sse" },
    /* 335 */ { ud_itab__335, UD_TAB__OPC_SSE, "/sse" },
    /* 336 */ { ud_itab__336, UD_TAB__OPC_SSE, "/sse" },
    /* 337 */ { ud_itab__337, UD_TAB__OPC_SSE, "/sse" },
    /* 338 */ { ud_itab__338, UD_TAB__OPC_SSE, "/sse" },
    /* 339 */ { ud_itab__339, UD_TAB__OPC_SSE, "/sse" },
    /* 340 */ { ud_itab__340, UD_TAB__OPC_SSE, "/sse" },
    /* 341 */ { ud_itab__341, UD_TAB__OPC_SSE, "/sse" },
    /* 342 */ { ud_itab__342, UD_TAB__OPC_SSE, "/sse" },
    /* 343 */ { ud_itab__343, UD_TAB__OPC_SSE, "/sse" },
    /* 344 */ { ud_itab__344, UD_TAB__OPC_SSE, "/sse" },
    /* 345 */ { ud_itab__345, UD_TAB__OPC_SSE, "/sse" },
    /* 346 */ { ud_itab__346, UD_TAB__OPC_SSE, "/sse" },
    /* 347 */ { ud_itab__347, UD_TAB__OPC_SSE, "/sse" },
    /* 348 */ { ud_itab__348, UD_TAB__OPC_SSE, "/sse" },
    /* 349 */ { ud_itab__349, UD_TAB__OPC_SSE, "/sse" },
    /* 350 */ { ud_itab__350, UD_TAB__OPC_SSE, "/sse" },
    /* 351 */ { ud_itab__351, UD_TAB__OPC_SSE, "/sse" },
    /* 352 */ { ud_itab__352, UD_TAB__OPC_SSE, "/sse" },
    /* 353 */ { ud_itab__353, UD_TAB__OPC_SSE, "/sse" },
    /* 354 */ { ud_itab__354, UD_TAB__OPC_SSE, "/sse" },
    /* 355 */ { ud_itab__355, UD_TAB__OPC_SSE, "/sse" },
    /* 356 */ { ud_itab__356, UD_TAB__OPC_SSE, "/sse" },
    /* 357 */ { ud_itab__357, UD_TAB__OPC_SSE, "/sse" },
    /* 358 */ { ud_itab__358, UD_TAB__OPC_SSE, "/sse" },
    /* 359 */ { ud_itab__359, UD_TAB__OPC_SSE, "/sse" },
    /* 360 */ { ud_itab__360, UD_TAB__OPC_SSE, "/sse" },
    /* 361 */ { ud_itab__361, UD_TAB__OPC_SSE, "/sse" },
    /* 362 */ { ud_itab__362, UD_TAB__OPC_SSE, "/sse" },
    /* 363 */ { ud_itab__363, UD_TAB__OPC_SSE, "/sse" },
    /* 364 */ { ud_itab__364, UD_TAB__OPC_MOD, "/mod" },
    /* 365 */ { ud_itab__365, UD_TAB__OPC_REG, "/reg" },
    /* 366 */ { ud_itab__366, UD_TAB__OPC_RM, "/rm" },
    /* 367 */ { ud_itab__367, UD_TAB__OPC_SSE, "/sse" },
    /* 368 */ { ud_itab__368, UD_TAB__OPC_MOD, "/mod" },
    /* 369 */ { ud_itab__369, UD_TAB__OPC_RM, "/rm" },
    /* 370 */ { ud_itab__370, UD_TAB__OPC_SSE, "/sse" },
    /* 371 */ { ud_itab__371, UD_TAB__OPC_MOD, "/mod" },
    /* 372 */ { ud_itab__372, UD_TAB__OPC_RM, "/rm" },
    /* 373 */ { ud_itab__373, UD_TAB__OPC_SSE, "/sse" },
    /* 374 */ { ud_itab__374, UD_TAB__OPC_MOD, "/mod" },
    /* 375 */ { ud_itab__375, UD_TAB__OPC_MOD, "/mod" },
    /* 376 */ { ud_itab__376, UD_TAB__OPC_REG, "/reg" },
    /* 377 */ { ud_itab__377, UD_TAB__OPC_RM, "/rm" },
    /* 378 */ { ud_itab__378, UD_TAB__OPC_SSE, "/sse" },
    /* 379 */ { ud_itab__379, UD_TAB__OPC_MOD, "/mod" },
    /* 380 */ { ud_itab__380, UD_TAB__OPC_RM, "/rm" },
    /* 381 */ { ud_itab__381, UD_TAB__OPC_SSE, "/sse" },
    /* 382 */ { ud_itab__382, UD_TAB__OPC_MOD, "/mod" },
    /* 383 */ { ud_itab__383, UD_TAB__OPC_RM, "/rm" },
    /* 384 */ { ud_itab__384, UD_TAB__OPC_SSE, "/sse" },
    /* 385 */ { ud_itab__385, UD_TAB__OPC_MOD, "/mod" },
    /* 386 */ { ud_itab__386, UD_TAB__OPC_RM, "/rm" },
    /* 387 */ { ud_itab__387, UD_TAB__OPC_SSE, "/sse" },
    /* 388 */ { ud_itab__388, UD_TAB__OPC_MOD, "/mod" },
    /* 389 */ { ud_itab__389, UD_TAB__OPC_RM, "/rm" },
    /* 390 */ { ud_itab__390, UD_TAB__OPC_SSE, "/sse" },
    /* 391 */ { ud_itab__391, UD_TAB__OPC_MOD, "/mod" },
    /* 392 */ { ud_itab__392, UD_TAB__OPC_RM, "/rm" },
    /* 393 */ { ud_itab__393, UD_TAB__OPC_SSE, "/sse" },
    /* 394 */ { ud_itab__394, UD_TAB__OPC_MOD, "/mod" },
    /* 395 */ { ud_itab__395, UD_TAB__OPC_SSE, "/sse" },
    /* 396 */ { ud_itab__396, UD_TAB__OPC_SSE, "/sse" },
    /* 397 */ { ud_itab__397, UD_TAB__OPC_SSE, "/sse" },
    /* 398 */ { ud_itab__398, UD_TAB__OPC_SSE, "/sse" },
    /* 399 */ { ud_itab__399, UD_TAB__OPC_SSE, "/sse" },
    /* 400 */ { ud_itab__400, UD_TAB__OPC_SSE, "/sse" },
    /* 401 */ { ud_itab__401, UD_TAB__OPC_MOD, "/mod" },
    /* 402 */ { ud_itab__402, UD_TAB__OPC_REG, "/reg" },
    /* 403 */ { ud_itab__403, UD_TAB__OPC_SSE, "/sse" },
    /* 404 */ { ud_itab__404, UD_TAB__OPC_MOD, "/mod" },
    /* 405 */ { ud_itab__405, UD_TAB__OPC_SSE, "/sse" },
    /* 406 */ { ud_itab__406, UD_TAB__OPC_MOD, "/mod" },
    /* 407 */ { ud_itab__407, UD_TAB__OPC_SSE, "/sse" },
    /* 408 */ { ud_itab__408, UD_TAB__OPC_MOD, "/mod" },
    /* 409 */ { ud_itab__409, UD_TAB__OPC_SSE, "/sse" },
    /* 410 */ { ud_itab__410, UD_TAB__OPC_MOD, "/mod" },
    /* 411 */ { ud_itab__411, UD_TAB__OPC_SSE, "/sse" },
    /* 412 */ { ud_itab__412, UD_TAB__OPC_MOD, "/mod" },
    /* 413 */ { ud_itab__413, UD_TAB__OPC_SSE, "/sse" },
    /* 414 */ { ud_itab__414, UD_TAB__OPC_MOD, "/mod" },
    /* 415 */ { ud_itab__415, UD_TAB__OPC_SSE, "/sse" },
    /* 416 */ { ud_itab__416, UD_TAB__OPC_MOD, "/mod" },
    /* 417 */ { ud_itab__417, UD_TAB__OPC_REG, "/reg" },
    /* 418 */ { ud_itab__418, UD_TAB__OPC_RM, "/rm" },
    /* 419 */ { ud_itab__419, UD_TAB__OPC_SSE, "/sse" },
    /* 420 */ { ud_itab__420, UD_TAB__OPC_MOD, "/mod" },
    /* 421 */ { ud_itab__421, UD_TAB__OPC_SSE, "/sse" },
    /* 422 */ { ud_itab__422, UD_TAB__OPC_MOD, "/mod" },
    /* 423 */ { ud_itab__423, UD_TAB__OPC_SSE, "/sse" },
    /* 424 */ { ud_itab__424, UD_TAB__OPC_MOD, "/mod" },
    /* 425 */ { ud_itab__425, UD_TAB__OPC_SSE, "/sse" },
    /* 426 */ { ud_itab__426, UD_TAB__OPC_MOD, "/mod" },
    /* 427 */ { ud_itab__427, UD_TAB__OPC_SSE, "/sse" },
    /* 428 */ { ud_itab__428, UD_TAB__OPC_MOD, "/mod" },
    /* 429 */ { ud_itab__429, UD_TAB__OPC_SSE, "/sse" },
    /* 430 */ { ud_itab__430, UD_TAB__OPC_MOD, "/mod" },
    /* 431 */ { ud_itab__431, UD_TAB__OPC_SSE, "/sse" },
    /* 432 */ { ud_itab__432, UD_TAB__OPC_MOD, "/mod" },
    /* 433 */ { ud_itab__433, UD_TAB__OPC_SSE, "/sse" },
    /* 434 */ { ud_itab__434, UD_TAB__OPC_MOD, "/mod" },
    /* 435 */ { ud_itab__435, UD_TAB__OPC_RM, "/rm" },
    /* 436 */ { ud_itab__436, UD_TAB__OPC_SSE, "/sse" },
    /* 437 */ { ud_itab__437, UD_TAB__OPC_MOD, "/mod" },
    /* 438 */ { ud_itab__438, UD_TAB__OPC_SSE, "/sse" },
    /* 439 */ { ud_itab__439, UD_TAB__OPC_MOD, "/mod" },
    /* 440 */ { ud_itab__440, UD_TAB__OPC_SSE, "/sse" },
    /* 441 */ { ud_itab__441, UD_TAB__OPC_MOD, "/mod" },
    /* 442 */ { ud_itab__442, UD_TAB__OPC_SSE, "/sse" },
    /* 443 */ { ud_itab__443, UD_TAB__OPC_MOD, "/mod" },
    /* 444 */ { ud_itab__444, UD_TAB__OPC_SSE, "/sse" },
    /* 445 */ { ud_itab__445, UD_TAB__OPC_MOD, "/mod" },
    /* 446 */ { ud_itab__446, UD_TAB__OPC_SSE, "/sse" },
    /* 447 */ { ud_itab__447, UD_TAB__OPC_MOD, "/mod" },
    /* 448 */ { ud_itab__448, UD_TAB__OPC_SSE, "/sse" },
    /* 449 */ { ud_itab__449, UD_TAB__OPC_MOD, "/mod" },
    /* 450 */ { ud_itab__450, UD_TAB__OPC_SSE, "/sse" },
    /* 451 */ { ud_itab__451, UD_TAB__OPC_MOD, "/mod" },
    /* 452 */ { ud_itab__452, UD_TAB__OPC_RM, "/rm" },
    /* 453 */ { ud_itab__453, UD_TAB__OPC_SSE, "/sse" },
    /* 454 */ { ud_itab__454, UD_TAB__OPC_MOD, "/mod" },
    /* 455 */ { ud_itab__455, UD_TAB__OPC_SSE, "/sse" },
    /* 456 */ { ud_itab__456, UD_TAB__OPC_MOD, "/mod" },
    /* 457 */ { ud_itab__457, UD_TAB__OPC_SSE, "/sse" },
    /* 458 */ { ud_itab__458, UD_TAB__OPC_MOD, "/mod" },
    /* 459 */ { ud_itab__459, UD_TAB__OPC_SSE, "/sse" },
    /* 460 */ { ud_itab__460, UD_TAB__OPC_MOD, "/mod" },
    /* 461 */ { ud_itab__461, UD_TAB__OPC_SSE, "/sse" },
    /* 462 */ { ud_itab__462, UD_TAB__OPC_MOD, "/mod" },
    /* 463 */ { ud_itab__463, UD_TAB__OPC_SSE, "/sse" },
    /* 464 */ { ud_itab__464, UD_TAB__OPC_MOD, "/mod" },
    /* 465 */ { ud_itab__465, UD_TAB__OPC_SSE, "/sse" },
    /* 466 */ { ud_itab__466, UD_TAB__OPC_MOD, "/mod" },
    /* 467 */ { ud_itab__467, UD_TAB__OPC_SSE, "/sse" },
    /* 468 */ { ud_itab__468, UD_TAB__OPC_MOD, "/mod" },
    /* 469 */ { ud_itab__469, UD_TAB__OPC_SSE, "/sse" },
    /* 470 */ { ud_itab__470, UD_TAB__OPC_SSE, "/sse" },
    /* 471 */ { ud_itab__471, UD_TAB__OPC_SSE, "/sse" },
    /* 472 */ { ud_itab__472, UD_TAB__OPC_SSE, "/sse" },
    /* 473 */ { ud_itab__473, UD_TAB__OPC_SSE, "/sse" },
    /* 474 */ { ud_itab__474, UD_TAB__OPC_SSE, "/sse" },
    /* 475 */ { ud_itab__475, UD_TAB__OPC_SSE, "/sse" },
    /* 476 */ { ud_itab__476, UD_TAB__OPC_SSE, "/sse" },
    /* 477 */ { ud_itab__477, UD_TAB__OPC_SSE, "/sse" },
    /* 478 */ { ud_itab__478, UD_TAB__OPC_SSE, "/sse" },
    /* 479 */ { ud_itab__479, UD_TAB__OPC_REG, "/reg" },
    /* 480 */ { ud_itab__480, UD_TAB__OPC_SSE, "/sse" },
    /* 481 */ { ud_itab__481, UD_TAB__OPC_SSE, "/sse" },
    /* 482 */ { ud_itab__482, UD_TAB__OPC_SSE, "/sse" },
    /* 483 */ { ud_itab__483, UD_TAB__OPC_SSE, "/sse" },
    /* 484 */ { ud_itab__484, UD_TAB__OPC_SSE, "/sse" },
    /* 485 */ { ud_itab__485, UD_TAB__OPC_SSE, "/sse" },
    /* 486 */ { ud_itab__486, UD_TAB__OPC_SSE, "/sse" },
    /* 487 */ { ud_itab__487, UD_TAB__OPC_SSE, "/sse" },
    /* 488 */ { ud_itab__488, UD_TAB__OPC_SSE, "/sse" },
    /* 489 */ { ud_itab__489, UD_TAB__OPC_SSE, "/sse" },
    /* 490 */ { ud_itab__490, UD_TAB__OPC_SSE, "/sse" },
    /* 491 */ { ud_itab__491, UD_TAB__OPC_SSE, "/sse" },
    /* 492 */ { ud_itab__492, UD_TAB__OPC_SSE, "/sse" },
    /* 493 */ { ud_itab__493, UD_TAB__OPC_SSE, "/sse" },
    /* 494 */ { ud_itab__494, UD_TAB__OPC_SSE, "/sse" },
    /* 495 */ { ud_itab__495, UD_TAB__OPC_SSE, "/sse" },
    /* 496 */ { ud_itab__496, UD_TAB__OPC_REG, "/reg" },
    /* 497 */ { ud_itab__497, UD_TAB__OPC_SSE, "/sse" },
    /* 498 */ { ud_itab__498, UD_TAB__OPC_OSIZE, "/o" },
    /* 499 */ { ud_itab__499, UD_TAB__OPC_SSE, "/sse" },
    /* 500 */ { ud_itab__500, UD_TAB__OPC_VENDOR, "intel" },
    /* 501 */ { ud_itab__501, UD_TAB__OPC_VENDOR, "intel" },
    /* 502 */ { ud_itab__502, UD_TAB__OPC_VENDOR, "intel" },
    /* 503 */ { ud_itab__503, UD_TAB__OPC_SSE, "/sse" },
    /* 504 */ { ud_itab__504, UD_TAB__OPC_VENDOR, "intel" },
    /* 505 */ { ud_itab__505, UD_TAB__OPC_SSE, "/sse" },
    /* 506 */ { ud_itab__506, UD_TAB__OPC_SSE, "/sse" },
    /* 507 */ { ud_itab__507, UD_TAB__OPC_SSE, "/sse" },
    /* 508 */ { ud_itab__508, UD_TAB__OPC_SSE, "/sse" },
    /* 509 */ { ud_itab__509, UD_TAB__OPC_SSE, "/sse" },
    /* 510 */ { ud_itab__510, UD_TAB__OPC_SSE, "/sse" },
    /* 511 */ { ud_itab__511, UD_TAB__OPC_SSE, "/sse" },
    /* 512 */ { ud_itab__512, UD_TAB__OPC_SSE, "/sse" },
    /* 513 */ { ud_itab__513, UD_TAB__OPC_SSE, "/sse" },
    /* 514 */ { ud_itab__514, UD_TAB__OPC_SSE, "/sse" },
    /* 515 */ { ud_itab__515, UD_TAB__OPC_SSE, "/sse" },
    /* 516 */ { ud_itab__516, UD_TAB__OPC_SSE, "/sse" },
    /* 517 */ { ud_itab__517, UD_TAB__OPC_SSE, "/sse" },
    /* 518 */ { ud_itab__518, UD_TAB__OPC_SSE, "/sse" },
    /* 519 */ { ud_itab__519, UD_TAB__OPC_SSE, "/sse" },
    /* 520 */ { ud_itab__520, UD_TAB__OPC_SSE, "/sse" },
    /* 521 */ { ud_itab__521, UD_TAB__OPC_SSE, "/sse" },
    /* 522 */ { ud_itab__522, UD_TAB__OPC_SSE, "/sse" },
    /* 523 */ { ud_itab__523, UD_TAB__OPC_SSE, "/sse" },
    /* 524 */ { ud_itab__524, UD_TAB__OPC_SSE, "/sse" },
    /* 525 */ { ud_itab__525, UD_TAB__OPC_SSE, "/sse" },
    /* 526 */ { ud_itab__526, UD_TAB__OPC_SSE, "/sse" },
    /* 527 */ { ud_itab__527, UD_TAB__OPC_SSE, "/sse" },
    /* 528 */ { ud_itab__528, UD_TAB__OPC_SSE, "/sse" },
    /* 529 */ { ud_itab__529, UD_TAB__OPC_SSE, "/sse" },
    /* 530 */ { ud_itab__530, UD_TAB__OPC_SSE, "/sse" },
    /* 531 */ { ud_itab__531, UD_TAB__OPC_SSE, "/sse" },
    /* 532 */ { ud_itab__532, UD_TAB__OPC_SSE, "/sse" },
    /* 533 */ { ud_itab__533, UD_TAB__OPC_SSE, "/sse" },
    /* 534 */ { ud_itab__534, UD_TAB__OPC_SSE, "/sse" },
    /* 535 */ { ud_itab__535, UD_TAB__OPC_SSE, "/sse" },
    /* 536 */ { ud_itab__536, UD_TAB__OPC_SSE, "/sse" },
    /* 537 */ { ud_itab__537, UD_TAB__OPC_SSE, "/sse" },
    /* 538 */ { ud_itab__538, UD_TAB__OPC_SSE, "/sse" },
    /* 539 */ { ud_itab__539, UD_TAB__OPC_SSE, "/sse" },
    /* 540 */ { ud_itab__540, UD_TAB__OPC_SSE, "/sse" },
    /* 541 */ { ud_itab__541, UD_TAB__OPC_SSE, "/sse" },
    /* 542 */ { ud_itab__542, UD_TAB__OPC_SSE, "/sse" },
    /* 543 */ { ud_itab__543, UD_TAB__OPC_SSE, "/sse" },
    /* 544 */ { ud_itab__544, UD_TAB__OPC_SSE, "/sse" },
    /* 545 */ { ud_itab__545, UD_TAB__OPC_SSE, "/sse" },
    /* 546 */ { ud_itab__546, UD_TAB__OPC_SSE, "/sse" },
    /* 547 */ { ud_itab__547, UD_TAB__OPC_SSE, "/sse" },
    /* 548 */ { ud_itab__548, UD_TAB__OPC_SSE, "/sse" },
    /* 549 */ { ud_itab__549, UD_TAB__OPC_SSE, "/sse" },
    /* 550 */ { ud_itab__550, UD_TAB__OPC_SSE, "/sse" },
    /* 551 */ { ud_itab__551, UD_TAB__OPC_SSE, "/sse" },
    /* 552 */ { ud_itab__552, UD_TAB__OPC_MOD, "/mod" },
    /* 553 */ { ud_itab__553, UD_TAB__OPC_SSE, "/sse" },
    /* 554 */ { ud_itab__554, UD_TAB__OPC_MOD, "/mod" },
    /* 555 */ { ud_itab__555, UD_TAB__OPC_MOD, "/mod" },
    /* 556 */ { ud_itab__556, UD_TAB__OPC_SSE, "/sse" },
    /* 557 */ { ud_itab__557, UD_TAB__OPC_SSE, "/sse" },
    /* 558 */ { ud_itab__558, UD_TAB__OPC_SSE, "/sse" },
    /* 559 */ { ud_itab__559, UD_TAB__OPC_SSE, "/sse" },
    /* 560 */ { ud_itab__560, UD_TAB__OPC_SSE, "/sse" },
    /* 561 */ { ud_itab__561, UD_TAB__OPC_SSE, "/sse" },
    /* 562 */ { ud_itab__562, UD_TAB__OPC_SSE, "/sse" },
    /* 563 */ { ud_itab__563, UD_TAB__OPC_MODE, "/m" },
    /* 564 */ { ud_itab__564, UD_TAB__OPC_MODE, "/m" },
    /* 565 */ { ud_itab__565, UD_TAB__OPC_MODE, "/m" },
    /* 566 */ { ud_itab__566, UD_TAB__OPC_MODE, "/m" },
    /* 567 */ { ud_itab__567, UD_TAB__OPC_MODE, "/m" },
    /* 568 */ { ud_itab__568, UD_TAB__OPC_MODE, "/m" },
    /* 569 */ { ud_itab__569, UD_TAB__OPC_MODE, "/m" },
    /* 570 */ { ud_itab__570, UD_TAB__OPC_MODE, "/m" },
    /* 571 */ { ud_itab__571, UD_TAB__OPC_OSIZE, "/o" },
    /* 572 */ { ud_itab__572, UD_TAB__OPC_MODE, "/m" },
    /* 573 */ { ud_itab__573, UD_TAB__OPC_MODE, "/m" },
    /* 574 */ { ud_itab__574, UD_TAB__OPC_OSIZE, "/o" },
    /* 575 */ { ud_itab__575, UD_TAB__OPC_MODE, "/m" },
    /* 576 */ { ud_itab__576, UD_TAB__OPC_MODE, "/m" },
    /* 577 */ { ud_itab__577, UD_TAB__OPC_MODE, "/m" },
    /* 578 */ { ud_itab__578, UD_TAB__OPC_MODE, "/m" },
    /* 579 */ { ud_itab__579, UD_TAB__OPC_OSIZE, "/o" },
    /* 580 */ { ud_itab__580, UD_TAB__OPC_OSIZE, "/o" },
    /* 581 */ { ud_itab__581, UD_TAB__OPC_REG, "/reg" },
    /* 582 */ { ud_itab__582, UD_TAB__OPC_REG, "/reg" },
    /* 583 */ { ud_itab__583, UD_TAB__OPC_REG, "/reg" },
    /* 584 */ { ud_itab__584, UD_TAB__OPC_MODE, "/m" },
    /* 585 */ { ud_itab__585, UD_TAB__OPC_MODE, "/m" },
    /* 586 */ { ud_itab__586, UD_TAB__OPC_MODE, "/m" },
    /* 587 */ { ud_itab__587, UD_TAB__OPC_MODE, "/m" },
    /* 588 */ { ud_itab__588, UD_TAB__OPC_MODE, "/m" },
    /* 589 */ { ud_itab__589, UD_TAB__OPC_MODE, "/m" },
    /* 590 */ { ud_itab__590, UD_TAB__OPC_MODE, "/m" },
    /* 591 */ { ud_itab__591, UD_TAB__OPC_MODE, "/m" },
    /* 592 */ { ud_itab__592, UD_TAB__OPC_REG, "/reg" },
    /* 593 */ { ud_itab__593, UD_TAB__OPC_REG, "/reg" },
    /* 594 */ { ud_itab__594, UD_TAB__OPC_OSIZE, "/o" },
    /* 595 */ { ud_itab__595, UD_TAB__OPC_OSIZE, "/o" },
    /* 596 */ { ud_itab__596, UD_TAB__OPC_MODE, "/m" },
    /* 597 */ { ud_itab__597, UD_TAB__OPC_OSIZE, "/o" },
    /* 598 */ { ud_itab__598, UD_TAB__OPC_MODE, "/m" },
    /* 599 */ { ud_itab__599, UD_TAB__OPC_MODE, "/m" },
    /* 600 */ { ud_itab__600, UD_TAB__OPC_MODE, "/m" },
    /* 601 */ { ud_itab__601, UD_TAB__OPC_OSIZE, "/o" },
    /* 602 */ { ud_itab__602, UD_TAB__OPC_MODE, "/m" },
    /* 603 */ { ud_itab__603, UD_TAB__OPC_MODE, "/m" },
    /* 604 */ { ud_itab__604, UD_TAB__OPC_MODE, "/m" },
    /* 605 */ { ud_itab__605, UD_TAB__OPC_OSIZE, "/o" },
    /* 606 */ { ud_itab__606, UD_TAB__OPC_OSIZE, "/o" },
    /* 607 */ { ud_itab__607, UD_TAB__OPC_OSIZE, "/o" },
    /* 608 */ { ud_itab__608, UD_TAB__OPC_OSIZE, "/o" },
    /* 609 */ { ud_itab__609, UD_TAB__OPC_OSIZE, "/o" },
    /* 610 */ { ud_itab__610, UD_TAB__OPC_REG, "/reg" },
    /* 611 */ { ud_itab__611, UD_TAB__OPC_REG, "/reg" },
    /* 612 */ { ud_itab__612, UD_TAB__OPC_MODE, "/m" },
    /* 613 */ { ud_itab__613, UD_TAB__OPC_MODE, "/m" },
    /* 614 */ { ud_itab__614, UD_TAB__OPC_REG, "/reg" },
    /* 615 */ { ud_itab__615, UD_TAB__OPC_REG, "/reg" },
    /* 616 */ { ud_itab__616, UD_TAB__OPC_MODE, "/m" },
    /* 617 */ { ud_itab__617, UD_TAB__OPC_OSIZE, "/o" },
    /* 618 */ { ud_itab__618, UD_TAB__OPC_REG, "/reg" },
    /* 619 */ { ud_itab__619, UD_TAB__OPC_REG, "/reg" },
    /* 620 */ { ud_itab__620, UD_TAB__OPC_REG, "/reg" },
    /* 621 */ { ud_itab__621, UD_TAB__OPC_REG, "/reg" },
    /* 622 */ { ud_itab__622, UD_TAB__OPC_MODE, "/m" },
    /* 623 */ { ud_itab__623, UD_TAB__OPC_MODE, "/m" },
    /* 624 */ { ud_itab__624, UD_TAB__OPC_MODE, "/m" },
    /* 625 */ { ud_itab__625, UD_TAB__OPC_MOD, "/mod" },
    /* 626 */ { ud_itab__626, UD_TAB__OPC_REG, "/reg" },
    /* 627 */ { ud_itab__627, UD_TAB__OPC_MOD, "/mod" },
    /* 628 */ { ud_itab__628, UD_TAB__OPC_MOD, "/mod" },
    /* 629 */ { ud_itab__629, UD_TAB__OPC_MOD, "/mod" },
    /* 630 */ { ud_itab__630, UD_TAB__OPC_MOD, "/mod" },
    /* 631 */ { ud_itab__631, UD_TAB__OPC_MOD, "/mod" },
    /* 632 */ { ud_itab__632, UD_TAB__OPC_MOD, "/mod" },
    /* 633 */ { ud_itab__633, UD_TAB__OPC_MOD, "/mod" },
    /* 634 */ { ud_itab__634, UD_TAB__OPC_MOD, "/mod" },
    /* 635 */ { ud_itab__635, UD_TAB__OPC_X87, "/x87" },
    /* 636 */ { ud_itab__636, UD_TAB__OPC_MOD, "/mod" },
    /* 637 */ { ud_itab__637, UD_TAB__OPC_MOD, "/mod" },
    /* 638 */ { ud_itab__638, UD_TAB__OPC_MOD, "/mod" },
    /* 639 */ { ud_itab__639, UD_TAB__OPC_MOD, "/mod" },
    /* 640 */ { ud_itab__640, UD_TAB__OPC_MOD, "/mod" },
    /* 641 */ { ud_itab__641, UD_TAB__OPC_MOD, "/mod" },
    /* 642 */ { ud_itab__642, UD_TAB__OPC_MOD, "/mod" },
    /* 643 */ { ud_itab__643, UD_TAB__OPC_MOD, "/mod" },
    /* 644 */ { ud_itab__644, UD_TAB__OPC_MOD, "/mod" },
    /* 645 */ { ud_itab__645, UD_TAB__OPC_MOD, "/mod" },
    /* 646 */ { ud_itab__646, UD_TAB__OPC_MOD, "/mod" },
    /* 647 */ { ud_itab__647, UD_TAB__OPC_MOD, "/mod" },
    /* 648 */ { ud_itab__648, UD_TAB__OPC_MOD, "/mod" },
    /* 649 */ { ud_itab__649, UD_TAB__OPC_MOD, "/mod" },
    /* 650 */ { ud_itab__650, UD_TAB__OPC_MOD, "/mod" },
    /* 651 */ { ud_itab__651, UD_TAB__OPC_MOD, "/mod" },
    /* 652 */ { ud_itab__652, UD_TAB__OPC_MOD, "/mod" },
    /* 653 */ { ud_itab__653, UD_TAB__OPC_MOD, "/mod" },
    /* 654 */ { ud_itab__654, UD_TAB__OPC_MOD, "/mod" },
    /* 655 */ { ud_itab__655, UD_TAB__OPC_MOD, "/mod" },
    /* 656 */ { ud_itab__656, UD_TAB__OPC_MOD, "/mod" },
    /* 657 */ { ud_itab__657, UD_TAB__OPC_MOD, "/mod" },
    /* 658 */ { ud_itab__658, UD_TAB__OPC_MOD, "/mod" },
    /* 659 */ { ud_itab__659, UD_TAB__OPC_MOD, "/mod" },
    /* 660 */ { ud_itab__660, UD_TAB__OPC_MOD, "/mod" },
    /* 661 */ { ud_itab__661, UD_TAB__OPC_MOD, "/mod" },
    /* 662 */ { ud_itab__662, UD_TAB__OPC_MOD, "/mod" },
    /* 663 */ { ud_itab__663, UD_TAB__OPC_MOD, "/mod" },
    /* 664 */ { ud_itab__664, UD_TAB__OPC_MOD, "/mod" },
    /* 665 */ { ud_itab__665, UD_TAB__OPC_MOD, "/mod" },
    /* 666 */ { ud_itab__666, UD_TAB__OPC_MOD, "/mod" },
    /* 667 */ { ud_itab__667, UD_TAB__OPC_MOD, "/mod" },
    /* 668 */ { ud_itab__668, UD_TAB__OPC_MOD, "/mod" },
    /* 669 */ { ud_itab__669, UD_TAB__OPC_MOD, "/mod" },
    /* 670 */ { ud_itab__670, UD_TAB__OPC_MOD, "/mod" },
    /* 671 */ { ud_itab__671, UD_TAB__OPC_MOD, "/mod" },
    /* 672 */ { ud_itab__672, UD_TAB__OPC_MOD, "/mod" },
    /* 673 */ { ud_itab__673, UD_TAB__OPC_MOD, "/mod" },
    /* 674 */ { ud_itab__674, UD_TAB__OPC_MOD, "/mod" },
    /* 675 */ { ud_itab__675, UD_TAB__OPC_MOD, "/mod" },
    /* 676 */ { ud_itab__676, UD_TAB__OPC_MOD, "/mod" },
    /* 677 */ { ud_itab__677, UD_TAB__OPC_MOD, "/mod" },
    /* 678 */ { ud_itab__678, UD_TAB__OPC_MOD, "/mod" },
    /* 679 */ { ud_itab__679, UD_TAB__OPC_MOD, "/mod" },
    /* 680 */ { ud_itab__680, UD_TAB__OPC_MOD, "/mod" },
    /* 681 */ { ud_itab__681, UD_TAB__OPC_MOD, "/mod" },
    /* 682 */ { ud_itab__682, UD_TAB__OPC_MOD, "/mod" },
    /* 683 */ { ud_itab__683, UD_TAB__OPC_MOD, "/mod" },
    /* 684 */ { ud_itab__684, UD_TAB__OPC_MOD, "/mod" },
    /* 685 */ { ud_itab__685, UD_TAB__OPC_MOD, "/mod" },
    /* 686 */ { ud_itab__686, UD_TAB__OPC_MOD, "/mod" },
    /* 687 */ { ud_itab__687, UD_TAB__OPC_MOD, "/mod" },
    /* 688 */ { ud_itab__688, UD_TAB__OPC_MOD, "/mod" },
    /* 689 */ { ud_itab__689, UD_TAB__OPC_MOD, "/mod" },
    /* 690 */ { ud_itab__690, UD_TAB__OPC_MOD, "/mod" },
    /* 691 */ { ud_itab__691, UD_TAB__OPC_MOD, "/mod" },
    /* 692 */ { ud_itab__692, UD_TAB__OPC_MOD, "/mod" },
    /* 693 */ { ud_itab__693, UD_TAB__OPC_MOD, "/mod" },
    /* 694 */ { ud_itab__694, UD_TAB__OPC_MOD, "/mod" },
    /* 695 */ { ud_itab__695, UD_TAB__OPC_MOD, "/mod" },
    /* 696 */ { ud_itab__696, UD_TAB__OPC_MOD, "/mod" },
    /* 697 */ { ud_itab__697, UD_TAB__OPC_MOD, "/mod" },
    /* 698 */ { ud_itab__698, UD_TAB__OPC_MOD, "/mod" },
    /* 699 */ { ud_itab__699, UD_TAB__OPC_MOD, "/mod" },
    /* 700 */ { ud_itab__700, UD_TAB__OPC_MOD, "/mod" },
    /* 701 */ { ud_itab__701, UD_TAB__OPC_REG, "/reg" },
    /* 702 */ { ud_itab__702, UD_TAB__OPC_MOD, "/mod" },
    /* 703 */ { ud_itab__703, UD_TAB__OPC_MOD, "/mod" },
    /* 704 */ { ud_itab__704, UD_TAB__OPC_MOD, "/mod" },
    /* 705 */ { ud_itab__705, UD_TAB__OPC_MOD, "/mod" },
    /* 706 */ { ud_itab__706, UD_TAB__OPC_MOD, "/mod" },
    /* 707 */ { ud_itab__707, UD_TAB__OPC_MOD, "/mod" },
    /* 708 */ { ud_itab__708, UD_TAB__OPC_MOD, "/mod" },
    /* 709 */ { ud_itab__709, UD_TAB__OPC_X87, "/x87" },
    /* 710 */ { ud_itab__710, UD_TAB__OPC_MOD, "/mod" },
    /* 711 */ { ud_itab__711, UD_TAB__OPC_MOD, "/mod" },
    /* 712 */ { ud_itab__712, UD_TAB__OPC_MOD, "/mod" },
    /* 713 */ { ud_itab__713, UD_TAB__OPC_MOD, "/mod" },
    /* 714 */ { ud_itab__714, UD_TAB__OPC_MOD, "/mod" },
    /* 715 */ { ud_itab__715, UD_TAB__OPC_MOD, "/mod" },
    /* 716 */ { ud_itab__716, UD_TAB__OPC_MOD, "/mod" },
    /* 717 */ { ud_itab__717, UD_TAB__OPC_MOD, "/mod" },
    /* 718 */ { ud_itab__718, UD_TAB__OPC_MOD, "/mod" },
    /* 719 */ { ud_itab__719, UD_TAB__OPC_MOD, "/mod" },
    /* 720 */ { ud_itab__720, UD_TAB__OPC_MOD, "/mod" },
    /* 721 */ { ud_itab__721, UD_TAB__OPC_MOD, "/mod" },
    /* 722 */ { ud_itab__722, UD_TAB__OPC_MOD, "/mod" },
    /* 723 */ { ud_itab__723, UD_TAB__OPC_MOD, "/mod" },
    /* 724 */ { ud_itab__724, UD_TAB__OPC_MOD, "/mod" },
    /* 725 */ { ud_itab__725, UD_TAB__OPC_MOD, "/mod" },
    /* 726 */ { ud_itab__726, UD_TAB__OPC_MOD, "/mod" },
    /* 727 */ { ud_itab__727, UD_TAB__OPC_MOD, "/mod" },
    /* 728 */ { ud_itab__728, UD_TAB__OPC_MOD, "/mod" },
    /* 729 */ { ud_itab__729, UD_TAB__OPC_MOD, "/mod" },
    /* 730 */ { ud_itab__730, UD_TAB__OPC_MOD, "/mod" },
    /* 731 */ { ud_itab__731, UD_TAB__OPC_MOD, "/mod" },
    /* 732 */ { ud_itab__732, UD_TAB__OPC_MOD, "/mod" },
    /* 733 */ { ud_itab__733, UD_TAB__OPC_MOD, "/mod" },
    /* 734 */ { ud_itab__734, UD_TAB__OPC_MOD, "/mod" },
    /* 735 */ { ud_itab__735, UD_TAB__OPC_MOD, "/mod" },
    /* 736 */ { ud_itab__736, UD_TAB__OPC_MOD, "/mod" },
    /* 737 */ { ud_itab__737, UD_TAB__OPC_MOD, "/mod" },
    /* 738 */ { ud_itab__738, UD_TAB__OPC_MOD, "/mod" },
    /* 739 */ { ud_itab__739, UD_TAB__OPC_MOD, "/mod" },
    /* 740 */ { ud_itab__740, UD_TAB__OPC_MOD, "/mod" },
    /* 741 */ { ud_itab__741, UD_TAB__OPC_MOD, "/mod" },
    /* 742 */ { ud_itab__742, UD_TAB__OPC_MOD, "/mod" },
    /* 743 */ { ud_itab__743, UD_TAB__OPC_MOD, "/mod" },
    /* 744 */ { ud_itab__744, UD_TAB__OPC_MOD, "/mod" },
    /* 745 */ { ud_itab__745, UD_TAB__OPC_MOD, "/mod" },
    /* 746 */ { ud_itab__746, UD_TAB__OPC_MOD, "/mod" },
    /* 747 */ { ud_itab__747, UD_TAB__OPC_MOD, "/mod" },
    /* 748 */ { ud_itab__748, UD_TAB__OPC_MOD, "/mod" },
    /* 749 */ { ud_itab__749, UD_TAB__OPC_MOD, "/mod" },
    /* 750 */ { ud_itab__750, UD_TAB__OPC_MOD, "/mod" },
    /* 751 */ { ud_itab__751, UD_TAB__OPC_MOD, "/mod" },
    /* 752 */ { ud_itab__752, UD_TAB__OPC_MOD, "/mod" },
    /* 753 */ { ud_itab__753, UD_TAB__OPC_MOD, "/mod" },
    /* 754 */ { ud_itab__754, UD_TAB__OPC_MOD, "/mod" },
    /* 755 */ { ud_itab__755, UD_TAB__OPC_MOD, "/mod" },
    /* 756 */ { ud_itab__756, UD_TAB__OPC_MOD, "/mod" },
    /* 757 */ { ud_itab__757, UD_TAB__OPC_MOD, "/mod" },
    /* 758 */ { ud_itab__758, UD_TAB__OPC_MOD, "/mod" },
    /* 759 */ { ud_itab__759, UD_TAB__OPC_MOD, "/mod" },
    /* 760 */ { ud_itab__760, UD_TAB__OPC_MOD, "/mod" },
    /* 761 */ { ud_itab__761, UD_TAB__OPC_MOD, "/mod" },
    /* 762 */ { ud_itab__762, UD_TAB__OPC_MOD, "/mod" },
    /* 763 */ { ud_itab__763, UD_TAB__OPC_REG, "/reg" },
    /* 764 */ { ud_itab__764, UD_TAB__OPC_MOD, "/mod" },
    /* 765 */ { ud_itab__765, UD_TAB__OPC_MOD, "/mod" },
    /* 766 */ { ud_itab__766, UD_TAB__OPC_MOD, "/mod" },
    /* 767 */ { ud_itab__767, UD_TAB__OPC_MOD, "/mod" },
    /* 768 */ { ud_itab__768, UD_TAB__OPC_MOD, "/mod" },
    /* 769 */ { ud_itab__769, UD_TAB__OPC_MOD, "/mod" },
    /* 770 */ { ud_itab__770, UD_TAB__OPC_MOD, "/mod" },
    /* 771 */ { ud_itab__771, UD_TAB__OPC_MOD, "/mod" },
    /* 772 */ { ud_itab__772, UD_TAB__OPC_X87, "/x87" },
    /* 773 */ { ud_itab__773, UD_TAB__OPC_MOD, "/mod" },
    /* 774 */ { ud_itab__774, UD_TAB__OPC_MOD, "/mod" },
    /* 775 */ { ud_itab__775, UD_TAB__OPC_MOD, "/mod" },
    /* 776 */ { ud_itab__776, UD_TAB__OPC_MOD, "/mod" },
    /* 777 */ { ud_itab__777, UD_TAB__OPC_MOD, "/mod" },
    /* 778 */ { ud_itab__778, UD_TAB__OPC_MOD, "/mod" },
    /* 779 */ { ud_itab__779, UD_TAB__OPC_MOD, "/mod" },
    /* 780 */ { ud_itab__780, UD_TAB__OPC_MOD, "/mod" },
    /* 781 */ { ud_itab__781, UD_TAB__OPC_MOD, "/mod" },
    /* 782 */ { ud_itab__782, UD_TAB__OPC_MOD, "/mod" },
    /* 783 */ { ud_itab__783, UD_TAB__OPC_MOD, "/mod" },
    /* 784 */ { ud_itab__784, UD_TAB__OPC_MOD, "/mod" },
    /* 785 */ { ud_itab__785, UD_TAB__OPC_MOD, "/mod" },
    /* 786 */ { ud_itab__786, UD_TAB__OPC_MOD, "/mod" },
    /* 787 */ { ud_itab__787, UD_TAB__OPC_MOD, "/mod" },
    /* 788 */ { ud_itab__788, UD_TAB__OPC_MOD, "/mod" },
    /* 789 */ { ud_itab__789, UD_TAB__OPC_MOD, "/mod" },
    /* 790 */ { ud_itab__790, UD_TAB__OPC_MOD, "/mod" },
    /* 791 */ { ud_itab__791, UD_TAB__OPC_MOD, "/mod" },
    /* 792 */ { ud_itab__792, UD_TAB__OPC_MOD, "/mod" },
    /* 793 */ { ud_itab__793, UD_TAB__OPC_MOD, "/mod" },
    /* 794 */ { ud_itab__794, UD_TAB__OPC_MOD, "/mod" },
    /* 795 */ { ud_itab__795, UD_TAB__OPC_MOD, "/mod" },
    /* 796 */ { ud_itab__796, UD_TAB__OPC_MOD, "/mod" },
    /* 797 */ { ud_itab__797, UD_TAB__OPC_MOD, "/mod" },
    /* 798 */ { ud_itab__798, UD_TAB__OPC_MOD, "/mod" },
    /* 799 */ { ud_itab__799, UD_TAB__OPC_MOD, "/mod" },
    /* 800 */ { ud_itab__800, UD_TAB__OPC_MOD, "/mod" },
    /* 801 */ { ud_itab__801, UD_TAB__OPC_MOD, "/mod" },
    /* 802 */ { ud_itab__802, UD_TAB__OPC_MOD, "/mod" },
    /* 803 */ { ud_itab__803, UD_TAB__OPC_MOD, "/mod" },
    /* 804 */ { ud_itab__804, UD_TAB__OPC_MOD, "/mod" },
    /* 805 */ { ud_itab__805, UD_TAB__OPC_MOD, "/mod" },
    /* 806 */ { ud_itab__806, UD_TAB__OPC_MOD, "/mod" },
    /* 807 */ { ud_itab__807, UD_TAB__OPC_REG, "/reg" },
    /* 808 */ { ud_itab__808, UD_TAB__OPC_MOD, "/mod" },
    /* 809 */ { ud_itab__809, UD_TAB__OPC_MOD, "/mod" },
    /* 810 */ { ud_itab__810, UD_TAB__OPC_MOD, "/mod" },
    /* 811 */ { ud_itab__811, UD_TAB__OPC_MOD, "/mod" },
    /* 812 */ { ud_itab__812, UD_TAB__OPC_MOD, "/mod" },
    /* 813 */ { ud_itab__813, UD_TAB__OPC_MOD, "/mod" },
    /* 814 */ { ud_itab__814, UD_TAB__OPC_X87, "/x87" },
    /* 815 */ { ud_itab__815, UD_TAB__OPC_MOD, "/mod" },
    /* 816 */ { ud_itab__816, UD_TAB__OPC_MOD, "/mod" },
    /* 817 */ { ud_itab__817, UD_TAB__OPC_MOD, "/mod" },
    /* 818 */ { ud_itab__818, UD_TAB__OPC_MOD, "/mod" },
    /* 819 */ { ud_itab__819, UD_TAB__OPC_MOD, "/mod" },
    /* 820 */ { ud_itab__820, UD_TAB__OPC_MOD, "/mod" },
    /* 821 */ { ud_itab__821, UD_TAB__OPC_MOD, "/mod" },
    /* 822 */ { ud_itab__822, UD_TAB__OPC_MOD, "/mod" },
    /* 823 */ { ud_itab__823, UD_TAB__OPC_MOD, "/mod" },
    /* 824 */ { ud_itab__824, UD_TAB__OPC_MOD, "/mod" },
    /* 825 */ { ud_itab__825, UD_TAB__OPC_MOD, "/mod" },
    /* 826 */ { ud_itab__826, UD_TAB__OPC_MOD, "/mod" },
    /* 827 */ { ud_itab__827, UD_TAB__OPC_MOD, "/mod" },
    /* 828 */ { ud_itab__828, UD_TAB__OPC_MOD, "/mod" },
    /* 829 */ { ud_itab__829, UD_TAB__OPC_MOD, "/mod" },
    /* 830 */ { ud_itab__830, UD_TAB__OPC_MOD, "/mod" },
    /* 831 */ { ud_itab__831, UD_TAB__OPC_MOD, "/mod" },
    /* 832 */ { ud_itab__832, UD_TAB__OPC_MOD, "/mod" },
    /* 833 */ { ud_itab__833, UD_TAB__OPC_MOD, "/mod" },
    /* 834 */ { ud_itab__834, UD_TAB__OPC_MOD, "/mod" },
    /* 835 */ { ud_itab__835, UD_TAB__OPC_MOD, "/mod" },
    /* 836 */ { ud_itab__836, UD_TAB__OPC_MOD, "/mod" },
    /* 837 */ { ud_itab__837, UD_TAB__OPC_MOD, "/mod" },
    /* 838 */ { ud_itab__838, UD_TAB__OPC_MOD, "/mod" },
    /* 839 */ { ud_itab__839, UD_TAB__OPC_MOD, "/mod" },
    /* 840 */ { ud_itab__840, UD_TAB__OPC_MOD, "/mod" },
    /* 841 */ { ud_itab__841, UD_TAB__OPC_MOD, "/mod" },
    /* 842 */ { ud_itab__842, UD_TAB__OPC_MOD, "/mod" },
    /* 843 */ { ud_itab__843, UD_TAB__OPC_MOD, "/mod" },
    /* 844 */ { ud_itab__844, UD_TAB__OPC_MOD, "/mod" },
    /* 845 */ { ud_itab__845, UD_TAB__OPC_MOD, "/mod" },
    /* 846 */ { ud_itab__846, UD_TAB__OPC_MOD, "/mod" },
    /* 847 */ { ud_itab__847, UD_TAB__OPC_MOD, "/mod" },
    /* 848 */ { ud_itab__848, UD_TAB__OPC_MOD, "/mod" },
    /* 849 */ { ud_itab__849, UD_TAB__OPC_MOD, "/mod" },
    /* 850 */ { ud_itab__850, UD_TAB__OPC_MOD, "/mod" },
    /* 851 */ { ud_itab__851, UD_TAB__OPC_MOD, "/mod" },
    /* 852 */ { ud_itab__852, UD_TAB__OPC_MOD, "/mod" },
    /* 853 */ { ud_itab__853, UD_TAB__OPC_MOD, "/mod" },
    /* 854 */ { ud_itab__854, UD_TAB__OPC_MOD, "/mod" },
    /* 855 */ { ud_itab__855, UD_TAB__OPC_MOD, "/mod" },
    /* 856 */ { ud_itab__856, UD_TAB__OPC_MOD, "/mod" },
    /* 857 */ { ud_itab__857, UD_TAB__OPC_MOD, "/mod" },
    /* 858 */ { ud_itab__858, UD_TAB__OPC_MOD, "/mod" },
    /* 859 */ { ud_itab__859, UD_TAB__OPC_MOD, "/mod" },
    /* 860 */ { ud_itab__860, UD_TAB__OPC_MOD, "/mod" },
    /* 861 */ { ud_itab__861, UD_TAB__OPC_MOD, "/mod" },
    /* 862 */ { ud_itab__862, UD_TAB__OPC_MOD, "/mod" },
    /* 863 */ { ud_itab__863, UD_TAB__OPC_MOD, "/mod" },
    /* 864 */ { ud_itab__864, UD_TAB__OPC_MOD, "/mod" },
    /* 865 */ { ud_itab__865, UD_TAB__OPC_MOD, "/mod" },
    /* 866 */ { ud_itab__866, UD_TAB__OPC_REG, "/reg" },
    /* 867 */ { ud_itab__867, UD_TAB__OPC_MOD, "/mod" },
    /* 868 */ { ud_itab__868, UD_TAB__OPC_MOD, "/mod" },
    /* 869 */ { ud_itab__869, UD_TAB__OPC_MOD, "/mod" },
    /* 870 */ { ud_itab__870, UD_TAB__OPC_MOD, "/mod" },
    /* 871 */ { ud_itab__871, UD_TAB__OPC_MOD, "/mod" },
    /* 872 */ { ud_itab__872, UD_TAB__OPC_MOD, "/mod" },
    /* 873 */ { ud_itab__873, UD_TAB__OPC_MOD, "/mod" },
    /* 874 */ { ud_itab__874, UD_TAB__OPC_MOD, "/mod" },
    /* 875 */ { ud_itab__875, UD_TAB__OPC_X87, "/x87" },
    /* 876 */ { ud_itab__876, UD_TAB__OPC_MOD, "/mod" },
    /* 877 */ { ud_itab__877, UD_TAB__OPC_MOD, "/mod" },
    /* 878 */ { ud_itab__878, UD_TAB__OPC_MOD, "/mod" },
    /* 879 */ { ud_itab__879, UD_TAB__OPC_MOD, "/mod" },
    /* 880 */ { ud_itab__880, UD_TAB__OPC_MOD, "/mod" },
    /* 881 */ { ud_itab__881, UD_TAB__OPC_MOD, "/mod" },
    /* 882 */ { ud_itab__882, UD_TAB__OPC_MOD, "/mod" },
    /* 883 */ { ud_itab__883, UD_TAB__OPC_MOD, "/mod" },
    /* 884 */ { ud_itab__884, UD_TAB__OPC_MOD, "/mod" },
    /* 885 */ { ud_itab__885, UD_TAB__OPC_MOD, "/mod" },
    /* 886 */ { ud_itab__886, UD_TAB__OPC_MOD, "/mod" },
    /* 887 */ { ud_itab__887, UD_TAB__OPC_MOD, "/mod" },
    /* 888 */ { ud_itab__888, UD_TAB__OPC_MOD, "/mod" },
    /* 889 */ { ud_itab__889, UD_TAB__OPC_MOD, "/mod" },
    /* 890 */ { ud_itab__890, UD_TAB__OPC_MOD, "/mod" },
    /* 891 */ { ud_itab__891, UD_TAB__OPC_MOD, "/mod" },
    /* 892 */ { ud_itab__892, UD_TAB__OPC_MOD, "/mod" },
    /* 893 */ { ud_itab__893, UD_TAB__OPC_MOD, "/mod" },
    /* 894 */ { ud_itab__894, UD_TAB__OPC_MOD, "/mod" },
    /* 895 */ { ud_itab__895, UD_TAB__OPC_MOD, "/mod" },
    /* 896 */ { ud_itab__896, UD_TAB__OPC_MOD, "/mod" },
    /* 897 */ { ud_itab__897, UD_TAB__OPC_MOD, "/mod" },
    /* 898 */ { ud_itab__898, UD_TAB__OPC_MOD, "/mod" },
    /* 899 */ { ud_itab__899, UD_TAB__OPC_MOD, "/mod" },
    /* 900 */ { ud_itab__900, UD_TAB__OPC_MOD, "/mod" },
    /* 901 */ { ud_itab__901, UD_TAB__OPC_MOD, "/mod" },
    /* 902 */ { ud_itab__902, UD_TAB__OPC_MOD, "/mod" },
    /* 903 */ { ud_itab__903, UD_TAB__OPC_MOD, "/mod" },
    /* 904 */ { ud_itab__904, UD_TAB__OPC_MOD, "/mod" },
    /* 905 */ { ud_itab__905, UD_TAB__OPC_MOD, "/mod" },
    /* 906 */ { ud_itab__906, UD_TAB__OPC_MOD, "/mod" },
    /* 907 */ { ud_itab__907, UD_TAB__OPC_MOD, "/mod" },
    /* 908 */ { ud_itab__908, UD_TAB__OPC_MOD, "/mod" },
    /* 909 */ { ud_itab__909, UD_TAB__OPC_MOD, "/mod" },
    /* 910 */ { ud_itab__910, UD_TAB__OPC_MOD, "/mod" },
    /* 911 */ { ud_itab__911, UD_TAB__OPC_MOD, "/mod" },
    /* 912 */ { ud_itab__912, UD_TAB__OPC_MOD, "/mod" },
    /* 913 */ { ud_itab__913, UD_TAB__OPC_MOD, "/mod" },
    /* 914 */ { ud_itab__914, UD_TAB__OPC_MOD, "/mod" },
    /* 915 */ { ud_itab__915, UD_TAB__OPC_MOD, "/mod" },
    /* 916 */ { ud_itab__916, UD_TAB__OPC_MOD, "/mod" },
    /* 917 */ { ud_itab__917, UD_TAB__OPC_MOD, "/mod" },
    /* 918 */ { ud_itab__918, UD_TAB__OPC_MOD, "/mod" },
    /* 919 */ { ud_itab__919, UD_TAB__OPC_MOD, "/mod" },
    /* 920 */ { ud_itab__920, UD_TAB__OPC_MOD, "/mod" },
    /* 921 */ { ud_itab__921, UD_TAB__OPC_MOD, "/mod" },
    /* 922 */ { ud_itab__922, UD_TAB__OPC_MOD, "/mod" },
    /* 923 */ { ud_itab__923, UD_TAB__OPC_MOD, "/mod" },
    /* 924 */ { ud_itab__924, UD_TAB__OPC_MOD, "/mod" },
    /* 925 */ { ud_itab__925, UD_TAB__OPC_MOD, "/mod" },
    /* 926 */ { ud_itab__926, UD_TAB__OPC_MOD, "/mod" },
    /* 927 */ { ud_itab__927, UD_TAB__OPC_MOD, "/mod" },
    /* 928 */ { ud_itab__928, UD_TAB__OPC_MOD, "/mod" },
    /* 929 */ { ud_itab__929, UD_TAB__OPC_MOD, "/mod" },
    /* 930 */ { ud_itab__930, UD_TAB__OPC_MOD, "/mod" },
    /* 931 */ { ud_itab__931, UD_TAB__OPC_MOD, "/mod" },
    /* 932 */ { ud_itab__932, UD_TAB__OPC_MOD, "/mod" },
    /* 933 */ { ud_itab__933, UD_TAB__OPC_MOD, "/mod" },
    /* 934 */ { ud_itab__934, UD_TAB__OPC_MOD, "/mod" },
    /* 935 */ { ud_itab__935, UD_TAB__OPC_MOD, "/mod" },
    /* 936 */ { ud_itab__936, UD_TAB__OPC_MOD, "/mod" },
    /* 937 */ { ud_itab__937, UD_TAB__OPC_MOD, "/mod" },
    /* 938 */ { ud_itab__938, UD_TAB__OPC_MOD, "/mod" },
    /* 939 */ { ud_itab__939, UD_TAB__OPC_MOD, "/mod" },
    /* 940 */ { ud_itab__940, UD_TAB__OPC_MOD, "/mod" },
    /* 941 */ { ud_itab__941, UD_TAB__OPC_REG, "/reg" },
    /* 942 */ { ud_itab__942, UD_TAB__OPC_MOD, "/mod" },
    /* 943 */ { ud_itab__943, UD_TAB__OPC_MOD, "/mod" },
    /* 944 */ { ud_itab__944, UD_TAB__OPC_MOD, "/mod" },
    /* 945 */ { ud_itab__945, UD_TAB__OPC_MOD, "/mod" },
    /* 946 */ { ud_itab__946, UD_TAB__OPC_MOD, "/mod" },
    /* 947 */ { ud_itab__947, UD_TAB__OPC_MOD, "/mod" },
    /* 948 */ { ud_itab__948, UD_TAB__OPC_MOD, "/mod" },
    /* 949 */ { ud_itab__949, UD_TAB__OPC_X87, "/x87" },
    /* 950 */ { ud_itab__950, UD_TAB__OPC_MOD, "/mod" },
    /* 951 */ { ud_itab__951, UD_TAB__OPC_MOD, "/mod" },
    /* 952 */ { ud_itab__952, UD_TAB__OPC_MOD, "/mod" },
    /* 953 */ { ud_itab__953, UD_TAB__OPC_MOD, "/mod" },
    /* 954 */ { ud_itab__954, UD_TAB__OPC_MOD, "/mod" },
    /* 955 */ { ud_itab__955, UD_TAB__OPC_MOD, "/mod" },
    /* 956 */ { ud_itab__956, UD_TAB__OPC_MOD, "/mod" },
    /* 957 */ { ud_itab__957, UD_TAB__OPC_MOD, "/mod" },
    /* 958 */ { ud_itab__958, UD_TAB__OPC_MOD, "/mod" },
    /* 959 */ { ud_itab__959, UD_TAB__OPC_MOD, "/mod" },
    /* 960 */ { ud_itab__960, UD_TAB__OPC_MOD, "/mod" },
    /* 961 */ { ud_itab__961, UD_TAB__OPC_MOD, "/mod" },
    /* 962 */ { ud_itab__962, UD_TAB__OPC_MOD, "/mod" },
    /* 963 */ { ud_itab__963, UD_TAB__OPC_MOD, "/mod" },
    /* 964 */ { ud_itab__964, UD_TAB__OPC_MOD, "/mod" },
    /* 965 */ { ud_itab__965, UD_TAB__OPC_MOD, "/mod" },
    /* 966 */ { ud_itab__966, UD_TAB__OPC_MOD, "/mod" },
    /* 967 */ { ud_itab__967, UD_TAB__OPC_MOD, "/mod" },
    /* 968 */ { ud_itab__968, UD_TAB__OPC_MOD, "/mod" },
    /* 969 */ { ud_itab__969, UD_TAB__OPC_MOD, "/mod" },
    /* 970 */ { ud_itab__970, UD_TAB__OPC_MOD, "/mod" },
    /* 971 */ { ud_itab__971, UD_TAB__OPC_MOD, "/mod" },
    /* 972 */ { ud_itab__972, UD_TAB__OPC_MOD, "/mod" },
    /* 973 */ { ud_itab__973, UD_TAB__OPC_MOD, "/mod" },
    /* 974 */ { ud_itab__974, UD_TAB__OPC_MOD, "/mod" },
    /* 975 */ { ud_itab__975, UD_TAB__OPC_MOD, "/mod" },
    /* 976 */ { ud_itab__976, UD_TAB__OPC_MOD, "/mod" },
    /* 977 */ { ud_itab__977, UD_TAB__OPC_MOD, "/mod" },
    /* 978 */ { ud_itab__978, UD_TAB__OPC_MOD, "/mod" },
    /* 979 */ { ud_itab__979, UD_TAB__OPC_MOD, "/mod" },
    /* 980 */ { ud_itab__980, UD_TAB__OPC_MOD, "/mod" },
    /* 981 */ { ud_itab__981, UD_TAB__OPC_MOD, "/mod" },
    /* 982 */ { ud_itab__982, UD_TAB__OPC_MOD, "/mod" },
    /* 983 */ { ud_itab__983, UD_TAB__OPC_MOD, "/mod" },
    /* 984 */ { ud_itab__984, UD_TAB__OPC_MOD, "/mod" },
    /* 985 */ { ud_itab__985, UD_TAB__OPC_MOD, "/mod" },
    /* 986 */ { ud_itab__986, UD_TAB__OPC_MOD, "/mod" },
    /* 987 */ { ud_itab__987, UD_TAB__OPC_MOD, "/mod" },
    /* 988 */ { ud_itab__988, UD_TAB__OPC_MOD, "/mod" },
    /* 989 */ { ud_itab__989, UD_TAB__OPC_MOD, "/mod" },
    /* 990 */ { ud_itab__990, UD_TAB__OPC_MOD, "/mod" },
    /* 991 */ { ud_itab__991, UD_TAB__OPC_MOD, "/mod" },
    /* 992 */ { ud_itab__992, UD_TAB__OPC_MOD, "/mod" },
    /* 993 */ { ud_itab__993, UD_TAB__OPC_MOD, "/mod" },
    /* 994 */ { ud_itab__994, UD_TAB__OPC_MOD, "/mod" },
    /* 995 */ { ud_itab__995, UD_TAB__OPC_MOD, "/mod" },
    /* 996 */ { ud_itab__996, UD_TAB__OPC_MOD, "/mod" },
    /* 997 */ { ud_itab__997, UD_TAB__OPC_MOD, "/mod" },
    /* 998 */ { ud_itab__998, UD_TAB__OPC_MOD, "/mod" },
    /* 999 */ { ud_itab__999, UD_TAB__OPC_REG, "/reg" },
    /* 1000 */ { ud_itab__1000, UD_TAB__OPC_MOD, "/mod" },
    /* 1001 */ { ud_itab__1001, UD_TAB__OPC_MOD, "/mod" },
    /* 1002 */ { ud_itab__1002, UD_TAB__OPC_MOD, "/mod" },
    /* 1003 */ { ud_itab__1003, UD_TAB__OPC_MOD, "/mod" },
    /* 1004 */ { ud_itab__1004, UD_TAB__OPC_MOD, "/mod" },
    /* 1005 */ { ud_itab__1005, UD_TAB__OPC_MOD, "/mod" },
    /* 1006 */ { ud_itab__1006, UD_TAB__OPC_MOD, "/mod" },
    /* 1007 */ { ud_itab__1007, UD_TAB__OPC_MOD, "/mod" },
    /* 1008 */ { ud_itab__1008, UD_TAB__OPC_X87, "/x87" },
    /* 1009 */ { ud_itab__1009, UD_TAB__OPC_MOD, "/mod" },
    /* 1010 */ { ud_itab__1010, UD_TAB__OPC_MOD, "/mod" },
    /* 1011 */ { ud_itab__1011, UD_TAB__OPC_MOD, "/mod" },
    /* 1012 */ { ud_itab__1012, UD_TAB__OPC_MOD, "/mod" },
    /* 1013 */ { ud_itab__1013, UD_TAB__OPC_MOD, "/mod" },
    /* 1014 */ { ud_itab__1014, UD_TAB__OPC_MOD, "/mod" },
    /* 1015 */ { ud_itab__1015, UD_TAB__OPC_MOD, "/mod" },
    /* 1016 */ { ud_itab__1016, UD_TAB__OPC_MOD, "/mod" },
    /* 1017 */ { ud_itab__1017, UD_TAB__OPC_MOD, "/mod" },
    /* 1018 */ { ud_itab__1018, UD_TAB__OPC_MOD, "/mod" },
    /* 1019 */ { ud_itab__1019, UD_TAB__OPC_MOD, "/mod" },
    /* 1020 */ { ud_itab__1020, UD_TAB__OPC_MOD, "/mod" },
    /* 1021 */ { ud_itab__1021, UD_TAB__OPC_MOD, "/mod" },
    /* 1022 */ { ud_itab__1022, UD_TAB__OPC_MOD, "/mod" },
    /* 1023 */ { ud_itab__1023, UD_TAB__OPC_MOD, "/mod" },
    /* 1024 */ { ud_itab__1024, UD_TAB__OPC_MOD, "/mod" },
    /* 1025 */ { ud_itab__1025, UD_TAB__OPC_MOD, "/mod" },
    /* 1026 */ { ud_itab__1026, UD_TAB__OPC_MOD, "/mod" },
    /* 1027 */ { ud_itab__1027, UD_TAB__OPC_MOD, "/mod" },
    /* 1028 */ { ud_itab__1028, UD_TAB__OPC_MOD, "/mod" },
    /* 1029 */ { ud_itab__1029, UD_TAB__OPC_MOD, "/mod" },
    /* 1030 */ { ud_itab__1030, UD_TAB__OPC_MOD, "/mod" },
    /* 1031 */ { ud_itab__1031, UD_TAB__OPC_MOD, "/mod" },
    /* 1032 */ { ud_itab__1032, UD_TAB__OPC_MOD, "/mod" },
    /* 1033 */ { ud_itab__1033, UD_TAB__OPC_MOD, "/mod" },
    /* 1034 */ { ud_itab__1034, UD_TAB__OPC_MOD, "/mod" },
    /* 1035 */ { ud_itab__1035, UD_TAB__OPC_MOD, "/mod" },
    /* 1036 */ { ud_itab__1036, UD_TAB__OPC_MOD, "/mod" },
    /* 1037 */ { ud_itab__1037, UD_TAB__OPC_MOD, "/mod" },
    /* 1038 */ { ud_itab__1038, UD_TAB__OPC_MOD, "/mod" },
    /* 1039 */ { ud_itab__1039, UD_TAB__OPC_MOD, "/mod" },
    /* 1040 */ { ud_itab__1040, UD_TAB__OPC_MOD, "/mod" },
    /* 1041 */ { ud_itab__1041, UD_TAB__OPC_MOD, "/mod" },
    /* 1042 */ { ud_itab__1042, UD_TAB__OPC_MOD, "/mod" },
    /* 1043 */ { ud_itab__1043, UD_TAB__OPC_MOD, "/mod" },
    /* 1044 */ { ud_itab__1044, UD_TAB__OPC_MOD, "/mod" },
    /* 1045 */ { ud_itab__1045, UD_TAB__OPC_MOD, "/mod" },
    /* 1046 */ { ud_itab__1046, UD_TAB__OPC_MOD, "/mod" },
    /* 1047 */ { ud_itab__1047, UD_TAB__OPC_MOD, "/mod" },
    /* 1048 */ { ud_itab__1048, UD_TAB__OPC_MOD, "/mod" },
    /* 1049 */ { ud_itab__1049, UD_TAB__OPC_MOD, "/mod" },
    /* 1050 */ { ud_itab__1050, UD_TAB__OPC_MOD, "/mod" },
    /* 1051 */ { ud_itab__1051, UD_TAB__OPC_MOD, "/mod" },
    /* 1052 */ { ud_itab__1052, UD_TAB__OPC_MOD, "/mod" },
    /* 1053 */ { ud_itab__1053, UD_TAB__OPC_MOD, "/mod" },
    /* 1054 */ { ud_itab__1054, UD_TAB__OPC_MOD, "/mod" },
    /* 1055 */ { ud_itab__1055, UD_TAB__OPC_MOD, "/mod" },
    /* 1056 */ { ud_itab__1056, UD_TAB__OPC_MOD, "/mod" },
    /* 1057 */ { ud_itab__1057, UD_TAB__OPC_MOD, "/mod" },
    /* 1058 */ { ud_itab__1058, UD_TAB__OPC_MOD, "/mod" },
    /* 1059 */ { ud_itab__1059, UD_TAB__OPC_MOD, "/mod" },
    /* 1060 */ { ud_itab__1060, UD_TAB__OPC_MOD, "/mod" },
    /* 1061 */ { ud_itab__1061, UD_TAB__OPC_MOD, "/mod" },
    /* 1062 */ { ud_itab__1062, UD_TAB__OPC_MOD, "/mod" },
    /* 1063 */ { ud_itab__1063, UD_TAB__OPC_MOD, "/mod" },
    /* 1064 */ { ud_itab__1064, UD_TAB__OPC_MOD, "/mod" },
    /* 1065 */ { ud_itab__1065, UD_TAB__OPC_MOD, "/mod" },
    /* 1066 */ { ud_itab__1066, UD_TAB__OPC_MOD, "/mod" },
    /* 1067 */ { ud_itab__1067, UD_TAB__OPC_REG, "/reg" },
    /* 1068 */ { ud_itab__1068, UD_TAB__OPC_MOD, "/mod" },
    /* 1069 */ { ud_itab__1069, UD_TAB__OPC_MOD, "/mod" },
    /* 1070 */ { ud_itab__1070, UD_TAB__OPC_MOD, "/mod" },
    /* 1071 */ { ud_itab__1071, UD_TAB__OPC_MOD, "/mod" },
    /* 1072 */ { ud_itab__1072, UD_TAB__OPC_MOD, "/mod" },
    /* 1073 */ { ud_itab__1073, UD_TAB__OPC_MOD, "/mod" },
    /* 1074 */ { ud_itab__1074, UD_TAB__OPC_MOD, "/mod" },
    /* 1075 */ { ud_itab__1075, UD_TAB__OPC_MOD, "/mod" },
    /* 1076 */ { ud_itab__1076, UD_TAB__OPC_X87, "/x87" },
    /* 1077 */ { ud_itab__1077, UD_TAB__OPC_MOD, "/mod" },
    /* 1078 */ { ud_itab__1078, UD_TAB__OPC_MOD, "/mod" },
    /* 1079 */ { ud_itab__1079, UD_TAB__OPC_MOD, "/mod" },
    /* 1080 */ { ud_itab__1080, UD_TAB__OPC_MOD, "/mod" },
    /* 1081 */ { ud_itab__1081, UD_TAB__OPC_MOD, "/mod" },
    /* 1082 */ { ud_itab__1082, UD_TAB__OPC_MOD, "/mod" },
    /* 1083 */ { ud_itab__1083, UD_TAB__OPC_MOD, "/mod" },
    /* 1084 */ { ud_itab__1084, UD_TAB__OPC_MOD, "/mod" },
    /* 1085 */ { ud_itab__1085, UD_TAB__OPC_MOD, "/mod" },
    /* 1086 */ { ud_itab__1086, UD_TAB__OPC_MOD, "/mod" },
    /* 1087 */ { ud_itab__1087, UD_TAB__OPC_MOD, "/mod" },
    /* 1088 */ { ud_itab__1088, UD_TAB__OPC_MOD, "/mod" },
    /* 1089 */ { ud_itab__1089, UD_TAB__OPC_MOD, "/mod" },
    /* 1090 */ { ud_itab__1090, UD_TAB__OPC_MOD, "/mod" },
    /* 1091 */ { ud_itab__1091, UD_TAB__OPC_MOD, "/mod" },
    /* 1092 */ { ud_itab__1092, UD_TAB__OPC_MOD, "/mod" },
    /* 1093 */ { ud_itab__1093, UD_TAB__OPC_MOD, "/mod" },
    /* 1094 */ { ud_itab__1094, UD_TAB__OPC_MOD, "/mod" },
    /* 1095 */ { ud_itab__1095, UD_TAB__OPC_MOD, "/mod" },
    /* 1096 */ { ud_itab__1096, UD_TAB__OPC_MOD, "/mod" },
    /* 1097 */ { ud_itab__1097, UD_TAB__OPC_MOD, "/mod" },
    /* 1098 */ { ud_itab__1098, UD_TAB__OPC_MOD, "/mod" },
    /* 1099 */ { ud_itab__1099, UD_TAB__OPC_MOD, "/mod" },
    /* 1100 */ { ud_itab__1100, UD_TAB__OPC_MOD, "/mod" },
    /* 1101 */ { ud_itab__1101, UD_TAB__OPC_MOD, "/mod" },
    /* 1102 */ { ud_itab__1102, UD_TAB__OPC_MOD, "/mod" },
    /* 1103 */ { ud_itab__1103, UD_TAB__OPC_MOD, "/mod" },
    /* 1104 */ { ud_itab__1104, UD_TAB__OPC_MOD, "/mod" },
    /* 1105 */ { ud_itab__1105, UD_TAB__OPC_MOD, "/mod" },
    /* 1106 */ { ud_itab__1106, UD_TAB__OPC_MOD, "/mod" },
    /* 1107 */ { ud_itab__1107, UD_TAB__OPC_MOD, "/mod" },
    /* 1108 */ { ud_itab__1108, UD_TAB__OPC_MOD, "/mod" },
    /* 1109 */ { ud_itab__1109, UD_TAB__OPC_MOD, "/mod" },
    /* 1110 */ { ud_itab__1110, UD_TAB__OPC_MOD, "/mod" },
    /* 1111 */ { ud_itab__1111, UD_TAB__OPC_MOD, "/mod" },
    /* 1112 */ { ud_itab__1112, UD_TAB__OPC_MOD, "/mod" },
    /* 1113 */ { ud_itab__1113, UD_TAB__OPC_MOD, "/mod" },
    /* 1114 */ { ud_itab__1114, UD_TAB__OPC_MOD, "/mod" },
    /* 1115 */ { ud_itab__1115, UD_TAB__OPC_MOD, "/mod" },
    /* 1116 */ { ud_itab__1116, UD_TAB__OPC_MOD, "/mod" },
    /* 1117 */ { ud_itab__1117, UD_TAB__OPC_MOD, "/mod" },
    /* 1118 */ { ud_itab__1118, UD_TAB__OPC_MOD, "/mod" },
    /* 1119 */ { ud_itab__1119, UD_TAB__OPC_MOD, "/mod" },
    /* 1120 */ { ud_itab__1120, UD_TAB__OPC_MOD, "/mod" },
    /* 1121 */ { ud_itab__1121, UD_TAB__OPC_MOD, "/mod" },
    /* 1122 */ { ud_itab__1122, UD_TAB__OPC_MOD, "/mod" },
    /* 1123 */ { ud_itab__1123, UD_TAB__OPC_MOD, "/mod" },
    /* 1124 */ { ud_itab__1124, UD_TAB__OPC_MOD, "/mod" },
    /* 1125 */ { ud_itab__1125, UD_TAB__OPC_MOD, "/mod" },
    /* 1126 */ { ud_itab__1126, UD_TAB__OPC_ASIZE, "/a" },
    /* 1127 */ { ud_itab__1127, UD_TAB__OPC_MODE, "/m" },
    /* 1128 */ { ud_itab__1128, UD_TAB__OPC_REG, "/reg" },
    /* 1129 */ { ud_itab__1129, UD_TAB__OPC_REG, "/reg" },
    /* 1130 */ { ud_itab__1130, UD_TAB__OPC_REG, "/reg" },
    /* 1131 */ { ud_itab__1131, UD_TAB__OPC_REG, "/reg" },
    /* 1132 */ { ud_itab__1132, UD_TAB__OPC_MODE, "/m" },
};

/* itab entry operand definitions (for readability) */
#define O_AL      { OP_AL,       SZ_B     }
#define O_AX      { OP_AX,       SZ_W     }
#define O_Av      { OP_A,        SZ_V     }
#define O_C       { OP_C,        SZ_NA    }
#define O_CL      { OP_CL,       SZ_B     }
#define O_CS      { OP_CS,       SZ_NA    }
#define O_CX      { OP_CX,       SZ_W     }
#define O_D       { OP_D,        SZ_NA    }
#define O_DL      { OP_DL,       SZ_B     }
#define O_DS      { OP_DS,       SZ_NA    }
#define O_DX      { OP_DX,       SZ_W     }
#define O_E       { OP_E,        SZ_NA    }
#define O_ES      { OP_ES,       SZ_NA    }
#define O_Eb      { OP_E,        SZ_B     }
#define O_Ed      { OP_E,        SZ_D     }
#define O_Eq      { OP_E,        SZ_Q     }
#define O_Ev      { OP_E,        SZ_V     }
#define O_Ew      { OP_E,        SZ_W     }
#define O_Ey      { OP_E,        SZ_Y     }
#define O_Ez      { OP_E,        SZ_Z     }
#define O_FS      { OP_FS,       SZ_NA    }
#define O_Fv      { OP_F,        SZ_V     }
#define O_G       { OP_G,        SZ_NA    }
#define O_GS      { OP_GS,       SZ_NA    }
#define O_Gb      { OP_G,        SZ_B     }
#define O_Gd      { OP_G,        SZ_D     }
#define O_Gq      { OP_G,        SZ_Q     }
#define O_Gv      { OP_G,        SZ_V     }
#define O_Gw      { OP_G,        SZ_W     }
#define O_Gy      { OP_G,        SZ_Y     }
#define O_Gz      { OP_G,        SZ_Z     }
#define O_I1      { OP_I1,       SZ_NA    }
#define O_I3      { OP_I3,       SZ_NA    }
#define O_Ib      { OP_I,        SZ_B     }
#define O_Iv      { OP_I,        SZ_V     }
#define O_Iw      { OP_I,        SZ_W     }
#define O_Iz      { OP_I,        SZ_Z     }
#define O_Jb      { OP_J,        SZ_B     }
#define O_Jv      { OP_J,        SZ_V     }
#define O_Jz      { OP_J,        SZ_Z     }
#define O_M       { OP_M,        SZ_NA    }
#define O_Mb      { OP_M,        SZ_B     }
#define O_MbRd    { OP_MR,       SZ_BD    }
#define O_MbRv    { OP_MR,       SZ_BV    }
#define O_Md      { OP_M,        SZ_D     }
#define O_MdRy    { OP_MR,       SZ_DY    }
#define O_MdU     { OP_MU,       SZ_DO    }
#define O_Mo      { OP_M,        SZ_O     }
#define O_Mq      { OP_M,        SZ_Q     }
#define O_MqU     { OP_MU,       SZ_QO    }
#define O_Ms      { OP_M,        SZ_W     }
#define O_Mt      { OP_M,        SZ_T     }
#define O_Mv      { OP_M,        SZ_V     }
#define O_Mw      { OP_M,        SZ_W     }
#define O_MwRd    { OP_MR,       SZ_WD    }
#define O_MwRv    { OP_MR,       SZ_WV    }
#define O_MwRy    { OP_MR,       SZ_WY    }
#define O_MwU     { OP_MU,       SZ_WO    }
#define O_N       { OP_N,        SZ_Q     }
#define O_NONE    { OP_NONE,     SZ_NA    }
#define O_Ob      { OP_O,        SZ_B     }
#define O_Ov      { OP_O,        SZ_V     }
#define O_Ow      { OP_O,        SZ_W     }
#define O_P       { OP_P,        SZ_Q     }
#define O_Q       { OP_Q,        SZ_Q     }
#define O_R       { OP_R,        SZ_RDQ   }
#define O_R0b     { OP_R0,       SZ_B     }
#define O_R0v     { OP_R0,       SZ_V     }
#define O_R0w     { OP_R0,       SZ_W     }
#define O_R0y     { OP_R0,       SZ_Y     }
#define O_R0z     { OP_R0,       SZ_Z     }
#define O_R1b     { OP_R1,       SZ_B     }
#define O_R1v     { OP_R1,       SZ_V     }
#define O_R1w     { OP_R1,       SZ_W     }
#define O_R1y     { OP_R1,       SZ_Y     }
#define O_R1z     { OP_R1,       SZ_Z     }
#define O_R2b     { OP_R2,       SZ_B     }
#define O_R2v     { OP_R2,       SZ_V     }
#define O_R2w     { OP_R2,       SZ_W     }
#define O_R2y     { OP_R2,       SZ_Y     }
#define O_R2z     { OP_R2,       SZ_Z     }
#define O_R3b     { OP_R3,       SZ_B     }
#define O_R3v     { OP_R3,       SZ_V     }
#define O_R3w     { OP_R3,       SZ_W     }
#define O_R3y     { OP_R3,       SZ_Y     }
#define O_R3z     { OP_R3,       SZ_Z     }
#define O_R4b     { OP_R4,       SZ_B     }
#define O_R4v     { OP_R4,       SZ_V     }
#define O_R4w     { OP_R4,       SZ_W     }
#define O_R4y     { OP_R4,       SZ_Y     }
#define O_R4z     { OP_R4,       SZ_Z     }
#define O_R5b     { OP_R5,       SZ_B     }
#define O_R5v     { OP_R5,       SZ_V     }
#define O_R5w     { OP_R5,       SZ_W     }
#define O_R5y     { OP_R5,       SZ_Y     }
#define O_R5z     { OP_R5,       SZ_Z     }
#define O_R6b     { OP_R6,       SZ_B     }
#define O_R6v     { OP_R6,       SZ_V     }
#define O_R6w     { OP_R6,       SZ_W     }
#define O_R6y     { OP_R6,       SZ_Y     }
#define O_R6z     { OP_R6,       SZ_Z     }
#define O_R7b     { OP_R7,       SZ_B     }
#define O_R7v     { OP_R7,       SZ_V     }
#define O_R7w     { OP_R7,       SZ_W     }
#define O_R7y     { OP_R7,       SZ_Y     }
#define O_R7z     { OP_R7,       SZ_Z     }
#define O_S       { OP_S,        SZ_NA    }
#define O_SS      { OP_SS,       SZ_NA    }
#define O_ST0     { OP_ST0,      SZ_NA    }
#define O_ST1     { OP_ST1,      SZ_NA    }
#define O_ST2     { OP_ST2,      SZ_NA    }
#define O_ST3     { OP_ST3,      SZ_NA    }
#define O_ST4     { OP_ST4,      SZ_NA    }
#define O_ST5     { OP_ST5,      SZ_NA    }
#define O_ST6     { OP_ST6,      SZ_NA    }
#define O_ST7     { OP_ST7,      SZ_NA    }
#define O_U       { OP_U,        SZ_O     }
#define O_V       { OP_V,        SZ_O     }
#define O_W       { OP_W,        SZ_O     }
#define O_eAX     { OP_eAX,      SZ_Z     }
#define O_eCX     { OP_eCX,      SZ_Z     }
#define O_eDX     { OP_eDX,      SZ_Z     }
#define O_rAX     { OP_rAX,      SZ_V     }
#define O_rCX     { OP_rCX,      SZ_V     }
#define O_rDX     { OP_rDX,      SZ_V     }
#define O_sIb     { OP_sI,       SZ_B     }
#define O_sIv     { OP_sI,       SZ_V     }
#define O_sIz     { OP_sI,       SZ_Z     }

struct ud_itab_entry ud_itab[] = {
  /* 0000 */ { UD_Iinvalid, O_NONE, O_NONE, O_NONE, P_none },
  /* 0001 */ { UD_Iadd, O_Eb, O_Gb, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0002 */ { UD_Iadd, O_Ev, O_Gv, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0003 */ { UD_Iadd, O_Gb, O_Eb, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0004 */ { UD_Iadd, O_Gv, O_Ev, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0005 */ { UD_Iadd, O_AL, O_Ib, O_NONE, P_none },
  /* 0006 */ { UD_Iadd, O_rAX, O_sIz, O_NONE, P_oso|P_rexw },
  /* 0007 */ { UD_Ipush, O_ES, O_NONE, O_NONE, P_inv64 },
  /* 0008 */ { UD_Ipop, O_ES, O_NONE, O_NONE, P_inv64 },
  /* 0009 */ { UD_Ior, O_Eb, O_Gb, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0010 */ { UD_Ior, O_Ev, O_Gv, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0011 */ { UD_Ior, O_Gb, O_Eb, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0012 */ { UD_Ior, O_Gv, O_Ev, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0013 */ { UD_Ior, O_AL, O_Ib, O_NONE, P_none },
  /* 0014 */ { UD_Ior, O_rAX, O_sIz, O_NONE, P_oso|P_rexw },
  /* 0015 */ { UD_Ipush, O_CS, O_NONE, O_NONE, P_inv64 },
  /* 0016 */ { UD_Isldt, O_MwRv, O_NONE, O_NONE, P_aso|P_oso|P_rexr|P_rexw|P_rexx|P_rexb },
  /* 0017 */ { UD_Istr, O_MwRv, O_NONE, O_NONE, P_aso|P_oso|P_rexr|P_rexw|P_rexx|P_rexb },
  /* 0018 */ { UD_Illdt, O_Ew, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0019 */ { UD_Iltr, O_Ew, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0020 */ { UD_Iverr, O_Ew, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0021 */ { UD_Iverw, O_Ew, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0022 */ { UD_Isgdt, O_M, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0023 */ { UD_Isidt, O_M, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0024 */ { UD_Ilgdt, O_M, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0025 */ { UD_Ilidt, O_M, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0026 */ { UD_Ismsw, O_MwRv, O_NONE, O_NONE, P_aso|P_oso|P_rexr|P_rexw|P_rexx|P_rexb },
  /* 0027 */ { UD_Ilmsw, O_Ew, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0028 */ { UD_Iinvlpg, O_M, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0029 */ { UD_Ivmcall, O_NONE, O_NONE, O_NONE, P_none },
  /* 0030 */ { UD_Ivmlaunch, O_NONE, O_NONE, O_NONE, P_none },
  /* 0031 */ { UD_Ivmresume, O_NONE, O_NONE, O_NONE, P_none },
  /* 0032 */ { UD_Ivmxoff, O_NONE, O_NONE, O_NONE, P_none },
  /* 0033 */ { UD_Imonitor, O_NONE, O_NONE, O_NONE, P_none },
  /* 0034 */ { UD_Imwait, O_NONE, O_NONE, O_NONE, P_none },
  /* 0035 */ { UD_Ixgetbv, O_NONE, O_NONE, O_NONE, P_none },
  /* 0036 */ { UD_Ixsetbv, O_NONE, O_NONE, O_NONE, P_none },
  /* 0037 */ { UD_Ivmrun, O_NONE, O_NONE, O_NONE, P_none },
  /* 0038 */ { UD_Ivmmcall, O_NONE, O_NONE, O_NONE, P_none },
  /* 0039 */ { UD_Ivmload, O_NONE, O_NONE, O_NONE, P_none },
  /* 0040 */ { UD_Ivmsave, O_NONE, O_NONE, O_NONE, P_none },
  /* 0041 */ { UD_Istgi, O_NONE, O_NONE, O_NONE, P_none },
  /* 0042 */ { UD_Iclgi, O_NONE, O_NONE, O_NONE, P_none },
  /* 0043 */ { UD_Iskinit, O_NONE, O_NONE, O_NONE, P_none },
  /* 0044 */ { UD_Iinvlpga, O_NONE, O_NONE, O_NONE, P_none },
  /* 0045 */ { UD_Ismsw, O_MwRv, O_NONE, O_NONE, P_aso|P_oso|P_rexr|P_rexw|P_rexx|P_rexb },
  /* 0046 */ { UD_Ilmsw, O_Ew, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0047 */ { UD_Iswapgs, O_NONE, O_NONE, O_NONE, P_none },
  /* 0048 */ { UD_Irdtscp, O_NONE, O_NONE, O_NONE, P_none },
  /* 0049 */ { UD_Ilar, O_Gv, O_Ew, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0050 */ { UD_Ilsl, O_Gv, O_Ew, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0051 */ { UD_Isyscall, O_NONE, O_NONE, O_NONE, P_none },
  /* 0052 */ { UD_Iclts, O_NONE, O_NONE, O_NONE, P_none },
  /* 0053 */ { UD_Isysret, O_NONE, O_NONE, O_NONE, P_none },
  /* 0054 */ { UD_Iinvd, O_NONE, O_NONE, O_NONE, P_none },
  /* 0055 */ { UD_Iwbinvd, O_NONE, O_NONE, O_NONE, P_none },
  /* 0056 */ { UD_Iud2, O_NONE, O_NONE, O_NONE, P_none },
  /* 0057 */ { UD_Iprefetch, O_M, O_NONE, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0058 */ { UD_Iprefetch, O_M, O_NONE, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0059 */ { UD_Iprefetch, O_M, O_NONE, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0060 */ { UD_Iprefetch, O_M, O_NONE, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0061 */ { UD_Iprefetch, O_M, O_NONE, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0062 */ { UD_Iprefetch, O_M, O_NONE, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0063 */ { UD_Iprefetch, O_M, O_NONE, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0064 */ { UD_Iprefetch, O_M, O_NONE, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0065 */ { UD_Ifemms, O_NONE, O_NONE, O_NONE, P_none },
  /* 0066 */ { UD_Ipi2fw, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0067 */ { UD_Ipi2fd, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0068 */ { UD_Ipf2iw, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0069 */ { UD_Ipf2id, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0070 */ { UD_Ipfnacc, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0071 */ { UD_Ipfpnacc, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0072 */ { UD_Ipfcmpge, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0073 */ { UD_Ipfmin, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0074 */ { UD_Ipfrcp, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0075 */ { UD_Ipfrsqrt, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0076 */ { UD_Ipfsub, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0077 */ { UD_Ipfadd, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0078 */ { UD_Ipfcmpgt, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0079 */ { UD_Ipfmax, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0080 */ { UD_Ipfrcpit1, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0081 */ { UD_Ipfrsqit1, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0082 */ { UD_Ipfsubr, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0083 */ { UD_Ipfacc, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0084 */ { UD_Ipfcmpeq, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0085 */ { UD_Ipfmul, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0086 */ { UD_Ipfrcpit2, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0087 */ { UD_Ipmulhrw, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0088 */ { UD_Ipswapd, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0089 */ { UD_Ipavgusb, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0090 */ { UD_Imovups, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0091 */ { UD_Imovsd, O_V, O_W, O_NONE, P_str|P_aso|P_rexr|P_rexx|P_rexb },
  /* 0092 */ { UD_Imovss, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0093 */ { UD_Imovupd, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0094 */ { UD_Imovups, O_W, O_V, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0095 */ { UD_Imovsd, O_W, O_V, O_NONE, P_str|P_aso|P_rexr|P_rexx|P_rexb },
  /* 0096 */ { UD_Imovss, O_W, O_V, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0097 */ { UD_Imovupd, O_W, O_V, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0098 */ { UD_Imovlps, O_V, O_M, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0099 */ { UD_Imovddup, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0100 */ { UD_Imovsldup, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0101 */ { UD_Imovlpd, O_V, O_M, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0102 */ { UD_Imovhlps, O_V, O_U, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0103 */ { UD_Imovddup, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0104 */ { UD_Imovsldup, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0105 */ { UD_Imovlps, O_M, O_V, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0106 */ { UD_Imovlpd, O_M, O_V, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0107 */ { UD_Iunpcklps, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0108 */ { UD_Iunpcklpd, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0109 */ { UD_Iunpckhps, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0110 */ { UD_Iunpckhpd, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0111 */ { UD_Imovhps, O_V, O_M, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0112 */ { UD_Imovshdup, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0113 */ { UD_Imovhpd, O_V, O_M, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0114 */ { UD_Imovlhps, O_V, O_U, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0115 */ { UD_Imovshdup, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0116 */ { UD_Imovhps, O_M, O_V, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0117 */ { UD_Imovhpd, O_M, O_V, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0118 */ { UD_Iprefetchnta, O_M, O_NONE, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0119 */ { UD_Iprefetcht0, O_M, O_NONE, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0120 */ { UD_Iprefetcht1, O_M, O_NONE, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0121 */ { UD_Iprefetcht2, O_M, O_NONE, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0122 */ { UD_Inop, O_M, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0123 */ { UD_Inop, O_M, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0124 */ { UD_Inop, O_M, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0125 */ { UD_Inop, O_M, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0126 */ { UD_Inop, O_M, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0127 */ { UD_Inop, O_M, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0128 */ { UD_Inop, O_M, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0129 */ { UD_Imov, O_R, O_C, O_NONE, P_rexr|P_rexw|P_rexb },
  /* 0130 */ { UD_Imov, O_R, O_D, O_NONE, P_rexr|P_rexw|P_rexb },
  /* 0131 */ { UD_Imov, O_C, O_R, O_NONE, P_rexr|P_rexw|P_rexb },
  /* 0132 */ { UD_Imov, O_D, O_R, O_NONE, P_rexr|P_rexw|P_rexb },
  /* 0133 */ { UD_Imovaps, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0134 */ { UD_Imovapd, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0135 */ { UD_Imovaps, O_W, O_V, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0136 */ { UD_Imovapd, O_W, O_V, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0137 */ { UD_Icvtpi2ps, O_V, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0138 */ { UD_Icvtsi2sd, O_V, O_Ey, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0139 */ { UD_Icvtsi2ss, O_V, O_Ey, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0140 */ { UD_Icvtpi2pd, O_V, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0141 */ { UD_Imovntps, O_M, O_V, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0142 */ { UD_Imovntpd, O_M, O_V, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0143 */ { UD_Icvttps2pi, O_P, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0144 */ { UD_Icvttsd2si, O_Gy, O_W, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0145 */ { UD_Icvttss2si, O_Gy, O_W, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0146 */ { UD_Icvttpd2pi, O_P, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0147 */ { UD_Icvtps2pi, O_P, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0148 */ { UD_Icvtsd2si, O_Gy, O_W, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0149 */ { UD_Icvtss2si, O_Gy, O_W, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0150 */ { UD_Icvtpd2pi, O_P, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0151 */ { UD_Iucomiss, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0152 */ { UD_Iucomisd, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0153 */ { UD_Icomiss, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0154 */ { UD_Icomisd, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0155 */ { UD_Iwrmsr, O_NONE, O_NONE, O_NONE, P_none },
  /* 0156 */ { UD_Irdtsc, O_NONE, O_NONE, O_NONE, P_none },
  /* 0157 */ { UD_Irdmsr, O_NONE, O_NONE, O_NONE, P_none },
  /* 0158 */ { UD_Irdpmc, O_NONE, O_NONE, O_NONE, P_none },
  /* 0159 */ { UD_Isysenter, O_NONE, O_NONE, O_NONE, P_none },
  /* 0160 */ { UD_Isysenter, O_NONE, O_NONE, O_NONE, P_none },
  /* 0161 */ { UD_Isysexit, O_NONE, O_NONE, O_NONE, P_none },
  /* 0162 */ { UD_Isysexit, O_NONE, O_NONE, O_NONE, P_none },
  /* 0163 */ { UD_Igetsec, O_NONE, O_NONE, O_NONE, P_none },
  /* 0164 */ { UD_Ipshufb, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0165 */ { UD_Ipshufb, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0166 */ { UD_Iphaddw, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0167 */ { UD_Iphaddw, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0168 */ { UD_Iphaddd, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0169 */ { UD_Iphaddd, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0170 */ { UD_Iphaddsw, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0171 */ { UD_Iphaddsw, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0172 */ { UD_Ipmaddubsw, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0173 */ { UD_Ipmaddubsw, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0174 */ { UD_Iphsubw, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0175 */ { UD_Iphsubw, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0176 */ { UD_Iphsubd, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0177 */ { UD_Iphsubd, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0178 */ { UD_Iphsubsw, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0179 */ { UD_Iphsubsw, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0180 */ { UD_Ipsignb, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0181 */ { UD_Ipsignb, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0182 */ { UD_Ipsignw, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0183 */ { UD_Ipsignw, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0184 */ { UD_Ipsignd, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0185 */ { UD_Ipsignd, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0186 */ { UD_Ipmulhrsw, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0187 */ { UD_Ipmulhrsw, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0188 */ { UD_Ipblendvb, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0189 */ { UD_Iblendvps, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0190 */ { UD_Iblendvpd, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0191 */ { UD_Iptest, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexw|P_rexx|P_rexb },
  /* 0192 */ { UD_Ipabsb, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0193 */ { UD_Ipabsb, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0194 */ { UD_Ipabsw, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0195 */ { UD_Ipabsw, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0196 */ { UD_Ipabsd, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0197 */ { UD_Ipabsd, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0198 */ { UD_Ipmovsxbw, O_V, O_MqU, O_NONE, P_aso|P_rexr|P_rexw|P_rexx|P_rexb },
  /* 0199 */ { UD_Ipmovsxbd, O_V, O_MdU, O_NONE, P_aso|P_rexr|P_rexw|P_rexx|P_rexb },
  /* 0200 */ { UD_Ipmovsxbq, O_V, O_MwU, O_NONE, P_aso|P_rexr|P_rexw|P_rexx|P_rexb },
  /* 0201 */ { UD_Ipmovsxwd, O_V, O_MqU, O_NONE, P_aso|P_rexr|P_rexw|P_rexx|P_rexb },
  /* 0202 */ { UD_Ipmovsxwq, O_V, O_MdU, O_NONE, P_aso|P_rexr|P_rexw|P_rexx|P_rexb },
  /* 0203 */ { UD_Ipmovsxdq, O_V, O_MqU, O_NONE, P_aso|P_rexr|P_rexw|P_rexx|P_rexb },
  /* 0204 */ { UD_Ipmuldq, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0205 */ { UD_Ipcmpeqq, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexw|P_rexx|P_rexb },
  /* 0206 */ { UD_Imovntdqa, O_V, O_Mo, O_NONE, P_aso|P_rexr|P_rexw|P_rexx|P_rexb },
  /* 0207 */ { UD_Ipackusdw, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexw|P_rexx|P_rexb },
  /* 0208 */ { UD_Ipmovzxbw, O_V, O_MqU, O_NONE, P_aso|P_rexr|P_rexw|P_rexx|P_rexb },
  /* 0209 */ { UD_Ipmovzxbd, O_V, O_MdU, O_NONE, P_aso|P_rexr|P_rexw|P_rexx|P_rexb },
  /* 0210 */ { UD_Ipmovzxbq, O_V, O_MwU, O_NONE, P_aso|P_rexr|P_rexw|P_rexx|P_rexb },
  /* 0211 */ { UD_Ipmovzxwd, O_V, O_MqU, O_NONE, P_aso|P_rexr|P_rexw|P_rexx|P_rexb },
  /* 0212 */ { UD_Ipmovzxwq, O_V, O_MdU, O_NONE, P_aso|P_rexr|P_rexw|P_rexx|P_rexb },
  /* 0213 */ { UD_Ipmovzxdq, O_V, O_MqU, O_NONE, P_aso|P_rexr|P_rexw|P_rexx|P_rexb },
  /* 0214 */ { UD_Ipcmpgtq, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexw|P_rexx|P_rexb },
  /* 0215 */ { UD_Ipminsb, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0216 */ { UD_Ipminsd, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0217 */ { UD_Ipminuw, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0218 */ { UD_Ipminud, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0219 */ { UD_Ipmaxsb, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0220 */ { UD_Ipmaxsd, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0221 */ { UD_Ipmaxuw, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0222 */ { UD_Ipmaxud, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0223 */ { UD_Ipmulld, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0224 */ { UD_Iphminposuw, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0225 */ { UD_Iinvept, O_Gq, O_Mo, O_NONE, P_none },
  /* 0226 */ { UD_Iinvvpid, O_Gq, O_Mo, O_NONE, P_none },
  /* 0227 */ { UD_Iaesimc, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0228 */ { UD_Iaesenc, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0229 */ { UD_Iaesenclast, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0230 */ { UD_Iaesdec, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0231 */ { UD_Iaesdeclast, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0232 */ { UD_Imovbe, O_Gv, O_Mv, O_NONE, P_aso|P_oso|P_rexr|P_rexw|P_rexx|P_rexb },
  /* 0233 */ { UD_Icrc32, O_Gy, O_Eb, O_NONE, P_aso|P_oso|P_rexr|P_rexw|P_rexx|P_rexb },
  /* 0234 */ { UD_Imovbe, O_Mv, O_Gv, O_NONE, P_aso|P_oso|P_rexr|P_rexw|P_rexx|P_rexb },
  /* 0235 */ { UD_Icrc32, O_Gy, O_Ev, O_NONE, P_aso|P_oso|P_rexr|P_rexw|P_rexx|P_rexb },
  /* 0236 */ { UD_Iroundps, O_V, O_W, O_Ib, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0237 */ { UD_Iroundpd, O_V, O_W, O_Ib, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0238 */ { UD_Iroundss, O_V, O_W, O_Ib, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0239 */ { UD_Iroundsd, O_V, O_W, O_Ib, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0240 */ { UD_Iblendps, O_V, O_W, O_Ib, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0241 */ { UD_Iblendpd, O_V, O_W, O_Ib, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0242 */ { UD_Ipblendw, O_V, O_W, O_Ib, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0243 */ { UD_Ipalignr, O_P, O_Q, O_Ib, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0244 */ { UD_Ipalignr, O_V, O_W, O_Ib, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0245 */ { UD_Ipextrb, O_MbRv, O_V, O_Ib, P_aso|P_rexx|P_rexr|P_rexb|P_def64 },
  /* 0246 */ { UD_Ipextrw, O_MwRd, O_V, O_Ib, P_aso|P_rexx|P_rexr|P_rexb },
  /* 0247 */ { UD_Ipextrd, O_Ed, O_V, O_Ib, P_aso|P_rexr|P_rexx|P_rexw|P_rexb },
  /* 0248 */ { UD_Ipextrd, O_Ed, O_V, O_Ib, P_aso|P_rexr|P_rexx|P_rexw|P_rexb },
  /* 0249 */ { UD_Ipextrq, O_Eq, O_V, O_Ib, P_aso|P_rexr|P_rexw|P_rexb|P_def64 },
  /* 0250 */ { UD_Iextractps, O_MdRy, O_V, O_Ib, P_aso|P_rexr|P_rexw|P_rexx|P_rexb },
  /* 0251 */ { UD_Ipinsrb, O_V, O_MbRd, O_Ib, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0252 */ { UD_Iinsertps, O_V, O_Md, O_Ib, P_aso|P_rexr|P_rexw|P_rexx|P_rexb },
  /* 0253 */ { UD_Ipinsrd, O_V, O_Ed, O_Ib, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0254 */ { UD_Ipinsrd, O_V, O_Ed, O_Ib, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0255 */ { UD_Ipinsrq, O_V, O_Eq, O_Ib, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0256 */ { UD_Idpps, O_V, O_W, O_Ib, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0257 */ { UD_Idppd, O_V, O_W, O_Ib, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0258 */ { UD_Impsadbw, O_V, O_W, O_Ib, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0259 */ { UD_Ipclmulqdq, O_V, O_W, O_Ib, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0260 */ { UD_Ipcmpestrm, O_V, O_W, O_Ib, P_aso|P_rexr|P_rexw|P_rexx|P_rexb },
  /* 0261 */ { UD_Ipcmpestri, O_V, O_W, O_Ib, P_aso|P_rexr|P_rexw|P_rexx|P_rexb },
  /* 0262 */ { UD_Ipcmpistrm, O_V, O_W, O_Ib, P_aso|P_rexr|P_rexw|P_rexx|P_rexb },
  /* 0263 */ { UD_Ipcmpistri, O_V, O_W, O_Ib, P_aso|P_rexr|P_rexw|P_rexx|P_rexb },
  /* 0264 */ { UD_Iaeskeygenassist, O_V, O_W, O_Ib, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0265 */ { UD_Icmovo, O_Gv, O_Ev, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0266 */ { UD_Icmovno, O_Gv, O_Ev, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0267 */ { UD_Icmovb, O_Gv, O_Ev, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0268 */ { UD_Icmovae, O_Gv, O_Ev, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0269 */ { UD_Icmovz, O_Gv, O_Ev, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0270 */ { UD_Icmovnz, O_Gv, O_Ev, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0271 */ { UD_Icmovbe, O_Gv, O_Ev, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0272 */ { UD_Icmova, O_Gv, O_Ev, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0273 */ { UD_Icmovs, O_Gv, O_Ev, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0274 */ { UD_Icmovns, O_Gv, O_Ev, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0275 */ { UD_Icmovp, O_Gv, O_Ev, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0276 */ { UD_Icmovnp, O_Gv, O_Ev, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0277 */ { UD_Icmovl, O_Gv, O_Ev, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0278 */ { UD_Icmovge, O_Gv, O_Ev, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0279 */ { UD_Icmovle, O_Gv, O_Ev, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0280 */ { UD_Icmovg, O_Gv, O_Ev, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0281 */ { UD_Imovmskps, O_Gd, O_U, O_NONE, P_oso|P_rexr|P_rexb },
  /* 0282 */ { UD_Imovmskpd, O_Gd, O_U, O_NONE, P_oso|P_rexr|P_rexb },
  /* 0283 */ { UD_Isqrtps, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0284 */ { UD_Isqrtsd, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0285 */ { UD_Isqrtss, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0286 */ { UD_Isqrtpd, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0287 */ { UD_Irsqrtps, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0288 */ { UD_Irsqrtss, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0289 */ { UD_Ircpps, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0290 */ { UD_Ircpss, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0291 */ { UD_Iandps, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0292 */ { UD_Iandpd, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0293 */ { UD_Iandnps, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0294 */ { UD_Iandnpd, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0295 */ { UD_Iorps, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0296 */ { UD_Iorpd, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0297 */ { UD_Ixorps, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0298 */ { UD_Ixorpd, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0299 */ { UD_Iaddps, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0300 */ { UD_Iaddsd, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0301 */ { UD_Iaddss, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0302 */ { UD_Iaddpd, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0303 */ { UD_Imulps, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0304 */ { UD_Imulsd, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0305 */ { UD_Imulss, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0306 */ { UD_Imulpd, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0307 */ { UD_Icvtps2pd, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0308 */ { UD_Icvtsd2ss, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0309 */ { UD_Icvtss2sd, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0310 */ { UD_Icvtpd2ps, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0311 */ { UD_Icvtdq2ps, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0312 */ { UD_Icvttps2dq, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0313 */ { UD_Icvtps2dq, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0314 */ { UD_Isubps, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0315 */ { UD_Isubsd, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0316 */ { UD_Isubss, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0317 */ { UD_Isubpd, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0318 */ { UD_Iminps, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0319 */ { UD_Iminsd, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0320 */ { UD_Iminss, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0321 */ { UD_Iminpd, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0322 */ { UD_Idivps, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0323 */ { UD_Idivsd, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0324 */ { UD_Idivss, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0325 */ { UD_Idivpd, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0326 */ { UD_Imaxps, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0327 */ { UD_Imaxsd, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0328 */ { UD_Imaxss, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0329 */ { UD_Imaxpd, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0330 */ { UD_Ipunpcklbw, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0331 */ { UD_Ipunpcklbw, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0332 */ { UD_Ipunpcklwd, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0333 */ { UD_Ipunpcklwd, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0334 */ { UD_Ipunpckldq, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0335 */ { UD_Ipunpckldq, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0336 */ { UD_Ipacksswb, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0337 */ { UD_Ipacksswb, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0338 */ { UD_Ipcmpgtb, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0339 */ { UD_Ipcmpgtb, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0340 */ { UD_Ipcmpgtw, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0341 */ { UD_Ipcmpgtw, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0342 */ { UD_Ipcmpgtd, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0343 */ { UD_Ipcmpgtd, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0344 */ { UD_Ipackuswb, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0345 */ { UD_Ipackuswb, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0346 */ { UD_Ipunpckhbw, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0347 */ { UD_Ipunpckhbw, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0348 */ { UD_Ipunpckhwd, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0349 */ { UD_Ipunpckhwd, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0350 */ { UD_Ipunpckhdq, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0351 */ { UD_Ipunpckhdq, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0352 */ { UD_Ipackssdw, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0353 */ { UD_Ipackssdw, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0354 */ { UD_Ipunpcklqdq, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0355 */ { UD_Ipunpckhqdq, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0356 */ { UD_Imovd, O_P, O_Ey, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0357 */ { UD_Imovd, O_V, O_Ey, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0358 */ { UD_Imovq, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0359 */ { UD_Imovdqu, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0360 */ { UD_Imovdqa, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0361 */ { UD_Ipshufw, O_P, O_Q, O_Ib, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0362 */ { UD_Ipshuflw, O_V, O_W, O_Ib, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0363 */ { UD_Ipshufhw, O_V, O_W, O_Ib, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0364 */ { UD_Ipshufd, O_V, O_W, O_Ib, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0365 */ { UD_Ipsrlw, O_N, O_Ib, O_NONE, P_none },
  /* 0366 */ { UD_Ipsrlw, O_U, O_Ib, O_NONE, P_rexb },
  /* 0367 */ { UD_Ipsraw, O_N, O_Ib, O_NONE, P_none },
  /* 0368 */ { UD_Ipsraw, O_U, O_Ib, O_NONE, P_rexb },
  /* 0369 */ { UD_Ipsllw, O_N, O_Ib, O_NONE, P_none },
  /* 0370 */ { UD_Ipsllw, O_U, O_Ib, O_NONE, P_rexb },
  /* 0371 */ { UD_Ipsrld, O_N, O_Ib, O_NONE, P_none },
  /* 0372 */ { UD_Ipsrld, O_U, O_Ib, O_NONE, P_rexb },
  /* 0373 */ { UD_Ipsrad, O_N, O_Ib, O_NONE, P_none },
  /* 0374 */ { UD_Ipsrad, O_U, O_Ib, O_NONE, P_rexb },
  /* 0375 */ { UD_Ipslld, O_N, O_Ib, O_NONE, P_none },
  /* 0376 */ { UD_Ipslld, O_U, O_Ib, O_NONE, P_rexb },
  /* 0377 */ { UD_Ipsrlq, O_N, O_Ib, O_NONE, P_none },
  /* 0378 */ { UD_Ipsrlq, O_U, O_Ib, O_NONE, P_rexb },
  /* 0379 */ { UD_Ipsrldq, O_U, O_Ib, O_NONE, P_rexb },
  /* 0380 */ { UD_Ipsllq, O_N, O_Ib, O_NONE, P_none },
  /* 0381 */ { UD_Ipsllq, O_U, O_Ib, O_NONE, P_rexb },
  /* 0382 */ { UD_Ipslldq, O_U, O_Ib, O_NONE, P_rexb },
  /* 0383 */ { UD_Ipcmpeqb, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0384 */ { UD_Ipcmpeqb, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0385 */ { UD_Ipcmpeqw, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0386 */ { UD_Ipcmpeqw, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0387 */ { UD_Ipcmpeqd, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0388 */ { UD_Ipcmpeqd, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0389 */ { UD_Iemms, O_NONE, O_NONE, O_NONE, P_none },
  /* 0390 */ { UD_Ivmread, O_Ey, O_Gy, O_NONE, P_aso|P_rexr|P_rexx|P_rexb|P_def64 },
  /* 0391 */ { UD_Ivmwrite, O_Gy, O_Ey, O_NONE, P_aso|P_rexr|P_rexx|P_rexb|P_def64 },
  /* 0392 */ { UD_Ihaddps, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0393 */ { UD_Ihaddpd, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0394 */ { UD_Ihsubps, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0395 */ { UD_Ihsubpd, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0396 */ { UD_Imovd, O_Ey, O_P, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0397 */ { UD_Imovq, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0398 */ { UD_Imovd, O_Ey, O_V, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0399 */ { UD_Imovq, O_Q, O_P, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0400 */ { UD_Imovdqu, O_W, O_V, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0401 */ { UD_Imovdqa, O_W, O_V, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0402 */ { UD_Ijo, O_Jz, O_NONE, O_NONE, P_oso|P_def64 },
  /* 0403 */ { UD_Ijno, O_Jz, O_NONE, O_NONE, P_oso|P_def64 },
  /* 0404 */ { UD_Ijb, O_Jz, O_NONE, O_NONE, P_oso|P_def64 },
  /* 0405 */ { UD_Ijae, O_Jz, O_NONE, O_NONE, P_oso|P_def64 },
  /* 0406 */ { UD_Ijz, O_Jz, O_NONE, O_NONE, P_oso|P_def64 },
  /* 0407 */ { UD_Ijnz, O_Jz, O_NONE, O_NONE, P_oso|P_def64 },
  /* 0408 */ { UD_Ijbe, O_Jz, O_NONE, O_NONE, P_oso|P_def64 },
  /* 0409 */ { UD_Ija, O_Jz, O_NONE, O_NONE, P_oso|P_def64 },
  /* 0410 */ { UD_Ijs, O_Jz, O_NONE, O_NONE, P_oso|P_def64 },
  /* 0411 */ { UD_Ijns, O_Jz, O_NONE, O_NONE, P_oso|P_def64 },
  /* 0412 */ { UD_Ijp, O_Jz, O_NONE, O_NONE, P_oso|P_def64 },
  /* 0413 */ { UD_Ijnp, O_Jz, O_NONE, O_NONE, P_oso|P_def64 },
  /* 0414 */ { UD_Ijl, O_Jz, O_NONE, O_NONE, P_oso|P_def64 },
  /* 0415 */ { UD_Ijge, O_Jz, O_NONE, O_NONE, P_oso|P_def64 },
  /* 0416 */ { UD_Ijle, O_Jz, O_NONE, O_NONE, P_oso|P_def64 },
  /* 0417 */ { UD_Ijg, O_Jz, O_NONE, O_NONE, P_oso|P_def64 },
  /* 0418 */ { UD_Iseto, O_Eb, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0419 */ { UD_Isetno, O_Eb, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0420 */ { UD_Isetb, O_Eb, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0421 */ { UD_Isetae, O_Eb, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0422 */ { UD_Isetz, O_Eb, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0423 */ { UD_Isetnz, O_Eb, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0424 */ { UD_Isetbe, O_Eb, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0425 */ { UD_Iseta, O_Eb, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0426 */ { UD_Isets, O_Eb, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0427 */ { UD_Isetns, O_Eb, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0428 */ { UD_Isetp, O_Eb, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0429 */ { UD_Isetnp, O_Eb, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0430 */ { UD_Isetl, O_Eb, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0431 */ { UD_Isetge, O_Eb, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0432 */ { UD_Isetle, O_Eb, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0433 */ { UD_Isetg, O_Eb, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0434 */ { UD_Ipush, O_FS, O_NONE, O_NONE, P_none },
  /* 0435 */ { UD_Ipop, O_FS, O_NONE, O_NONE, P_none },
  /* 0436 */ { UD_Icpuid, O_NONE, O_NONE, O_NONE, P_none },
  /* 0437 */ { UD_Ibt, O_Ev, O_Gv, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0438 */ { UD_Ishld, O_Ev, O_Gv, O_Ib, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0439 */ { UD_Ishld, O_Ev, O_Gv, O_CL, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0440 */ { UD_Imontmul, O_NONE, O_NONE, O_NONE, P_none },
  /* 0441 */ { UD_Ixsha1, O_NONE, O_NONE, O_NONE, P_none },
  /* 0442 */ { UD_Ixsha256, O_NONE, O_NONE, O_NONE, P_none },
  /* 0443 */ { UD_Ixstore, O_NONE, O_NONE, O_NONE, P_none },
  /* 0444 */ { UD_Ixcryptecb, O_NONE, O_NONE, O_NONE, P_none },
  /* 0445 */ { UD_Ixcryptcbc, O_NONE, O_NONE, O_NONE, P_none },
  /* 0446 */ { UD_Ixcryptctr, O_NONE, O_NONE, O_NONE, P_none },
  /* 0447 */ { UD_Ixcryptcfb, O_NONE, O_NONE, O_NONE, P_none },
  /* 0448 */ { UD_Ixcryptofb, O_NONE, O_NONE, O_NONE, P_none },
  /* 0449 */ { UD_Ipush, O_GS, O_NONE, O_NONE, P_none },
  /* 0450 */ { UD_Ipop, O_GS, O_NONE, O_NONE, P_none },
  /* 0451 */ { UD_Irsm, O_NONE, O_NONE, O_NONE, P_none },
  /* 0452 */ { UD_Ibts, O_Ev, O_Gv, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0453 */ { UD_Ishrd, O_Ev, O_Gv, O_Ib, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0454 */ { UD_Ishrd, O_Ev, O_Gv, O_CL, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0455 */ { UD_Ifxsave, O_M, O_NONE, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0456 */ { UD_Ifxrstor, O_M, O_NONE, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0457 */ { UD_Ildmxcsr, O_Md, O_NONE, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0458 */ { UD_Istmxcsr, O_Md, O_NONE, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0459 */ { UD_Ixsave, O_M, O_NONE, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0460 */ { UD_Ixrstor, O_M, O_NONE, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0461 */ { UD_Iclflush, O_M, O_NONE, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0462 */ { UD_Ilfence, O_NONE, O_NONE, O_NONE, P_none },
  /* 0463 */ { UD_Ilfence, O_NONE, O_NONE, O_NONE, P_none },
  /* 0464 */ { UD_Ilfence, O_NONE, O_NONE, O_NONE, P_none },
  /* 0465 */ { UD_Ilfence, O_NONE, O_NONE, O_NONE, P_none },
  /* 0466 */ { UD_Ilfence, O_NONE, O_NONE, O_NONE, P_none },
  /* 0467 */ { UD_Ilfence, O_NONE, O_NONE, O_NONE, P_none },
  /* 0468 */ { UD_Ilfence, O_NONE, O_NONE, O_NONE, P_none },
  /* 0469 */ { UD_Ilfence, O_NONE, O_NONE, O_NONE, P_none },
  /* 0470 */ { UD_Imfence, O_NONE, O_NONE, O_NONE, P_none },
  /* 0471 */ { UD_Imfence, O_NONE, O_NONE, O_NONE, P_none },
  /* 0472 */ { UD_Imfence, O_NONE, O_NONE, O_NONE, P_none },
  /* 0473 */ { UD_Imfence, O_NONE, O_NONE, O_NONE, P_none },
  /* 0474 */ { UD_Imfence, O_NONE, O_NONE, O_NONE, P_none },
  /* 0475 */ { UD_Imfence, O_NONE, O_NONE, O_NONE, P_none },
  /* 0476 */ { UD_Imfence, O_NONE, O_NONE, O_NONE, P_none },
  /* 0477 */ { UD_Imfence, O_NONE, O_NONE, O_NONE, P_none },
  /* 0478 */ { UD_Isfence, O_NONE, O_NONE, O_NONE, P_none },
  /* 0479 */ { UD_Isfence, O_NONE, O_NONE, O_NONE, P_none },
  /* 0480 */ { UD_Isfence, O_NONE, O_NONE, O_NONE, P_none },
  /* 0481 */ { UD_Isfence, O_NONE, O_NONE, O_NONE, P_none },
  /* 0482 */ { UD_Isfence, O_NONE, O_NONE, O_NONE, P_none },
  /* 0483 */ { UD_Isfence, O_NONE, O_NONE, O_NONE, P_none },
  /* 0484 */ { UD_Isfence, O_NONE, O_NONE, O_NONE, P_none },
  /* 0485 */ { UD_Isfence, O_NONE, O_NONE, O_NONE, P_none },
  /* 0486 */ { UD_Iimul, O_Gv, O_Ev, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0487 */ { UD_Icmpxchg, O_Eb, O_Gb, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0488 */ { UD_Icmpxchg, O_Ev, O_Gv, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0489 */ { UD_Ilss, O_Gv, O_M, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0490 */ { UD_Ibtr, O_Ev, O_Gv, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0491 */ { UD_Ilfs, O_Gz, O_M, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0492 */ { UD_Ilgs, O_Gz, O_M, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0493 */ { UD_Imovzx, O_Gv, O_Eb, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0494 */ { UD_Imovzx, O_Gy, O_Ew, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0495 */ { UD_Ipopcnt, O_Gv, O_Ev, O_NONE, P_aso|P_oso|P_rexr|P_rexw|P_rexx|P_rexb },
  /* 0496 */ { UD_Ibt, O_Ev, O_Ib, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0497 */ { UD_Ibts, O_Ev, O_Ib, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0498 */ { UD_Ibtr, O_Ev, O_Ib, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0499 */ { UD_Ibtc, O_Ev, O_Ib, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0500 */ { UD_Ibtc, O_Ev, O_Gv, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0501 */ { UD_Ibsf, O_Gv, O_Ev, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0502 */ { UD_Ibsr, O_Gv, O_Ev, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0503 */ { UD_Imovsx, O_Gv, O_Eb, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0504 */ { UD_Imovsx, O_Gy, O_Ew, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0505 */ { UD_Ixadd, O_Eb, O_Gb, O_NONE, P_aso|P_oso|P_rexr|P_rexx|P_rexb },
  /* 0506 */ { UD_Ixadd, O_Ev, O_Gv, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0507 */ { UD_Icmpps, O_V, O_W, O_Ib, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0508 */ { UD_Icmpsd, O_V, O_W, O_Ib, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0509 */ { UD_Icmpss, O_V, O_W, O_Ib, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0510 */ { UD_Icmppd, O_V, O_W, O_Ib, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0511 */ { UD_Imovnti, O_M, O_Gy, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0512 */ { UD_Ipinsrw, O_P, O_MwRy, O_Ib, P_aso|P_rexw|P_rexr|P_rexx|P_rexb|P_def64 },
  /* 0513 */ { UD_Ipinsrw, O_V, O_MwRy, O_Ib, P_aso|P_rexw|P_rexr|P_rexx|P_rexb|P_def64 },
  /* 0514 */ { UD_Ipextrw, O_Gd, O_N, O_Ib, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0515 */ { UD_Ipextrw, O_Gd, O_U, O_Ib, P_aso|P_rexr|P_rexb },
  /* 0516 */ { UD_Ishufps, O_V, O_W, O_Ib, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0517 */ { UD_Ishufpd, O_V, O_W, O_Ib, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0518 */ { UD_Icmpxchg8b, O_M, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0519 */ { UD_Icmpxchg8b, O_M, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0520 */ { UD_Icmpxchg16b, O_M, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0521 */ { UD_Ivmptrld, O_Mq, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0522 */ { UD_Ivmxon, O_Mq, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0523 */ { UD_Ivmclear, O_Mq, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0524 */ { UD_Ivmptrst, O_Mq, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0525 */ { UD_Ibswap, O_R0y, O_NONE, O_NONE, P_oso|P_rexw|P_rexb },
  /* 0526 */ { UD_Ibswap, O_R1y, O_NONE, O_NONE, P_oso|P_rexw|P_rexb },
  /* 0527 */ { UD_Ibswap, O_R2y, O_NONE, O_NONE, P_oso|P_rexw|P_rexb },
  /* 0528 */ { UD_Ibswap, O_R3y, O_NONE, O_NONE, P_oso|P_rexw|P_rexb },
  /* 0529 */ { UD_Ibswap, O_R4y, O_NONE, O_NONE, P_oso|P_rexw|P_rexb },
  /* 0530 */ { UD_Ibswap, O_R5y, O_NONE, O_NONE, P_oso|P_rexw|P_rexb },
  /* 0531 */ { UD_Ibswap, O_R6y, O_NONE, O_NONE, P_oso|P_rexw|P_rexb },
  /* 0532 */ { UD_Ibswap, O_R7y, O_NONE, O_NONE, P_oso|P_rexw|P_rexb },
  /* 0533 */ { UD_Iaddsubps, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0534 */ { UD_Iaddsubpd, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0535 */ { UD_Ipsrlw, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0536 */ { UD_Ipsrlw, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0537 */ { UD_Ipsrld, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0538 */ { UD_Ipsrld, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0539 */ { UD_Ipsrlq, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0540 */ { UD_Ipsrlq, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0541 */ { UD_Ipaddq, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0542 */ { UD_Ipaddq, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0543 */ { UD_Ipmullw, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0544 */ { UD_Ipmullw, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0545 */ { UD_Imovdq2q, O_P, O_U, O_NONE, P_aso|P_rexb },
  /* 0546 */ { UD_Imovq2dq, O_V, O_N, O_NONE, P_aso|P_rexr },
  /* 0547 */ { UD_Imovq, O_W, O_V, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0548 */ { UD_Ipmovmskb, O_Gd, O_N, O_NONE, P_oso|P_rexr|P_rexb },
  /* 0549 */ { UD_Ipmovmskb, O_Gd, O_U, O_NONE, P_rexr|P_rexb },
  /* 0550 */ { UD_Ipsubusb, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0551 */ { UD_Ipsubusb, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0552 */ { UD_Ipsubusw, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0553 */ { UD_Ipsubusw, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0554 */ { UD_Ipminub, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0555 */ { UD_Ipminub, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0556 */ { UD_Ipand, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0557 */ { UD_Ipand, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0558 */ { UD_Ipaddusb, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0559 */ { UD_Ipaddusb, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0560 */ { UD_Ipaddusw, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0561 */ { UD_Ipaddusw, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0562 */ { UD_Ipmaxub, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0563 */ { UD_Ipmaxub, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0564 */ { UD_Ipandn, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0565 */ { UD_Ipandn, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0566 */ { UD_Ipavgb, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0567 */ { UD_Ipavgb, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0568 */ { UD_Ipsraw, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0569 */ { UD_Ipsraw, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0570 */ { UD_Ipsrad, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0571 */ { UD_Ipsrad, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0572 */ { UD_Ipavgw, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0573 */ { UD_Ipavgw, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0574 */ { UD_Ipmulhuw, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0575 */ { UD_Ipmulhuw, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0576 */ { UD_Ipmulhw, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0577 */ { UD_Ipmulhw, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0578 */ { UD_Icvtpd2dq, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0579 */ { UD_Icvtdq2pd, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0580 */ { UD_Icvttpd2dq, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0581 */ { UD_Imovntq, O_M, O_P, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0582 */ { UD_Imovntdq, O_M, O_V, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0583 */ { UD_Ipsubsb, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0584 */ { UD_Ipsubsb, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0585 */ { UD_Ipsubsw, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0586 */ { UD_Ipsubsw, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0587 */ { UD_Ipminsw, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0588 */ { UD_Ipminsw, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0589 */ { UD_Ipor, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0590 */ { UD_Ipor, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0591 */ { UD_Ipaddsb, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0592 */ { UD_Ipaddsb, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0593 */ { UD_Ipaddsw, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0594 */ { UD_Ipaddsw, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0595 */ { UD_Ipmaxsw, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0596 */ { UD_Ipmaxsw, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0597 */ { UD_Ipxor, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0598 */ { UD_Ipxor, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0599 */ { UD_Ilddqu, O_V, O_M, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0600 */ { UD_Ipsllw, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0601 */ { UD_Ipsllw, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0602 */ { UD_Ipslld, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0603 */ { UD_Ipslld, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0604 */ { UD_Ipsllq, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0605 */ { UD_Ipsllq, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0606 */ { UD_Ipmuludq, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0607 */ { UD_Ipmuludq, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0608 */ { UD_Ipmaddwd, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0609 */ { UD_Ipmaddwd, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0610 */ { UD_Ipsadbw, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0611 */ { UD_Ipsadbw, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0612 */ { UD_Imaskmovq, O_P, O_N, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0613 */ { UD_Imaskmovdqu, O_V, O_U, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0614 */ { UD_Ipsubb, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0615 */ { UD_Ipsubb, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0616 */ { UD_Ipsubw, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0617 */ { UD_Ipsubw, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0618 */ { UD_Ipsubd, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0619 */ { UD_Ipsubd, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0620 */ { UD_Ipsubq, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0621 */ { UD_Ipsubq, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0622 */ { UD_Ipaddb, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0623 */ { UD_Ipaddb, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0624 */ { UD_Ipaddw, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0625 */ { UD_Ipaddw, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0626 */ { UD_Ipaddd, O_P, O_Q, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0627 */ { UD_Ipaddd, O_V, O_W, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0628 */ { UD_Iadc, O_Eb, O_Gb, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0629 */ { UD_Iadc, O_Ev, O_Gv, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0630 */ { UD_Iadc, O_Gb, O_Eb, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0631 */ { UD_Iadc, O_Gv, O_Ev, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0632 */ { UD_Iadc, O_AL, O_Ib, O_NONE, P_none },
  /* 0633 */ { UD_Iadc, O_rAX, O_sIz, O_NONE, P_oso|P_rexw },
  /* 0634 */ { UD_Ipush, O_SS, O_NONE, O_NONE, P_inv64 },
  /* 0635 */ { UD_Ipop, O_SS, O_NONE, O_NONE, P_inv64 },
  /* 0636 */ { UD_Isbb, O_Eb, O_Gb, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0637 */ { UD_Isbb, O_Ev, O_Gv, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0638 */ { UD_Isbb, O_Gb, O_Eb, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0639 */ { UD_Isbb, O_Gv, O_Ev, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0640 */ { UD_Isbb, O_AL, O_Ib, O_NONE, P_none },
  /* 0641 */ { UD_Isbb, O_rAX, O_sIz, O_NONE, P_oso|P_rexw },
  /* 0642 */ { UD_Ipush, O_DS, O_NONE, O_NONE, P_inv64 },
  /* 0643 */ { UD_Ipop, O_DS, O_NONE, O_NONE, P_inv64 },
  /* 0644 */ { UD_Iand, O_Eb, O_Gb, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0645 */ { UD_Iand, O_Ev, O_Gv, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0646 */ { UD_Iand, O_Gb, O_Eb, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0647 */ { UD_Iand, O_Gv, O_Ev, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0648 */ { UD_Iand, O_AL, O_Ib, O_NONE, P_none },
  /* 0649 */ { UD_Iand, O_rAX, O_sIz, O_NONE, P_oso|P_rexw },
  /* 0650 */ { UD_Idaa, O_NONE, O_NONE, O_NONE, P_inv64 },
  /* 0651 */ { UD_Isub, O_Eb, O_Gb, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0652 */ { UD_Isub, O_Ev, O_Gv, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0653 */ { UD_Isub, O_Gb, O_Eb, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0654 */ { UD_Isub, O_Gv, O_Ev, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0655 */ { UD_Isub, O_AL, O_Ib, O_NONE, P_none },
  /* 0656 */ { UD_Isub, O_rAX, O_sIz, O_NONE, P_oso|P_rexw },
  /* 0657 */ { UD_Idas, O_NONE, O_NONE, O_NONE, P_inv64 },
  /* 0658 */ { UD_Ixor, O_Eb, O_Gb, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0659 */ { UD_Ixor, O_Ev, O_Gv, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0660 */ { UD_Ixor, O_Gb, O_Eb, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0661 */ { UD_Ixor, O_Gv, O_Ev, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0662 */ { UD_Ixor, O_AL, O_Ib, O_NONE, P_none },
  /* 0663 */ { UD_Ixor, O_rAX, O_sIz, O_NONE, P_oso|P_rexw },
  /* 0664 */ { UD_Iaaa, O_NONE, O_NONE, O_NONE, P_none },
  /* 0665 */ { UD_Icmp, O_Eb, O_Gb, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0666 */ { UD_Icmp, O_Ev, O_Gv, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0667 */ { UD_Icmp, O_Gb, O_Eb, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0668 */ { UD_Icmp, O_Gv, O_Ev, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0669 */ { UD_Icmp, O_AL, O_Ib, O_NONE, P_none },
  /* 0670 */ { UD_Icmp, O_rAX, O_sIz, O_NONE, P_oso|P_rexw },
  /* 0671 */ { UD_Iaas, O_NONE, O_NONE, O_NONE, P_none },
  /* 0672 */ { UD_Iinc, O_R0z, O_NONE, O_NONE, P_oso },
  /* 0673 */ { UD_Iinc, O_R1z, O_NONE, O_NONE, P_oso },
  /* 0674 */ { UD_Iinc, O_R2z, O_NONE, O_NONE, P_oso },
  /* 0675 */ { UD_Iinc, O_R3z, O_NONE, O_NONE, P_oso },
  /* 0676 */ { UD_Iinc, O_R4z, O_NONE, O_NONE, P_oso },
  /* 0677 */ { UD_Iinc, O_R5z, O_NONE, O_NONE, P_oso },
  /* 0678 */ { UD_Iinc, O_R6z, O_NONE, O_NONE, P_oso },
  /* 0679 */ { UD_Iinc, O_R7z, O_NONE, O_NONE, P_oso },
  /* 0680 */ { UD_Idec, O_R0z, O_NONE, O_NONE, P_oso },
  /* 0681 */ { UD_Idec, O_R1z, O_NONE, O_NONE, P_oso },
  /* 0682 */ { UD_Idec, O_R2z, O_NONE, O_NONE, P_oso },
  /* 0683 */ { UD_Idec, O_R3z, O_NONE, O_NONE, P_oso },
  /* 0684 */ { UD_Idec, O_R4z, O_NONE, O_NONE, P_oso },
  /* 0685 */ { UD_Idec, O_R5z, O_NONE, O_NONE, P_oso },
  /* 0686 */ { UD_Idec, O_R6z, O_NONE, O_NONE, P_oso },
  /* 0687 */ { UD_Idec, O_R7z, O_NONE, O_NONE, P_oso },
  /* 0688 */ { UD_Ipush, O_R0v, O_NONE, O_NONE, P_oso|P_rexb|P_def64 },
  /* 0689 */ { UD_Ipush, O_R1v, O_NONE, O_NONE, P_oso|P_rexb|P_def64 },
  /* 0690 */ { UD_Ipush, O_R2v, O_NONE, O_NONE, P_oso|P_rexb|P_def64 },
  /* 0691 */ { UD_Ipush, O_R3v, O_NONE, O_NONE, P_oso|P_rexb|P_def64 },
  /* 0692 */ { UD_Ipush, O_R4v, O_NONE, O_NONE, P_oso|P_rexb|P_def64 },
  /* 0693 */ { UD_Ipush, O_R5v, O_NONE, O_NONE, P_oso|P_rexb|P_def64 },
  /* 0694 */ { UD_Ipush, O_R6v, O_NONE, O_NONE, P_oso|P_rexb|P_def64 },
  /* 0695 */ { UD_Ipush, O_R7v, O_NONE, O_NONE, P_oso|P_rexb|P_def64 },
  /* 0696 */ { UD_Ipop, O_R0v, O_NONE, O_NONE, P_oso|P_rexb|P_def64 },
  /* 0697 */ { UD_Ipop, O_R1v, O_NONE, O_NONE, P_oso|P_rexb|P_def64 },
  /* 0698 */ { UD_Ipop, O_R2v, O_NONE, O_NONE, P_oso|P_rexb|P_def64 },
  /* 0699 */ { UD_Ipop, O_R3v, O_NONE, O_NONE, P_oso|P_rexb|P_def64 },
  /* 0700 */ { UD_Ipop, O_R4v, O_NONE, O_NONE, P_oso|P_rexb|P_def64 },
  /* 0701 */ { UD_Ipop, O_R5v, O_NONE, O_NONE, P_oso|P_rexb|P_def64 },
  /* 0702 */ { UD_Ipop, O_R6v, O_NONE, O_NONE, P_oso|P_rexb|P_def64 },
  /* 0703 */ { UD_Ipop, O_R7v, O_NONE, O_NONE, P_oso|P_rexb|P_def64 },
  /* 0704 */ { UD_Ipusha, O_NONE, O_NONE, O_NONE, P_oso|P_inv64 },
  /* 0705 */ { UD_Ipushad, O_NONE, O_NONE, O_NONE, P_oso|P_inv64 },
  /* 0706 */ { UD_Ipopa, O_NONE, O_NONE, O_NONE, P_oso|P_inv64 },
  /* 0707 */ { UD_Ipopad, O_NONE, O_NONE, O_NONE, P_oso|P_inv64 },
  /* 0708 */ { UD_Ibound, O_Gv, O_M, O_NONE, P_aso|P_oso },
  /* 0709 */ { UD_Iarpl, O_Ew, O_Gw, O_NONE, P_aso },
  /* 0710 */ { UD_Imovsxd, O_Gq, O_Ed, O_NONE, P_aso|P_oso|P_rexw|P_rexx|P_rexr|P_rexb },
  /* 0711 */ { UD_Ipush, O_sIz, O_NONE, O_NONE, P_oso|P_def64 },
  /* 0712 */ { UD_Iimul, O_Gv, O_Ev, O_Iz, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0713 */ { UD_Ipush, O_sIb, O_NONE, O_NONE, P_oso|P_def64 },
  /* 0714 */ { UD_Iimul, O_Gv, O_Ev, O_sIb, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0715 */ { UD_Iinsb, O_NONE, O_NONE, O_NONE, P_str|P_seg },
  /* 0716 */ { UD_Iinsw, O_NONE, O_NONE, O_NONE, P_str|P_oso|P_seg },
  /* 0717 */ { UD_Iinsd, O_NONE, O_NONE, O_NONE, P_str|P_oso|P_seg },
  /* 0718 */ { UD_Ioutsb, O_NONE, O_NONE, O_NONE, P_str|P_seg },
  /* 0719 */ { UD_Ioutsw, O_NONE, O_NONE, O_NONE, P_str|P_oso|P_seg },
  /* 0720 */ { UD_Ioutsd, O_NONE, O_NONE, O_NONE, P_str|P_oso|P_seg },
  /* 0721 */ { UD_Ijo, O_Jb, O_NONE, O_NONE, P_none },
  /* 0722 */ { UD_Ijno, O_Jb, O_NONE, O_NONE, P_none },
  /* 0723 */ { UD_Ijb, O_Jb, O_NONE, O_NONE, P_none },
  /* 0724 */ { UD_Ijae, O_Jb, O_NONE, O_NONE, P_none },
  /* 0725 */ { UD_Ijz, O_Jb, O_NONE, O_NONE, P_none },
  /* 0726 */ { UD_Ijnz, O_Jb, O_NONE, O_NONE, P_none },
  /* 0727 */ { UD_Ijbe, O_Jb, O_NONE, O_NONE, P_none },
  /* 0728 */ { UD_Ija, O_Jb, O_NONE, O_NONE, P_none },
  /* 0729 */ { UD_Ijs, O_Jb, O_NONE, O_NONE, P_none },
  /* 0730 */ { UD_Ijns, O_Jb, O_NONE, O_NONE, P_none },
  /* 0731 */ { UD_Ijp, O_Jb, O_NONE, O_NONE, P_none },
  /* 0732 */ { UD_Ijnp, O_Jb, O_NONE, O_NONE, P_none },
  /* 0733 */ { UD_Ijl, O_Jb, O_NONE, O_NONE, P_none },
  /* 0734 */ { UD_Ijge, O_Jb, O_NONE, O_NONE, P_none },
  /* 0735 */ { UD_Ijle, O_Jb, O_NONE, O_NONE, P_none },
  /* 0736 */ { UD_Ijg, O_Jb, O_NONE, O_NONE, P_none },
  /* 0737 */ { UD_Iadd, O_Eb, O_Ib, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0738 */ { UD_Ior, O_Eb, O_Ib, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0739 */ { UD_Iadc, O_Eb, O_Ib, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0740 */ { UD_Isbb, O_Eb, O_Ib, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0741 */ { UD_Iand, O_Eb, O_Ib, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0742 */ { UD_Isub, O_Eb, O_Ib, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0743 */ { UD_Ixor, O_Eb, O_Ib, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0744 */ { UD_Icmp, O_Eb, O_Ib, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0745 */ { UD_Iadd, O_Ev, O_sIz, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0746 */ { UD_Ior, O_Ev, O_sIz, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0747 */ { UD_Iadc, O_Ev, O_sIz, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0748 */ { UD_Isbb, O_Ev, O_sIz, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0749 */ { UD_Iand, O_Ev, O_sIz, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0750 */ { UD_Isub, O_Ev, O_sIz, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0751 */ { UD_Ixor, O_Ev, O_sIz, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0752 */ { UD_Icmp, O_Ev, O_sIz, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0753 */ { UD_Iadd, O_Eb, O_Ib, O_NONE, P_aso|P_rexr|P_rexx|P_rexb|P_inv64 },
  /* 0754 */ { UD_Ior, O_Eb, O_Ib, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0755 */ { UD_Iadc, O_Eb, O_Ib, O_NONE, P_aso|P_rexr|P_rexx|P_rexb|P_inv64 },
  /* 0756 */ { UD_Isbb, O_Eb, O_Ib, O_NONE, P_aso|P_rexr|P_rexx|P_rexb|P_inv64 },
  /* 0757 */ { UD_Iand, O_Eb, O_Ib, O_NONE, P_aso|P_rexr|P_rexx|P_rexb|P_inv64 },
  /* 0758 */ { UD_Isub, O_Eb, O_Ib, O_NONE, P_aso|P_rexr|P_rexx|P_rexb|P_inv64 },
  /* 0759 */ { UD_Ixor, O_Eb, O_Ib, O_NONE, P_aso|P_rexr|P_rexx|P_rexb|P_inv64 },
  /* 0760 */ { UD_Icmp, O_Eb, O_Ib, O_NONE, P_aso|P_rexr|P_rexx|P_rexb|P_inv64 },
  /* 0761 */ { UD_Iadd, O_Ev, O_sIb, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0762 */ { UD_Ior, O_Ev, O_sIb, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0763 */ { UD_Iadc, O_Ev, O_sIb, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0764 */ { UD_Isbb, O_Ev, O_sIb, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0765 */ { UD_Iand, O_Ev, O_sIb, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0766 */ { UD_Isub, O_Ev, O_sIb, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0767 */ { UD_Ixor, O_Ev, O_sIb, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0768 */ { UD_Icmp, O_Ev, O_sIb, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0769 */ { UD_Itest, O_Eb, O_Gb, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0770 */ { UD_Itest, O_Ev, O_Gv, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0771 */ { UD_Ixchg, O_Eb, O_Gb, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0772 */ { UD_Ixchg, O_Ev, O_Gv, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0773 */ { UD_Imov, O_Eb, O_Gb, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0774 */ { UD_Imov, O_Ev, O_Gv, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0775 */ { UD_Imov, O_Gb, O_Eb, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0776 */ { UD_Imov, O_Gv, O_Ev, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0777 */ { UD_Imov, O_MwRv, O_S, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0778 */ { UD_Ilea, O_Gv, O_M, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0779 */ { UD_Imov, O_S, O_MwRv, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0780 */ { UD_Ipop, O_Ev, O_NONE, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb|P_def64 },
  /* 0781 */ { UD_Ixchg, O_R0v, O_rAX, O_NONE, P_oso|P_rexw|P_rexb },
  /* 0782 */ { UD_Ixchg, O_R1v, O_rAX, O_NONE, P_oso|P_rexw|P_rexb },
  /* 0783 */ { UD_Ixchg, O_R2v, O_rAX, O_NONE, P_oso|P_rexw|P_rexb },
  /* 0784 */ { UD_Ixchg, O_R3v, O_rAX, O_NONE, P_oso|P_rexw|P_rexb },
  /* 0785 */ { UD_Ixchg, O_R4v, O_rAX, O_NONE, P_oso|P_rexw|P_rexb },
  /* 0786 */ { UD_Ixchg, O_R5v, O_rAX, O_NONE, P_oso|P_rexw|P_rexb },
  /* 0787 */ { UD_Ixchg, O_R6v, O_rAX, O_NONE, P_oso|P_rexw|P_rexb },
  /* 0788 */ { UD_Ixchg, O_R7v, O_rAX, O_NONE, P_oso|P_rexw|P_rexb },
  /* 0789 */ { UD_Icbw, O_NONE, O_NONE, O_NONE, P_oso|P_rexw },
  /* 0790 */ { UD_Icwde, O_NONE, O_NONE, O_NONE, P_oso|P_rexw },
  /* 0791 */ { UD_Icdqe, O_NONE, O_NONE, O_NONE, P_oso|P_rexw },
  /* 0792 */ { UD_Icwd, O_NONE, O_NONE, O_NONE, P_oso|P_rexw },
  /* 0793 */ { UD_Icdq, O_NONE, O_NONE, O_NONE, P_oso|P_rexw },
  /* 0794 */ { UD_Icqo, O_NONE, O_NONE, O_NONE, P_oso|P_rexw },
  /* 0795 */ { UD_Icall, O_Av, O_NONE, O_NONE, P_oso },
  /* 0796 */ { UD_Iwait, O_NONE, O_NONE, O_NONE, P_none },
  /* 0797 */ { UD_Ipushfw, O_NONE, O_NONE, O_NONE, P_oso },
  /* 0798 */ { UD_Ipushfw, O_NONE, O_NONE, O_NONE, P_oso|P_rexw|P_def64 },
  /* 0799 */ { UD_Ipushfd, O_NONE, O_NONE, O_NONE, P_oso },
  /* 0800 */ { UD_Ipushfq, O_NONE, O_NONE, O_NONE, P_oso|P_rexw|P_def64 },
  /* 0801 */ { UD_Ipushfq, O_NONE, O_NONE, O_NONE, P_oso|P_rexw|P_def64 },
  /* 0802 */ { UD_Ipopfw, O_NONE, O_NONE, O_NONE, P_oso },
  /* 0803 */ { UD_Ipopfd, O_NONE, O_NONE, O_NONE, P_oso },
  /* 0804 */ { UD_Ipopfq, O_NONE, O_NONE, O_NONE, P_oso|P_def64 },
  /* 0805 */ { UD_Ipopfq, O_NONE, O_NONE, O_NONE, P_oso|P_def64 },
  /* 0806 */ { UD_Isahf, O_NONE, O_NONE, O_NONE, P_none },
  /* 0807 */ { UD_Ilahf, O_NONE, O_NONE, O_NONE, P_none },
  /* 0808 */ { UD_Imov, O_AL, O_Ob, O_NONE, P_none },
  /* 0809 */ { UD_Imov, O_rAX, O_Ov, O_NONE, P_aso|P_oso|P_rexw },
  /* 0810 */ { UD_Imov, O_Ob, O_AL, O_NONE, P_none },
  /* 0811 */ { UD_Imov, O_Ov, O_rAX, O_NONE, P_aso|P_oso|P_rexw },
  /* 0812 */ { UD_Imovsb, O_NONE, O_NONE, O_NONE, P_str|P_seg },
  /* 0813 */ { UD_Imovsw, O_NONE, O_NONE, O_NONE, P_str|P_seg|P_oso|P_rexw },
  /* 0814 */ { UD_Imovsd, O_NONE, O_NONE, O_NONE, P_str|P_seg|P_oso|P_rexw },
  /* 0815 */ { UD_Imovsq, O_NONE, O_NONE, O_NONE, P_str|P_seg|P_oso|P_rexw },
  /* 0816 */ { UD_Icmpsb, O_NONE, O_NONE, O_NONE, P_strz|P_seg },
  /* 0817 */ { UD_Icmpsw, O_NONE, O_NONE, O_NONE, P_strz|P_oso|P_rexw|P_seg },
  /* 0818 */ { UD_Icmpsd, O_NONE, O_NONE, O_NONE, P_strz|P_oso|P_rexw|P_seg },
  /* 0819 */ { UD_Icmpsq, O_NONE, O_NONE, O_NONE, P_strz|P_oso|P_rexw|P_seg },
  /* 0820 */ { UD_Itest, O_AL, O_Ib, O_NONE, P_none },
  /* 0821 */ { UD_Itest, O_rAX, O_sIz, O_NONE, P_oso|P_rexw },
  /* 0822 */ { UD_Istosb, O_NONE, O_NONE, O_NONE, P_str|P_seg },
  /* 0823 */ { UD_Istosw, O_NONE, O_NONE, O_NONE, P_str|P_seg|P_oso|P_rexw },
  /* 0824 */ { UD_Istosd, O_NONE, O_NONE, O_NONE, P_str|P_seg|P_oso|P_rexw },
  /* 0825 */ { UD_Istosq, O_NONE, O_NONE, O_NONE, P_str|P_seg|P_oso|P_rexw },
  /* 0826 */ { UD_Ilodsb, O_NONE, O_NONE, O_NONE, P_str|P_seg },
  /* 0827 */ { UD_Ilodsw, O_NONE, O_NONE, O_NONE, P_str|P_seg|P_oso|P_rexw },
  /* 0828 */ { UD_Ilodsd, O_NONE, O_NONE, O_NONE, P_str|P_seg|P_oso|P_rexw },
  /* 0829 */ { UD_Ilodsq, O_NONE, O_NONE, O_NONE, P_str|P_seg|P_oso|P_rexw },
  /* 0830 */ { UD_Iscasb, O_NONE, O_NONE, O_NONE, P_strz },
  /* 0831 */ { UD_Iscasw, O_NONE, O_NONE, O_NONE, P_strz|P_oso|P_rexw },
  /* 0832 */ { UD_Iscasd, O_NONE, O_NONE, O_NONE, P_strz|P_oso|P_rexw },
  /* 0833 */ { UD_Iscasq, O_NONE, O_NONE, O_NONE, P_strz|P_oso|P_rexw },
  /* 0834 */ { UD_Imov, O_R0b, O_Ib, O_NONE, P_rexb },
  /* 0835 */ { UD_Imov, O_R1b, O_Ib, O_NONE, P_rexb },
  /* 0836 */ { UD_Imov, O_R2b, O_Ib, O_NONE, P_rexb },
  /* 0837 */ { UD_Imov, O_R3b, O_Ib, O_NONE, P_rexb },
  /* 0838 */ { UD_Imov, O_R4b, O_Ib, O_NONE, P_rexb },
  /* 0839 */ { UD_Imov, O_R5b, O_Ib, O_NONE, P_rexb },
  /* 0840 */ { UD_Imov, O_R6b, O_Ib, O_NONE, P_rexb },
  /* 0841 */ { UD_Imov, O_R7b, O_Ib, O_NONE, P_rexb },
  /* 0842 */ { UD_Imov, O_R0v, O_Iv, O_NONE, P_oso|P_rexw|P_rexb },
  /* 0843 */ { UD_Imov, O_R1v, O_Iv, O_NONE, P_oso|P_rexw|P_rexb },
  /* 0844 */ { UD_Imov, O_R2v, O_Iv, O_NONE, P_oso|P_rexw|P_rexb },
  /* 0845 */ { UD_Imov, O_R3v, O_Iv, O_NONE, P_oso|P_rexw|P_rexb },
  /* 0846 */ { UD_Imov, O_R4v, O_Iv, O_NONE, P_oso|P_rexw|P_rexb },
  /* 0847 */ { UD_Imov, O_R5v, O_Iv, O_NONE, P_oso|P_rexw|P_rexb },
  /* 0848 */ { UD_Imov, O_R6v, O_Iv, O_NONE, P_oso|P_rexw|P_rexb },
  /* 0849 */ { UD_Imov, O_R7v, O_Iv, O_NONE, P_oso|P_rexw|P_rexb },
  /* 0850 */ { UD_Irol, O_Eb, O_Ib, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0851 */ { UD_Iror, O_Eb, O_Ib, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0852 */ { UD_Ircl, O_Eb, O_Ib, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0853 */ { UD_Ircr, O_Eb, O_Ib, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0854 */ { UD_Ishl, O_Eb, O_Ib, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0855 */ { UD_Ishr, O_Eb, O_Ib, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0856 */ { UD_Ishl, O_Eb, O_Ib, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0857 */ { UD_Isar, O_Eb, O_Ib, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0858 */ { UD_Irol, O_Ev, O_Ib, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0859 */ { UD_Iror, O_Ev, O_Ib, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0860 */ { UD_Ircl, O_Ev, O_Ib, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0861 */ { UD_Ircr, O_Ev, O_Ib, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0862 */ { UD_Ishl, O_Ev, O_Ib, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0863 */ { UD_Ishr, O_Ev, O_Ib, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0864 */ { UD_Ishl, O_Ev, O_Ib, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0865 */ { UD_Isar, O_Ev, O_Ib, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0866 */ { UD_Iret, O_Iw, O_NONE, O_NONE, P_none },
  /* 0867 */ { UD_Iret, O_NONE, O_NONE, O_NONE, P_none },
  /* 0868 */ { UD_Iles, O_Gv, O_M, O_NONE, P_aso|P_oso },
  /* 0869 */ { UD_Ilds, O_Gv, O_M, O_NONE, P_aso|P_oso },
  /* 0870 */ { UD_Imov, O_Eb, O_Ib, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0871 */ { UD_Imov, O_Ev, O_sIz, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0872 */ { UD_Ienter, O_Iw, O_Ib, O_NONE, P_def64 },
  /* 0873 */ { UD_Ileave, O_NONE, O_NONE, O_NONE, P_none },
  /* 0874 */ { UD_Iretf, O_Iw, O_NONE, O_NONE, P_none },
  /* 0875 */ { UD_Iretf, O_NONE, O_NONE, O_NONE, P_none },
  /* 0876 */ { UD_Iint3, O_NONE, O_NONE, O_NONE, P_none },
  /* 0877 */ { UD_Iint, O_Ib, O_NONE, O_NONE, P_none },
  /* 0878 */ { UD_Iinto, O_NONE, O_NONE, O_NONE, P_inv64 },
  /* 0879 */ { UD_Iiretw, O_NONE, O_NONE, O_NONE, P_oso|P_rexw },
  /* 0880 */ { UD_Iiretd, O_NONE, O_NONE, O_NONE, P_oso|P_rexw },
  /* 0881 */ { UD_Iiretq, O_NONE, O_NONE, O_NONE, P_oso|P_rexw },
  /* 0882 */ { UD_Irol, O_Eb, O_I1, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0883 */ { UD_Iror, O_Eb, O_I1, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0884 */ { UD_Ircl, O_Eb, O_I1, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0885 */ { UD_Ircr, O_Eb, O_I1, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0886 */ { UD_Ishl, O_Eb, O_I1, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0887 */ { UD_Ishr, O_Eb, O_I1, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0888 */ { UD_Ishl, O_Eb, O_I1, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0889 */ { UD_Isar, O_Eb, O_I1, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0890 */ { UD_Irol, O_Ev, O_I1, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0891 */ { UD_Iror, O_Ev, O_I1, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0892 */ { UD_Ircl, O_Ev, O_I1, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0893 */ { UD_Ircr, O_Ev, O_I1, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0894 */ { UD_Ishl, O_Ev, O_I1, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0895 */ { UD_Ishr, O_Ev, O_I1, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0896 */ { UD_Ishl, O_Ev, O_I1, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0897 */ { UD_Isar, O_Ev, O_I1, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0898 */ { UD_Irol, O_Eb, O_CL, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0899 */ { UD_Iror, O_Eb, O_CL, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0900 */ { UD_Ircl, O_Eb, O_CL, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0901 */ { UD_Ircr, O_Eb, O_CL, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0902 */ { UD_Ishl, O_Eb, O_CL, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0903 */ { UD_Ishr, O_Eb, O_CL, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0904 */ { UD_Ishl, O_Eb, O_CL, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0905 */ { UD_Isar, O_Eb, O_CL, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0906 */ { UD_Irol, O_Ev, O_CL, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0907 */ { UD_Iror, O_Ev, O_CL, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0908 */ { UD_Ircl, O_Ev, O_CL, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0909 */ { UD_Ircr, O_Ev, O_CL, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0910 */ { UD_Ishl, O_Ev, O_CL, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0911 */ { UD_Ishr, O_Ev, O_CL, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0912 */ { UD_Ishl, O_Ev, O_CL, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0913 */ { UD_Isar, O_Ev, O_CL, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 0914 */ { UD_Iaam, O_Ib, O_NONE, O_NONE, P_none },
  /* 0915 */ { UD_Iaad, O_Ib, O_NONE, O_NONE, P_none },
  /* 0916 */ { UD_Isalc, O_NONE, O_NONE, O_NONE, P_inv64 },
  /* 0917 */ { UD_Ixlatb, O_NONE, O_NONE, O_NONE, P_rexw|P_seg },
  /* 0918 */ { UD_Ifadd, O_Md, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0919 */ { UD_Ifmul, O_Md, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0920 */ { UD_Ifcom, O_Md, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0921 */ { UD_Ifcomp, O_Md, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0922 */ { UD_Ifsub, O_Md, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0923 */ { UD_Ifsubr, O_Md, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0924 */ { UD_Ifdiv, O_Md, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0925 */ { UD_Ifdivr, O_Md, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0926 */ { UD_Ifadd, O_ST0, O_ST0, O_NONE, P_none },
  /* 0927 */ { UD_Ifadd, O_ST0, O_ST1, O_NONE, P_none },
  /* 0928 */ { UD_Ifadd, O_ST0, O_ST2, O_NONE, P_none },
  /* 0929 */ { UD_Ifadd, O_ST0, O_ST3, O_NONE, P_none },
  /* 0930 */ { UD_Ifadd, O_ST0, O_ST4, O_NONE, P_none },
  /* 0931 */ { UD_Ifadd, O_ST0, O_ST5, O_NONE, P_none },
  /* 0932 */ { UD_Ifadd, O_ST0, O_ST6, O_NONE, P_none },
  /* 0933 */ { UD_Ifadd, O_ST0, O_ST7, O_NONE, P_none },
  /* 0934 */ { UD_Ifmul, O_ST0, O_ST0, O_NONE, P_none },
  /* 0935 */ { UD_Ifmul, O_ST0, O_ST1, O_NONE, P_none },
  /* 0936 */ { UD_Ifmul, O_ST0, O_ST2, O_NONE, P_none },
  /* 0937 */ { UD_Ifmul, O_ST0, O_ST3, O_NONE, P_none },
  /* 0938 */ { UD_Ifmul, O_ST0, O_ST4, O_NONE, P_none },
  /* 0939 */ { UD_Ifmul, O_ST0, O_ST5, O_NONE, P_none },
  /* 0940 */ { UD_Ifmul, O_ST0, O_ST6, O_NONE, P_none },
  /* 0941 */ { UD_Ifmul, O_ST0, O_ST7, O_NONE, P_none },
  /* 0942 */ { UD_Ifcom, O_ST0, O_ST0, O_NONE, P_none },
  /* 0943 */ { UD_Ifcom, O_ST0, O_ST1, O_NONE, P_none },
  /* 0944 */ { UD_Ifcom, O_ST0, O_ST2, O_NONE, P_none },
  /* 0945 */ { UD_Ifcom, O_ST0, O_ST3, O_NONE, P_none },
  /* 0946 */ { UD_Ifcom, O_ST0, O_ST4, O_NONE, P_none },
  /* 0947 */ { UD_Ifcom, O_ST0, O_ST5, O_NONE, P_none },
  /* 0948 */ { UD_Ifcom, O_ST0, O_ST6, O_NONE, P_none },
  /* 0949 */ { UD_Ifcom, O_ST0, O_ST7, O_NONE, P_none },
  /* 0950 */ { UD_Ifcomp, O_ST0, O_ST0, O_NONE, P_none },
  /* 0951 */ { UD_Ifcomp, O_ST0, O_ST1, O_NONE, P_none },
  /* 0952 */ { UD_Ifcomp, O_ST0, O_ST2, O_NONE, P_none },
  /* 0953 */ { UD_Ifcomp, O_ST0, O_ST3, O_NONE, P_none },
  /* 0954 */ { UD_Ifcomp, O_ST0, O_ST4, O_NONE, P_none },
  /* 0955 */ { UD_Ifcomp, O_ST0, O_ST5, O_NONE, P_none },
  /* 0956 */ { UD_Ifcomp, O_ST0, O_ST6, O_NONE, P_none },
  /* 0957 */ { UD_Ifcomp, O_ST0, O_ST7, O_NONE, P_none },
  /* 0958 */ { UD_Ifsub, O_ST0, O_ST0, O_NONE, P_none },
  /* 0959 */ { UD_Ifsub, O_ST0, O_ST1, O_NONE, P_none },
  /* 0960 */ { UD_Ifsub, O_ST0, O_ST2, O_NONE, P_none },
  /* 0961 */ { UD_Ifsub, O_ST0, O_ST3, O_NONE, P_none },
  /* 0962 */ { UD_Ifsub, O_ST0, O_ST4, O_NONE, P_none },
  /* 0963 */ { UD_Ifsub, O_ST0, O_ST5, O_NONE, P_none },
  /* 0964 */ { UD_Ifsub, O_ST0, O_ST6, O_NONE, P_none },
  /* 0965 */ { UD_Ifsub, O_ST0, O_ST7, O_NONE, P_none },
  /* 0966 */ { UD_Ifsubr, O_ST0, O_ST0, O_NONE, P_none },
  /* 0967 */ { UD_Ifsubr, O_ST0, O_ST1, O_NONE, P_none },
  /* 0968 */ { UD_Ifsubr, O_ST0, O_ST2, O_NONE, P_none },
  /* 0969 */ { UD_Ifsubr, O_ST0, O_ST3, O_NONE, P_none },
  /* 0970 */ { UD_Ifsubr, O_ST0, O_ST4, O_NONE, P_none },
  /* 0971 */ { UD_Ifsubr, O_ST0, O_ST5, O_NONE, P_none },
  /* 0972 */ { UD_Ifsubr, O_ST0, O_ST6, O_NONE, P_none },
  /* 0973 */ { UD_Ifsubr, O_ST0, O_ST7, O_NONE, P_none },
  /* 0974 */ { UD_Ifdiv, O_ST0, O_ST0, O_NONE, P_none },
  /* 0975 */ { UD_Ifdiv, O_ST0, O_ST1, O_NONE, P_none },
  /* 0976 */ { UD_Ifdiv, O_ST0, O_ST2, O_NONE, P_none },
  /* 0977 */ { UD_Ifdiv, O_ST0, O_ST3, O_NONE, P_none },
  /* 0978 */ { UD_Ifdiv, O_ST0, O_ST4, O_NONE, P_none },
  /* 0979 */ { UD_Ifdiv, O_ST0, O_ST5, O_NONE, P_none },
  /* 0980 */ { UD_Ifdiv, O_ST0, O_ST6, O_NONE, P_none },
  /* 0981 */ { UD_Ifdiv, O_ST0, O_ST7, O_NONE, P_none },
  /* 0982 */ { UD_Ifdivr, O_ST0, O_ST0, O_NONE, P_none },
  /* 0983 */ { UD_Ifdivr, O_ST0, O_ST1, O_NONE, P_none },
  /* 0984 */ { UD_Ifdivr, O_ST0, O_ST2, O_NONE, P_none },
  /* 0985 */ { UD_Ifdivr, O_ST0, O_ST3, O_NONE, P_none },
  /* 0986 */ { UD_Ifdivr, O_ST0, O_ST4, O_NONE, P_none },
  /* 0987 */ { UD_Ifdivr, O_ST0, O_ST5, O_NONE, P_none },
  /* 0988 */ { UD_Ifdivr, O_ST0, O_ST6, O_NONE, P_none },
  /* 0989 */ { UD_Ifdivr, O_ST0, O_ST7, O_NONE, P_none },
  /* 0990 */ { UD_Ifld, O_Md, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0991 */ { UD_Ifst, O_Md, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0992 */ { UD_Ifstp, O_Md, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0993 */ { UD_Ifldenv, O_M, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0994 */ { UD_Ifldcw, O_Mw, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0995 */ { UD_Ifnstenv, O_M, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0996 */ { UD_Ifnstcw, O_Mw, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 0997 */ { UD_Ifld, O_ST0, O_NONE, O_NONE, P_none },
  /* 0998 */ { UD_Ifld, O_ST1, O_NONE, O_NONE, P_none },
  /* 0999 */ { UD_Ifld, O_ST2, O_NONE, O_NONE, P_none },
  /* 1000 */ { UD_Ifld, O_ST3, O_NONE, O_NONE, P_none },
  /* 1001 */ { UD_Ifld, O_ST4, O_NONE, O_NONE, P_none },
  /* 1002 */ { UD_Ifld, O_ST5, O_NONE, O_NONE, P_none },
  /* 1003 */ { UD_Ifld, O_ST6, O_NONE, O_NONE, P_none },
  /* 1004 */ { UD_Ifld, O_ST7, O_NONE, O_NONE, P_none },
  /* 1005 */ { UD_Ifxch, O_ST0, O_ST0, O_NONE, P_none },
  /* 1006 */ { UD_Ifxch, O_ST0, O_ST1, O_NONE, P_none },
  /* 1007 */ { UD_Ifxch, O_ST0, O_ST2, O_NONE, P_none },
  /* 1008 */ { UD_Ifxch, O_ST0, O_ST3, O_NONE, P_none },
  /* 1009 */ { UD_Ifxch, O_ST0, O_ST4, O_NONE, P_none },
  /* 1010 */ { UD_Ifxch, O_ST0, O_ST5, O_NONE, P_none },
  /* 1011 */ { UD_Ifxch, O_ST0, O_ST6, O_NONE, P_none },
  /* 1012 */ { UD_Ifxch, O_ST0, O_ST7, O_NONE, P_none },
  /* 1013 */ { UD_Ifnop, O_NONE, O_NONE, O_NONE, P_none },
  /* 1014 */ { UD_Ifstp1, O_ST0, O_NONE, O_NONE, P_none },
  /* 1015 */ { UD_Ifstp1, O_ST1, O_NONE, O_NONE, P_none },
  /* 1016 */ { UD_Ifstp1, O_ST2, O_NONE, O_NONE, P_none },
  /* 1017 */ { UD_Ifstp1, O_ST3, O_NONE, O_NONE, P_none },
  /* 1018 */ { UD_Ifstp1, O_ST4, O_NONE, O_NONE, P_none },
  /* 1019 */ { UD_Ifstp1, O_ST5, O_NONE, O_NONE, P_none },
  /* 1020 */ { UD_Ifstp1, O_ST6, O_NONE, O_NONE, P_none },
  /* 1021 */ { UD_Ifstp1, O_ST7, O_NONE, O_NONE, P_none },
  /* 1022 */ { UD_Ifchs, O_NONE, O_NONE, O_NONE, P_none },
  /* 1023 */ { UD_Ifabs, O_NONE, O_NONE, O_NONE, P_none },
  /* 1024 */ { UD_Iftst, O_NONE, O_NONE, O_NONE, P_none },
  /* 1025 */ { UD_Ifxam, O_NONE, O_NONE, O_NONE, P_none },
  /* 1026 */ { UD_Ifld1, O_NONE, O_NONE, O_NONE, P_none },
  /* 1027 */ { UD_Ifldl2t, O_NONE, O_NONE, O_NONE, P_none },
  /* 1028 */ { UD_Ifldl2e, O_NONE, O_NONE, O_NONE, P_none },
  /* 1029 */ { UD_Ifldpi, O_NONE, O_NONE, O_NONE, P_none },
  /* 1030 */ { UD_Ifldlg2, O_NONE, O_NONE, O_NONE, P_none },
  /* 1031 */ { UD_Ifldln2, O_NONE, O_NONE, O_NONE, P_none },
  /* 1032 */ { UD_Ifldz, O_NONE, O_NONE, O_NONE, P_none },
  /* 1033 */ { UD_If2xm1, O_NONE, O_NONE, O_NONE, P_none },
  /* 1034 */ { UD_Ifyl2x, O_NONE, O_NONE, O_NONE, P_none },
  /* 1035 */ { UD_Ifptan, O_NONE, O_NONE, O_NONE, P_none },
  /* 1036 */ { UD_Ifpatan, O_NONE, O_NONE, O_NONE, P_none },
  /* 1037 */ { UD_Ifxtract, O_NONE, O_NONE, O_NONE, P_none },
  /* 1038 */ { UD_Ifprem1, O_NONE, O_NONE, O_NONE, P_none },
  /* 1039 */ { UD_Ifdecstp, O_NONE, O_NONE, O_NONE, P_none },
  /* 1040 */ { UD_Ifincstp, O_NONE, O_NONE, O_NONE, P_none },
  /* 1041 */ { UD_Ifprem, O_NONE, O_NONE, O_NONE, P_none },
  /* 1042 */ { UD_Ifyl2xp1, O_NONE, O_NONE, O_NONE, P_none },
  /* 1043 */ { UD_Ifsqrt, O_NONE, O_NONE, O_NONE, P_none },
  /* 1044 */ { UD_Ifsincos, O_NONE, O_NONE, O_NONE, P_none },
  /* 1045 */ { UD_Ifrndint, O_NONE, O_NONE, O_NONE, P_none },
  /* 1046 */ { UD_Ifscale, O_NONE, O_NONE, O_NONE, P_none },
  /* 1047 */ { UD_Ifsin, O_NONE, O_NONE, O_NONE, P_none },
  /* 1048 */ { UD_Ifcos, O_NONE, O_NONE, O_NONE, P_none },
  /* 1049 */ { UD_Ifiadd, O_Md, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 1050 */ { UD_Ifimul, O_Md, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 1051 */ { UD_Ificom, O_Md, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 1052 */ { UD_Ificomp, O_Md, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 1053 */ { UD_Ifisub, O_Md, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 1054 */ { UD_Ifisubr, O_Md, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 1055 */ { UD_Ifidiv, O_Md, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 1056 */ { UD_Ifidivr, O_Md, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 1057 */ { UD_Ifcmovb, O_ST0, O_ST0, O_NONE, P_none },
  /* 1058 */ { UD_Ifcmovb, O_ST0, O_ST1, O_NONE, P_none },
  /* 1059 */ { UD_Ifcmovb, O_ST0, O_ST2, O_NONE, P_none },
  /* 1060 */ { UD_Ifcmovb, O_ST0, O_ST3, O_NONE, P_none },
  /* 1061 */ { UD_Ifcmovb, O_ST0, O_ST4, O_NONE, P_none },
  /* 1062 */ { UD_Ifcmovb, O_ST0, O_ST5, O_NONE, P_none },
  /* 1063 */ { UD_Ifcmovb, O_ST0, O_ST6, O_NONE, P_none },
  /* 1064 */ { UD_Ifcmovb, O_ST0, O_ST7, O_NONE, P_none },
  /* 1065 */ { UD_Ifcmove, O_ST0, O_ST0, O_NONE, P_none },
  /* 1066 */ { UD_Ifcmove, O_ST0, O_ST1, O_NONE, P_none },
  /* 1067 */ { UD_Ifcmove, O_ST0, O_ST2, O_NONE, P_none },
  /* 1068 */ { UD_Ifcmove, O_ST0, O_ST3, O_NONE, P_none },
  /* 1069 */ { UD_Ifcmove, O_ST0, O_ST4, O_NONE, P_none },
  /* 1070 */ { UD_Ifcmove, O_ST0, O_ST5, O_NONE, P_none },
  /* 1071 */ { UD_Ifcmove, O_ST0, O_ST6, O_NONE, P_none },
  /* 1072 */ { UD_Ifcmove, O_ST0, O_ST7, O_NONE, P_none },
  /* 1073 */ { UD_Ifcmovbe, O_ST0, O_ST0, O_NONE, P_none },
  /* 1074 */ { UD_Ifcmovbe, O_ST0, O_ST1, O_NONE, P_none },
  /* 1075 */ { UD_Ifcmovbe, O_ST0, O_ST2, O_NONE, P_none },
  /* 1076 */ { UD_Ifcmovbe, O_ST0, O_ST3, O_NONE, P_none },
  /* 1077 */ { UD_Ifcmovbe, O_ST0, O_ST4, O_NONE, P_none },
  /* 1078 */ { UD_Ifcmovbe, O_ST0, O_ST5, O_NONE, P_none },
  /* 1079 */ { UD_Ifcmovbe, O_ST0, O_ST6, O_NONE, P_none },
  /* 1080 */ { UD_Ifcmovbe, O_ST0, O_ST7, O_NONE, P_none },
  /* 1081 */ { UD_Ifcmovu, O_ST0, O_ST0, O_NONE, P_none },
  /* 1082 */ { UD_Ifcmovu, O_ST0, O_ST1, O_NONE, P_none },
  /* 1083 */ { UD_Ifcmovu, O_ST0, O_ST2, O_NONE, P_none },
  /* 1084 */ { UD_Ifcmovu, O_ST0, O_ST3, O_NONE, P_none },
  /* 1085 */ { UD_Ifcmovu, O_ST0, O_ST4, O_NONE, P_none },
  /* 1086 */ { UD_Ifcmovu, O_ST0, O_ST5, O_NONE, P_none },
  /* 1087 */ { UD_Ifcmovu, O_ST0, O_ST6, O_NONE, P_none },
  /* 1088 */ { UD_Ifcmovu, O_ST0, O_ST7, O_NONE, P_none },
  /* 1089 */ { UD_Ifucompp, O_NONE, O_NONE, O_NONE, P_none },
  /* 1090 */ { UD_Ifild, O_Md, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 1091 */ { UD_Ifisttp, O_Md, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 1092 */ { UD_Ifist, O_Md, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 1093 */ { UD_Ifistp, O_Md, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 1094 */ { UD_Ifld, O_Mt, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 1095 */ { UD_Ifstp, O_Mt, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 1096 */ { UD_Ifcmovnb, O_ST0, O_ST0, O_NONE, P_none },
  /* 1097 */ { UD_Ifcmovnb, O_ST0, O_ST1, O_NONE, P_none },
  /* 1098 */ { UD_Ifcmovnb, O_ST0, O_ST2, O_NONE, P_none },
  /* 1099 */ { UD_Ifcmovnb, O_ST0, O_ST3, O_NONE, P_none },
  /* 1100 */ { UD_Ifcmovnb, O_ST0, O_ST4, O_NONE, P_none },
  /* 1101 */ { UD_Ifcmovnb, O_ST0, O_ST5, O_NONE, P_none },
  /* 1102 */ { UD_Ifcmovnb, O_ST0, O_ST6, O_NONE, P_none },
  /* 1103 */ { UD_Ifcmovnb, O_ST0, O_ST7, O_NONE, P_none },
  /* 1104 */ { UD_Ifcmovne, O_ST0, O_ST0, O_NONE, P_none },
  /* 1105 */ { UD_Ifcmovne, O_ST0, O_ST1, O_NONE, P_none },
  /* 1106 */ { UD_Ifcmovne, O_ST0, O_ST2, O_NONE, P_none },
  /* 1107 */ { UD_Ifcmovne, O_ST0, O_ST3, O_NONE, P_none },
  /* 1108 */ { UD_Ifcmovne, O_ST0, O_ST4, O_NONE, P_none },
  /* 1109 */ { UD_Ifcmovne, O_ST0, O_ST5, O_NONE, P_none },
  /* 1110 */ { UD_Ifcmovne, O_ST0, O_ST6, O_NONE, P_none },
  /* 1111 */ { UD_Ifcmovne, O_ST0, O_ST7, O_NONE, P_none },
  /* 1112 */ { UD_Ifcmovnbe, O_ST0, O_ST0, O_NONE, P_none },
  /* 1113 */ { UD_Ifcmovnbe, O_ST0, O_ST1, O_NONE, P_none },
  /* 1114 */ { UD_Ifcmovnbe, O_ST0, O_ST2, O_NONE, P_none },
  /* 1115 */ { UD_Ifcmovnbe, O_ST0, O_ST3, O_NONE, P_none },
  /* 1116 */ { UD_Ifcmovnbe, O_ST0, O_ST4, O_NONE, P_none },
  /* 1117 */ { UD_Ifcmovnbe, O_ST0, O_ST5, O_NONE, P_none },
  /* 1118 */ { UD_Ifcmovnbe, O_ST0, O_ST6, O_NONE, P_none },
  /* 1119 */ { UD_Ifcmovnbe, O_ST0, O_ST7, O_NONE, P_none },
  /* 1120 */ { UD_Ifcmovnu, O_ST0, O_ST0, O_NONE, P_none },
  /* 1121 */ { UD_Ifcmovnu, O_ST0, O_ST1, O_NONE, P_none },
  /* 1122 */ { UD_Ifcmovnu, O_ST0, O_ST2, O_NONE, P_none },
  /* 1123 */ { UD_Ifcmovnu, O_ST0, O_ST3, O_NONE, P_none },
  /* 1124 */ { UD_Ifcmovnu, O_ST0, O_ST4, O_NONE, P_none },
  /* 1125 */ { UD_Ifcmovnu, O_ST0, O_ST5, O_NONE, P_none },
  /* 1126 */ { UD_Ifcmovnu, O_ST0, O_ST6, O_NONE, P_none },
  /* 1127 */ { UD_Ifcmovnu, O_ST0, O_ST7, O_NONE, P_none },
  /* 1128 */ { UD_Ifclex, O_NONE, O_NONE, O_NONE, P_none },
  /* 1129 */ { UD_Ifninit, O_NONE, O_NONE, O_NONE, P_none },
  /* 1130 */ { UD_Ifucomi, O_ST0, O_ST0, O_NONE, P_none },
  /* 1131 */ { UD_Ifucomi, O_ST0, O_ST1, O_NONE, P_none },
  /* 1132 */ { UD_Ifucomi, O_ST0, O_ST2, O_NONE, P_none },
  /* 1133 */ { UD_Ifucomi, O_ST0, O_ST3, O_NONE, P_none },
  /* 1134 */ { UD_Ifucomi, O_ST0, O_ST4, O_NONE, P_none },
  /* 1135 */ { UD_Ifucomi, O_ST0, O_ST5, O_NONE, P_none },
  /* 1136 */ { UD_Ifucomi, O_ST0, O_ST6, O_NONE, P_none },
  /* 1137 */ { UD_Ifucomi, O_ST0, O_ST7, O_NONE, P_none },
  /* 1138 */ { UD_Ifcomi, O_ST0, O_ST0, O_NONE, P_none },
  /* 1139 */ { UD_Ifcomi, O_ST0, O_ST1, O_NONE, P_none },
  /* 1140 */ { UD_Ifcomi, O_ST0, O_ST2, O_NONE, P_none },
  /* 1141 */ { UD_Ifcomi, O_ST0, O_ST3, O_NONE, P_none },
  /* 1142 */ { UD_Ifcomi, O_ST0, O_ST4, O_NONE, P_none },
  /* 1143 */ { UD_Ifcomi, O_ST0, O_ST5, O_NONE, P_none },
  /* 1144 */ { UD_Ifcomi, O_ST0, O_ST6, O_NONE, P_none },
  /* 1145 */ { UD_Ifcomi, O_ST0, O_ST7, O_NONE, P_none },
  /* 1146 */ { UD_Ifadd, O_Mq, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 1147 */ { UD_Ifmul, O_Mq, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 1148 */ { UD_Ifcom, O_Mq, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 1149 */ { UD_Ifcomp, O_Mq, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 1150 */ { UD_Ifsub, O_Mq, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 1151 */ { UD_Ifsubr, O_Mq, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 1152 */ { UD_Ifdiv, O_Mq, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 1153 */ { UD_Ifdivr, O_Mq, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 1154 */ { UD_Ifadd, O_ST0, O_ST0, O_NONE, P_none },
  /* 1155 */ { UD_Ifadd, O_ST1, O_ST0, O_NONE, P_none },
  /* 1156 */ { UD_Ifadd, O_ST2, O_ST0, O_NONE, P_none },
  /* 1157 */ { UD_Ifadd, O_ST3, O_ST0, O_NONE, P_none },
  /* 1158 */ { UD_Ifadd, O_ST4, O_ST0, O_NONE, P_none },
  /* 1159 */ { UD_Ifadd, O_ST5, O_ST0, O_NONE, P_none },
  /* 1160 */ { UD_Ifadd, O_ST6, O_ST0, O_NONE, P_none },
  /* 1161 */ { UD_Ifadd, O_ST7, O_ST0, O_NONE, P_none },
  /* 1162 */ { UD_Ifmul, O_ST0, O_ST0, O_NONE, P_none },
  /* 1163 */ { UD_Ifmul, O_ST1, O_ST0, O_NONE, P_none },
  /* 1164 */ { UD_Ifmul, O_ST2, O_ST0, O_NONE, P_none },
  /* 1165 */ { UD_Ifmul, O_ST3, O_ST0, O_NONE, P_none },
  /* 1166 */ { UD_Ifmul, O_ST4, O_ST0, O_NONE, P_none },
  /* 1167 */ { UD_Ifmul, O_ST5, O_ST0, O_NONE, P_none },
  /* 1168 */ { UD_Ifmul, O_ST6, O_ST0, O_NONE, P_none },
  /* 1169 */ { UD_Ifmul, O_ST7, O_ST0, O_NONE, P_none },
  /* 1170 */ { UD_Ifcom2, O_ST0, O_NONE, O_NONE, P_none },
  /* 1171 */ { UD_Ifcom2, O_ST1, O_NONE, O_NONE, P_none },
  /* 1172 */ { UD_Ifcom2, O_ST2, O_NONE, O_NONE, P_none },
  /* 1173 */ { UD_Ifcom2, O_ST3, O_NONE, O_NONE, P_none },
  /* 1174 */ { UD_Ifcom2, O_ST4, O_NONE, O_NONE, P_none },
  /* 1175 */ { UD_Ifcom2, O_ST5, O_NONE, O_NONE, P_none },
  /* 1176 */ { UD_Ifcom2, O_ST6, O_NONE, O_NONE, P_none },
  /* 1177 */ { UD_Ifcom2, O_ST7, O_NONE, O_NONE, P_none },
  /* 1178 */ { UD_Ifcomp3, O_ST0, O_NONE, O_NONE, P_none },
  /* 1179 */ { UD_Ifcomp3, O_ST1, O_NONE, O_NONE, P_none },
  /* 1180 */ { UD_Ifcomp3, O_ST2, O_NONE, O_NONE, P_none },
  /* 1181 */ { UD_Ifcomp3, O_ST3, O_NONE, O_NONE, P_none },
  /* 1182 */ { UD_Ifcomp3, O_ST4, O_NONE, O_NONE, P_none },
  /* 1183 */ { UD_Ifcomp3, O_ST5, O_NONE, O_NONE, P_none },
  /* 1184 */ { UD_Ifcomp3, O_ST6, O_NONE, O_NONE, P_none },
  /* 1185 */ { UD_Ifcomp3, O_ST7, O_NONE, O_NONE, P_none },
  /* 1186 */ { UD_Ifsubr, O_ST0, O_ST0, O_NONE, P_none },
  /* 1187 */ { UD_Ifsubr, O_ST1, O_ST0, O_NONE, P_none },
  /* 1188 */ { UD_Ifsubr, O_ST2, O_ST0, O_NONE, P_none },
  /* 1189 */ { UD_Ifsubr, O_ST3, O_ST0, O_NONE, P_none },
  /* 1190 */ { UD_Ifsubr, O_ST4, O_ST0, O_NONE, P_none },
  /* 1191 */ { UD_Ifsubr, O_ST5, O_ST0, O_NONE, P_none },
  /* 1192 */ { UD_Ifsubr, O_ST6, O_ST0, O_NONE, P_none },
  /* 1193 */ { UD_Ifsubr, O_ST7, O_ST0, O_NONE, P_none },
  /* 1194 */ { UD_Ifsub, O_ST0, O_ST0, O_NONE, P_none },
  /* 1195 */ { UD_Ifsub, O_ST1, O_ST0, O_NONE, P_none },
  /* 1196 */ { UD_Ifsub, O_ST2, O_ST0, O_NONE, P_none },
  /* 1197 */ { UD_Ifsub, O_ST3, O_ST0, O_NONE, P_none },
  /* 1198 */ { UD_Ifsub, O_ST4, O_ST0, O_NONE, P_none },
  /* 1199 */ { UD_Ifsub, O_ST5, O_ST0, O_NONE, P_none },
  /* 1200 */ { UD_Ifsub, O_ST6, O_ST0, O_NONE, P_none },
  /* 1201 */ { UD_Ifsub, O_ST7, O_ST0, O_NONE, P_none },
  /* 1202 */ { UD_Ifdivr, O_ST0, O_ST0, O_NONE, P_none },
  /* 1203 */ { UD_Ifdivr, O_ST1, O_ST0, O_NONE, P_none },
  /* 1204 */ { UD_Ifdivr, O_ST2, O_ST0, O_NONE, P_none },
  /* 1205 */ { UD_Ifdivr, O_ST3, O_ST0, O_NONE, P_none },
  /* 1206 */ { UD_Ifdivr, O_ST4, O_ST0, O_NONE, P_none },
  /* 1207 */ { UD_Ifdivr, O_ST5, O_ST0, O_NONE, P_none },
  /* 1208 */ { UD_Ifdivr, O_ST6, O_ST0, O_NONE, P_none },
  /* 1209 */ { UD_Ifdivr, O_ST7, O_ST0, O_NONE, P_none },
  /* 1210 */ { UD_Ifdiv, O_ST0, O_ST0, O_NONE, P_none },
  /* 1211 */ { UD_Ifdiv, O_ST1, O_ST0, O_NONE, P_none },
  /* 1212 */ { UD_Ifdiv, O_ST2, O_ST0, O_NONE, P_none },
  /* 1213 */ { UD_Ifdiv, O_ST3, O_ST0, O_NONE, P_none },
  /* 1214 */ { UD_Ifdiv, O_ST4, O_ST0, O_NONE, P_none },
  /* 1215 */ { UD_Ifdiv, O_ST5, O_ST0, O_NONE, P_none },
  /* 1216 */ { UD_Ifdiv, O_ST6, O_ST0, O_NONE, P_none },
  /* 1217 */ { UD_Ifdiv, O_ST7, O_ST0, O_NONE, P_none },
  /* 1218 */ { UD_Ifld, O_Mq, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 1219 */ { UD_Ifisttp, O_Mq, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 1220 */ { UD_Ifst, O_Mq, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 1221 */ { UD_Ifstp, O_Mq, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 1222 */ { UD_Ifrstor, O_M, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 1223 */ { UD_Ifnsave, O_M, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 1224 */ { UD_Ifnstsw, O_Mw, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 1225 */ { UD_Iffree, O_ST0, O_NONE, O_NONE, P_none },
  /* 1226 */ { UD_Iffree, O_ST1, O_NONE, O_NONE, P_none },
  /* 1227 */ { UD_Iffree, O_ST2, O_NONE, O_NONE, P_none },
  /* 1228 */ { UD_Iffree, O_ST3, O_NONE, O_NONE, P_none },
  /* 1229 */ { UD_Iffree, O_ST4, O_NONE, O_NONE, P_none },
  /* 1230 */ { UD_Iffree, O_ST5, O_NONE, O_NONE, P_none },
  /* 1231 */ { UD_Iffree, O_ST6, O_NONE, O_NONE, P_none },
  /* 1232 */ { UD_Iffree, O_ST7, O_NONE, O_NONE, P_none },
  /* 1233 */ { UD_Ifxch4, O_ST0, O_NONE, O_NONE, P_none },
  /* 1234 */ { UD_Ifxch4, O_ST1, O_NONE, O_NONE, P_none },
  /* 1235 */ { UD_Ifxch4, O_ST2, O_NONE, O_NONE, P_none },
  /* 1236 */ { UD_Ifxch4, O_ST3, O_NONE, O_NONE, P_none },
  /* 1237 */ { UD_Ifxch4, O_ST4, O_NONE, O_NONE, P_none },
  /* 1238 */ { UD_Ifxch4, O_ST5, O_NONE, O_NONE, P_none },
  /* 1239 */ { UD_Ifxch4, O_ST6, O_NONE, O_NONE, P_none },
  /* 1240 */ { UD_Ifxch4, O_ST7, O_NONE, O_NONE, P_none },
  /* 1241 */ { UD_Ifst, O_ST0, O_NONE, O_NONE, P_none },
  /* 1242 */ { UD_Ifst, O_ST1, O_NONE, O_NONE, P_none },
  /* 1243 */ { UD_Ifst, O_ST2, O_NONE, O_NONE, P_none },
  /* 1244 */ { UD_Ifst, O_ST3, O_NONE, O_NONE, P_none },
  /* 1245 */ { UD_Ifst, O_ST4, O_NONE, O_NONE, P_none },
  /* 1246 */ { UD_Ifst, O_ST5, O_NONE, O_NONE, P_none },
  /* 1247 */ { UD_Ifst, O_ST6, O_NONE, O_NONE, P_none },
  /* 1248 */ { UD_Ifst, O_ST7, O_NONE, O_NONE, P_none },
  /* 1249 */ { UD_Ifstp, O_ST0, O_NONE, O_NONE, P_none },
  /* 1250 */ { UD_Ifstp, O_ST1, O_NONE, O_NONE, P_none },
  /* 1251 */ { UD_Ifstp, O_ST2, O_NONE, O_NONE, P_none },
  /* 1252 */ { UD_Ifstp, O_ST3, O_NONE, O_NONE, P_none },
  /* 1253 */ { UD_Ifstp, O_ST4, O_NONE, O_NONE, P_none },
  /* 1254 */ { UD_Ifstp, O_ST5, O_NONE, O_NONE, P_none },
  /* 1255 */ { UD_Ifstp, O_ST6, O_NONE, O_NONE, P_none },
  /* 1256 */ { UD_Ifstp, O_ST7, O_NONE, O_NONE, P_none },
  /* 1257 */ { UD_Ifucom, O_ST0, O_NONE, O_NONE, P_none },
  /* 1258 */ { UD_Ifucom, O_ST1, O_NONE, O_NONE, P_none },
  /* 1259 */ { UD_Ifucom, O_ST2, O_NONE, O_NONE, P_none },
  /* 1260 */ { UD_Ifucom, O_ST3, O_NONE, O_NONE, P_none },
  /* 1261 */ { UD_Ifucom, O_ST4, O_NONE, O_NONE, P_none },
  /* 1262 */ { UD_Ifucom, O_ST5, O_NONE, O_NONE, P_none },
  /* 1263 */ { UD_Ifucom, O_ST6, O_NONE, O_NONE, P_none },
  /* 1264 */ { UD_Ifucom, O_ST7, O_NONE, O_NONE, P_none },
  /* 1265 */ { UD_Ifucomp, O_ST0, O_NONE, O_NONE, P_none },
  /* 1266 */ { UD_Ifucomp, O_ST1, O_NONE, O_NONE, P_none },
  /* 1267 */ { UD_Ifucomp, O_ST2, O_NONE, O_NONE, P_none },
  /* 1268 */ { UD_Ifucomp, O_ST3, O_NONE, O_NONE, P_none },
  /* 1269 */ { UD_Ifucomp, O_ST4, O_NONE, O_NONE, P_none },
  /* 1270 */ { UD_Ifucomp, O_ST5, O_NONE, O_NONE, P_none },
  /* 1271 */ { UD_Ifucomp, O_ST6, O_NONE, O_NONE, P_none },
  /* 1272 */ { UD_Ifucomp, O_ST7, O_NONE, O_NONE, P_none },
  /* 1273 */ { UD_Ifiadd, O_Mw, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 1274 */ { UD_Ifimul, O_Mw, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 1275 */ { UD_Ificom, O_Mw, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 1276 */ { UD_Ificomp, O_Mw, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 1277 */ { UD_Ifisub, O_Mw, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 1278 */ { UD_Ifisubr, O_Mw, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 1279 */ { UD_Ifidiv, O_Mw, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 1280 */ { UD_Ifidivr, O_Mw, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 1281 */ { UD_Ifaddp, O_ST0, O_ST0, O_NONE, P_none },
  /* 1282 */ { UD_Ifaddp, O_ST1, O_ST0, O_NONE, P_none },
  /* 1283 */ { UD_Ifaddp, O_ST2, O_ST0, O_NONE, P_none },
  /* 1284 */ { UD_Ifaddp, O_ST3, O_ST0, O_NONE, P_none },
  /* 1285 */ { UD_Ifaddp, O_ST4, O_ST0, O_NONE, P_none },
  /* 1286 */ { UD_Ifaddp, O_ST5, O_ST0, O_NONE, P_none },
  /* 1287 */ { UD_Ifaddp, O_ST6, O_ST0, O_NONE, P_none },
  /* 1288 */ { UD_Ifaddp, O_ST7, O_ST0, O_NONE, P_none },
  /* 1289 */ { UD_Ifmulp, O_ST0, O_ST0, O_NONE, P_none },
  /* 1290 */ { UD_Ifmulp, O_ST1, O_ST0, O_NONE, P_none },
  /* 1291 */ { UD_Ifmulp, O_ST2, O_ST0, O_NONE, P_none },
  /* 1292 */ { UD_Ifmulp, O_ST3, O_ST0, O_NONE, P_none },
  /* 1293 */ { UD_Ifmulp, O_ST4, O_ST0, O_NONE, P_none },
  /* 1294 */ { UD_Ifmulp, O_ST5, O_ST0, O_NONE, P_none },
  /* 1295 */ { UD_Ifmulp, O_ST6, O_ST0, O_NONE, P_none },
  /* 1296 */ { UD_Ifmulp, O_ST7, O_ST0, O_NONE, P_none },
  /* 1297 */ { UD_Ifcomp5, O_ST0, O_NONE, O_NONE, P_none },
  /* 1298 */ { UD_Ifcomp5, O_ST1, O_NONE, O_NONE, P_none },
  /* 1299 */ { UD_Ifcomp5, O_ST2, O_NONE, O_NONE, P_none },
  /* 1300 */ { UD_Ifcomp5, O_ST3, O_NONE, O_NONE, P_none },
  /* 1301 */ { UD_Ifcomp5, O_ST4, O_NONE, O_NONE, P_none },
  /* 1302 */ { UD_Ifcomp5, O_ST5, O_NONE, O_NONE, P_none },
  /* 1303 */ { UD_Ifcomp5, O_ST6, O_NONE, O_NONE, P_none },
  /* 1304 */ { UD_Ifcomp5, O_ST7, O_NONE, O_NONE, P_none },
  /* 1305 */ { UD_Ifcompp, O_NONE, O_NONE, O_NONE, P_none },
  /* 1306 */ { UD_Ifsubrp, O_ST0, O_ST0, O_NONE, P_none },
  /* 1307 */ { UD_Ifsubrp, O_ST1, O_ST0, O_NONE, P_none },
  /* 1308 */ { UD_Ifsubrp, O_ST2, O_ST0, O_NONE, P_none },
  /* 1309 */ { UD_Ifsubrp, O_ST3, O_ST0, O_NONE, P_none },
  /* 1310 */ { UD_Ifsubrp, O_ST4, O_ST0, O_NONE, P_none },
  /* 1311 */ { UD_Ifsubrp, O_ST5, O_ST0, O_NONE, P_none },
  /* 1312 */ { UD_Ifsubrp, O_ST6, O_ST0, O_NONE, P_none },
  /* 1313 */ { UD_Ifsubrp, O_ST7, O_ST0, O_NONE, P_none },
  /* 1314 */ { UD_Ifsubp, O_ST0, O_ST0, O_NONE, P_none },
  /* 1315 */ { UD_Ifsubp, O_ST1, O_ST0, O_NONE, P_none },
  /* 1316 */ { UD_Ifsubp, O_ST2, O_ST0, O_NONE, P_none },
  /* 1317 */ { UD_Ifsubp, O_ST3, O_ST0, O_NONE, P_none },
  /* 1318 */ { UD_Ifsubp, O_ST4, O_ST0, O_NONE, P_none },
  /* 1319 */ { UD_Ifsubp, O_ST5, O_ST0, O_NONE, P_none },
  /* 1320 */ { UD_Ifsubp, O_ST6, O_ST0, O_NONE, P_none },
  /* 1321 */ { UD_Ifsubp, O_ST7, O_ST0, O_NONE, P_none },
  /* 1322 */ { UD_Ifdivrp, O_ST0, O_ST0, O_NONE, P_none },
  /* 1323 */ { UD_Ifdivrp, O_ST1, O_ST0, O_NONE, P_none },
  /* 1324 */ { UD_Ifdivrp, O_ST2, O_ST0, O_NONE, P_none },
  /* 1325 */ { UD_Ifdivrp, O_ST3, O_ST0, O_NONE, P_none },
  /* 1326 */ { UD_Ifdivrp, O_ST4, O_ST0, O_NONE, P_none },
  /* 1327 */ { UD_Ifdivrp, O_ST5, O_ST0, O_NONE, P_none },
  /* 1328 */ { UD_Ifdivrp, O_ST6, O_ST0, O_NONE, P_none },
  /* 1329 */ { UD_Ifdivrp, O_ST7, O_ST0, O_NONE, P_none },
  /* 1330 */ { UD_Ifdivp, O_ST0, O_ST0, O_NONE, P_none },
  /* 1331 */ { UD_Ifdivp, O_ST1, O_ST0, O_NONE, P_none },
  /* 1332 */ { UD_Ifdivp, O_ST2, O_ST0, O_NONE, P_none },
  /* 1333 */ { UD_Ifdivp, O_ST3, O_ST0, O_NONE, P_none },
  /* 1334 */ { UD_Ifdivp, O_ST4, O_ST0, O_NONE, P_none },
  /* 1335 */ { UD_Ifdivp, O_ST5, O_ST0, O_NONE, P_none },
  /* 1336 */ { UD_Ifdivp, O_ST6, O_ST0, O_NONE, P_none },
  /* 1337 */ { UD_Ifdivp, O_ST7, O_ST0, O_NONE, P_none },
  /* 1338 */ { UD_Ifild, O_Mw, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 1339 */ { UD_Ifisttp, O_Mw, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 1340 */ { UD_Ifist, O_Mw, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 1341 */ { UD_Ifistp, O_Mw, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 1342 */ { UD_Ifbld, O_Mt, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 1343 */ { UD_Ifild, O_Mq, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 1344 */ { UD_Ifbstp, O_Mt, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 1345 */ { UD_Ifistp, O_Mq, O_NONE, O_NONE, P_aso|P_rexr|P_rexx|P_rexb },
  /* 1346 */ { UD_Iffreep, O_ST0, O_NONE, O_NONE, P_none },
  /* 1347 */ { UD_Iffreep, O_ST1, O_NONE, O_NONE, P_none },
  /* 1348 */ { UD_Iffreep, O_ST2, O_NONE, O_NONE, P_none },
  /* 1349 */ { UD_Iffreep, O_ST3, O_NONE, O_NONE, P_none },
  /* 1350 */ { UD_Iffreep, O_ST4, O_NONE, O_NONE, P_none },
  /* 1351 */ { UD_Iffreep, O_ST5, O_NONE, O_NONE, P_none },
  /* 1352 */ { UD_Iffreep, O_ST6, O_NONE, O_NONE, P_none },
  /* 1353 */ { UD_Iffreep, O_ST7, O_NONE, O_NONE, P_none },
  /* 1354 */ { UD_Ifxch7, O_ST0, O_NONE, O_NONE, P_none },
  /* 1355 */ { UD_Ifxch7, O_ST1, O_NONE, O_NONE, P_none },
  /* 1356 */ { UD_Ifxch7, O_ST2, O_NONE, O_NONE, P_none },
  /* 1357 */ { UD_Ifxch7, O_ST3, O_NONE, O_NONE, P_none },
  /* 1358 */ { UD_Ifxch7, O_ST4, O_NONE, O_NONE, P_none },
  /* 1359 */ { UD_Ifxch7, O_ST5, O_NONE, O_NONE, P_none },
  /* 1360 */ { UD_Ifxch7, O_ST6, O_NONE, O_NONE, P_none },
  /* 1361 */ { UD_Ifxch7, O_ST7, O_NONE, O_NONE, P_none },
  /* 1362 */ { UD_Ifstp8, O_ST0, O_NONE, O_NONE, P_none },
  /* 1363 */ { UD_Ifstp8, O_ST1, O_NONE, O_NONE, P_none },
  /* 1364 */ { UD_Ifstp8, O_ST2, O_NONE, O_NONE, P_none },
  /* 1365 */ { UD_Ifstp8, O_ST3, O_NONE, O_NONE, P_none },
  /* 1366 */ { UD_Ifstp8, O_ST4, O_NONE, O_NONE, P_none },
  /* 1367 */ { UD_Ifstp8, O_ST5, O_NONE, O_NONE, P_none },
  /* 1368 */ { UD_Ifstp8, O_ST6, O_NONE, O_NONE, P_none },
  /* 1369 */ { UD_Ifstp8, O_ST7, O_NONE, O_NONE, P_none },
  /* 1370 */ { UD_Ifstp9, O_ST0, O_NONE, O_NONE, P_none },
  /* 1371 */ { UD_Ifstp9, O_ST1, O_NONE, O_NONE, P_none },
  /* 1372 */ { UD_Ifstp9, O_ST2, O_NONE, O_NONE, P_none },
  /* 1373 */ { UD_Ifstp9, O_ST3, O_NONE, O_NONE, P_none },
  /* 1374 */ { UD_Ifstp9, O_ST4, O_NONE, O_NONE, P_none },
  /* 1375 */ { UD_Ifstp9, O_ST5, O_NONE, O_NONE, P_none },
  /* 1376 */ { UD_Ifstp9, O_ST6, O_NONE, O_NONE, P_none },
  /* 1377 */ { UD_Ifstp9, O_ST7, O_NONE, O_NONE, P_none },
  /* 1378 */ { UD_Ifnstsw, O_AX, O_NONE, O_NONE, P_none },
  /* 1379 */ { UD_Ifucomip, O_ST0, O_ST0, O_NONE, P_none },
  /* 1380 */ { UD_Ifucomip, O_ST0, O_ST1, O_NONE, P_none },
  /* 1381 */ { UD_Ifucomip, O_ST0, O_ST2, O_NONE, P_none },
  /* 1382 */ { UD_Ifucomip, O_ST0, O_ST3, O_NONE, P_none },
  /* 1383 */ { UD_Ifucomip, O_ST0, O_ST4, O_NONE, P_none },
  /* 1384 */ { UD_Ifucomip, O_ST0, O_ST5, O_NONE, P_none },
  /* 1385 */ { UD_Ifucomip, O_ST0, O_ST6, O_NONE, P_none },
  /* 1386 */ { UD_Ifucomip, O_ST0, O_ST7, O_NONE, P_none },
  /* 1387 */ { UD_Ifcomip, O_ST0, O_ST0, O_NONE, P_none },
  /* 1388 */ { UD_Ifcomip, O_ST0, O_ST1, O_NONE, P_none },
  /* 1389 */ { UD_Ifcomip, O_ST0, O_ST2, O_NONE, P_none },
  /* 1390 */ { UD_Ifcomip, O_ST0, O_ST3, O_NONE, P_none },
  /* 1391 */ { UD_Ifcomip, O_ST0, O_ST4, O_NONE, P_none },
  /* 1392 */ { UD_Ifcomip, O_ST0, O_ST5, O_NONE, P_none },
  /* 1393 */ { UD_Ifcomip, O_ST0, O_ST6, O_NONE, P_none },
  /* 1394 */ { UD_Ifcomip, O_ST0, O_ST7, O_NONE, P_none },
  /* 1395 */ { UD_Iloopne, O_Jb, O_NONE, O_NONE, P_none },
  /* 1396 */ { UD_Iloope, O_Jb, O_NONE, O_NONE, P_none },
  /* 1397 */ { UD_Iloop, O_Jb, O_NONE, O_NONE, P_none },
  /* 1398 */ { UD_Ijcxz, O_Jb, O_NONE, O_NONE, P_aso },
  /* 1399 */ { UD_Ijecxz, O_Jb, O_NONE, O_NONE, P_aso },
  /* 1400 */ { UD_Ijrcxz, O_Jb, O_NONE, O_NONE, P_aso },
  /* 1401 */ { UD_Iin, O_AL, O_Ib, O_NONE, P_none },
  /* 1402 */ { UD_Iin, O_eAX, O_Ib, O_NONE, P_oso },
  /* 1403 */ { UD_Iout, O_Ib, O_AL, O_NONE, P_none },
  /* 1404 */ { UD_Iout, O_Ib, O_eAX, O_NONE, P_oso },
  /* 1405 */ { UD_Icall, O_Jz, O_NONE, O_NONE, P_oso|P_def64 },
  /* 1406 */ { UD_Ijmp, O_Jz, O_NONE, O_NONE, P_oso|P_def64 },
  /* 1407 */ { UD_Ijmp, O_Av, O_NONE, O_NONE, P_oso },
  /* 1408 */ { UD_Ijmp, O_Jb, O_NONE, O_NONE, P_def64 },
  /* 1409 */ { UD_Iin, O_AL, O_DX, O_NONE, P_none },
  /* 1410 */ { UD_Iin, O_eAX, O_DX, O_NONE, P_oso },
  /* 1411 */ { UD_Iout, O_DX, O_AL, O_NONE, P_none },
  /* 1412 */ { UD_Iout, O_DX, O_eAX, O_NONE, P_oso },
  /* 1413 */ { UD_Ilock, O_NONE, O_NONE, O_NONE, P_none },
  /* 1414 */ { UD_Iint1, O_NONE, O_NONE, O_NONE, P_none },
  /* 1415 */ { UD_Irepne, O_NONE, O_NONE, O_NONE, P_none },
  /* 1416 */ { UD_Irep, O_NONE, O_NONE, O_NONE, P_none },
  /* 1417 */ { UD_Ihlt, O_NONE, O_NONE, O_NONE, P_none },
  /* 1418 */ { UD_Icmc, O_NONE, O_NONE, O_NONE, P_none },
  /* 1419 */ { UD_Itest, O_Eb, O_Ib, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 1420 */ { UD_Itest, O_Eb, O_Ib, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 1421 */ { UD_Inot, O_Eb, O_NONE, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 1422 */ { UD_Ineg, O_Eb, O_NONE, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 1423 */ { UD_Imul, O_Eb, O_NONE, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 1424 */ { UD_Iimul, O_Eb, O_NONE, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 1425 */ { UD_Idiv, O_Eb, O_NONE, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 1426 */ { UD_Iidiv, O_Eb, O_NONE, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 1427 */ { UD_Itest, O_Ev, O_sIz, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 1428 */ { UD_Itest, O_Ev, O_Iz, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 1429 */ { UD_Inot, O_Ev, O_NONE, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 1430 */ { UD_Ineg, O_Ev, O_NONE, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 1431 */ { UD_Imul, O_Ev, O_NONE, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 1432 */ { UD_Iimul, O_Ev, O_NONE, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 1433 */ { UD_Idiv, O_Ev, O_NONE, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 1434 */ { UD_Iidiv, O_Ev, O_NONE, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 1435 */ { UD_Iclc, O_NONE, O_NONE, O_NONE, P_none },
  /* 1436 */ { UD_Istc, O_NONE, O_NONE, O_NONE, P_none },
  /* 1437 */ { UD_Icli, O_NONE, O_NONE, O_NONE, P_none },
  /* 1438 */ { UD_Isti, O_NONE, O_NONE, O_NONE, P_none },
  /* 1439 */ { UD_Icld, O_NONE, O_NONE, O_NONE, P_none },
  /* 1440 */ { UD_Istd, O_NONE, O_NONE, O_NONE, P_none },
  /* 1441 */ { UD_Iinc, O_Eb, O_NONE, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 1442 */ { UD_Idec, O_Eb, O_NONE, O_NONE, P_aso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 1443 */ { UD_Iinc, O_Ev, O_NONE, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 1444 */ { UD_Idec, O_Ev, O_NONE, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 1445 */ { UD_Icall, O_Ev, O_NONE, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 1446 */ { UD_Icall, O_Eq, O_NONE, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb|P_def64 },
  /* 1447 */ { UD_Icall, O_Fv, O_NONE, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 1448 */ { UD_Ijmp, O_Ev, O_NONE, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb|P_def64 },
  /* 1449 */ { UD_Ijmp, O_Fv, O_NONE, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb },
  /* 1450 */ { UD_Ipush, O_Ev, O_NONE, O_NONE, P_aso|P_oso|P_rexw|P_rexr|P_rexx|P_rexb|P_def64 },
};


const char * ud_mnemonics_str[] = {
"invalid",
    "3dnow",
    "none",
    "db",
    "pause",
    "aaa",
    "aad",
    "aam",
    "aas",
    "adc",
    "add",
    "addpd",
    "addps",
    "addsd",
    "addss",
    "and",
    "andpd",
    "andps",
    "andnpd",
    "andnps",
    "arpl",
    "movsxd",
    "bound",
    "bsf",
    "bsr",
    "bswap",
    "bt",
    "btc",
    "btr",
    "bts",
    "call",
    "cbw",
    "cwde",
    "cdqe",
    "clc",
    "cld",
    "clflush",
    "clgi",
    "cli",
    "clts",
    "cmc",
    "cmovo",
    "cmovno",
    "cmovb",
    "cmovae",
    "cmovz",
    "cmovnz",
    "cmovbe",
    "cmova",
    "cmovs",
    "cmovns",
    "cmovp",
    "cmovnp",
    "cmovl",
    "cmovge",
    "cmovle",
    "cmovg",
    "cmp",
    "cmppd",
    "cmpps",
    "cmpsb",
    "cmpsw",
    "cmpsd",
    "cmpsq",
    "cmpss",
    "cmpxchg",
    "cmpxchg8b",
    "cmpxchg16b",
    "comisd",
    "comiss",
    "cpuid",
    "cvtdq2pd",
    "cvtdq2ps",
    "cvtpd2dq",
    "cvtpd2pi",
    "cvtpd2ps",
    "cvtpi2ps",
    "cvtpi2pd",
    "cvtps2dq",
    "cvtps2pi",
    "cvtps2pd",
    "cvtsd2si",
    "cvtsd2ss",
    "cvtsi2ss",
    "cvtss2si",
    "cvtss2sd",
    "cvttpd2pi",
    "cvttpd2dq",
    "cvttps2dq",
    "cvttps2pi",
    "cvttsd2si",
    "cvtsi2sd",
    "cvttss2si",
    "cwd",
    "cdq",
    "cqo",
    "daa",
    "das",
    "dec",
    "div",
    "divpd",
    "divps",
    "divsd",
    "divss",
    "emms",
    "enter",
    "f2xm1",
    "fabs",
    "fadd",
    "faddp",
    "fbld",
    "fbstp",
    "fchs",
    "fclex",
    "fcmovb",
    "fcmove",
    "fcmovbe",
    "fcmovu",
    "fcmovnb",
    "fcmovne",
    "fcmovnbe",
    "fcmovnu",
    "fucomi",
    "fcom",
    "fcom2",
    "fcomp3",
    "fcomi",
    "fucomip",
    "fcomip",
    "fcomp",
    "fcomp5",
    "fcompp",
    "fcos",
    "fdecstp",
    "fdiv",
    "fdivp",
    "fdivr",
    "fdivrp",
    "femms",
    "ffree",
    "ffreep",
    "ficom",
    "ficomp",
    "fild",
    "fincstp",
    "fninit",
    "fiadd",
    "fidivr",
    "fidiv",
    "fisub",
    "fisubr",
    "fist",
    "fistp",
    "fisttp",
    "fld",
    "fld1",
    "fldl2t",
    "fldl2e",
    "fldpi",
    "fldlg2",
    "fldln2",
    "fldz",
    "fldcw",
    "fldenv",
    "fmul",
    "fmulp",
    "fimul",
    "fnop",
    "fpatan",
    "fprem",
    "fprem1",
    "fptan",
    "frndint",
    "frstor",
    "fnsave",
    "fscale",
    "fsin",
    "fsincos",
    "fsqrt",
    "fstp",
    "fstp1",
    "fstp8",
    "fstp9",
    "fst",
    "fnstcw",
    "fnstenv",
    "fnstsw",
    "fsub",
    "fsubp",
    "fsubr",
    "fsubrp",
    "ftst",
    "fucom",
    "fucomp",
    "fucompp",
    "fxam",
    "fxch",
    "fxch4",
    "fxch7",
    "fxrstor",
    "fxsave",
    "fxtract",
    "fyl2x",
    "fyl2xp1",
    "hlt",
    "idiv",
    "in",
    "imul",
    "inc",
    "insb",
    "insw",
    "insd",
    "int1",
    "int3",
    "int",
    "into",
    "invd",
    "invept",
    "invlpg",
    "invlpga",
    "invvpid",
    "iretw",
    "iretd",
    "iretq",
    "jo",
    "jno",
    "jb",
    "jae",
    "jz",
    "jnz",
    "jbe",
    "ja",
    "js",
    "jns",
    "jp",
    "jnp",
    "jl",
    "jge",
    "jle",
    "jg",
    "jcxz",
    "jecxz",
    "jrcxz",
    "jmp",
    "lahf",
    "lar",
    "lddqu",
    "ldmxcsr",
    "lds",
    "lea",
    "les",
    "lfs",
    "lgs",
    "lidt",
    "lss",
    "leave",
    "lfence",
    "lgdt",
    "lldt",
    "lmsw",
    "lock",
    "lodsb",
    "lodsw",
    "lodsd",
    "lodsq",
    "loopne",
    "loope",
    "loop",
    "lsl",
    "ltr",
    "maskmovq",
    "maxpd",
    "maxps",
    "maxsd",
    "maxss",
    "mfence",
    "minpd",
    "minps",
    "minsd",
    "minss",
    "monitor",
    "montmul",
    "mov",
    "movapd",
    "movaps",
    "movd",
    "movhpd",
    "movhps",
    "movlhps",
    "movlpd",
    "movlps",
    "movhlps",
    "movmskpd",
    "movmskps",
    "movntdq",
    "movnti",
    "movntpd",
    "movntps",
    "movntq",
    "movq",
    "movsb",
    "movsw",
    "movsd",
    "movsq",
    "movss",
    "movsx",
    "movupd",
    "movups",
    "movzx",
    "mul",
    "mulpd",
    "mulps",
    "mulsd",
    "mulss",
    "mwait",
    "neg",
    "nop",
    "not",
    "or",
    "orpd",
    "orps",
    "out",
    "outsb",
    "outsw",
    "outsd",
    "packsswb",
    "packssdw",
    "packuswb",
    "paddb",
    "paddw",
    "paddd",
    "paddsb",
    "paddsw",
    "paddusb",
    "paddusw",
    "pand",
    "pandn",
    "pavgb",
    "pavgw",
    "pcmpeqb",
    "pcmpeqw",
    "pcmpeqd",
    "pcmpgtb",
    "pcmpgtw",
    "pcmpgtd",
    "pextrb",
    "pextrd",
    "pextrq",
    "pextrw",
    "pinsrb",
    "pinsrw",
    "pinsrd",
    "pinsrq",
    "pmaddwd",
    "pmaxsw",
    "pmaxub",
    "pminsw",
    "pminub",
    "pmovmskb",
    "pmulhuw",
    "pmulhw",
    "pmullw",
    "pop",
    "popa",
    "popad",
    "popfw",
    "popfd",
    "popfq",
    "por",
    "prefetch",
    "prefetchnta",
    "prefetcht0",
    "prefetcht1",
    "prefetcht2",
    "psadbw",
    "pshufw",
    "psllw",
    "pslld",
    "psllq",
    "psraw",
    "psrad",
    "psrlw",
    "psrld",
    "psrlq",
    "psubb",
    "psubw",
    "psubd",
    "psubsb",
    "psubsw",
    "psubusb",
    "psubusw",
    "punpckhbw",
    "punpckhwd",
    "punpckhdq",
    "punpcklbw",
    "punpcklwd",
    "punpckldq",
    "pi2fw",
    "pi2fd",
    "pf2iw",
    "pf2id",
    "pfnacc",
    "pfpnacc",
    "pfcmpge",
    "pfmin",
    "pfrcp",
    "pfrsqrt",
    "pfsub",
    "pfadd",
    "pfcmpgt",
    "pfmax",
    "pfrcpit1",
    "pfrsqit1",
    "pfsubr",
    "pfacc",
    "pfcmpeq",
    "pfmul",
    "pfrcpit2",
    "pmulhrw",
    "pswapd",
    "pavgusb",
    "push",
    "pusha",
    "pushad",
    "pushfw",
    "pushfd",
    "pushfq",
    "pxor",
    "rcl",
    "rcr",
    "rol",
    "ror",
    "rcpps",
    "rcpss",
    "rdmsr",
    "rdpmc",
    "rdtsc",
    "rdtscp",
    "repne",
    "rep",
    "ret",
    "retf",
    "rsm",
    "rsqrtps",
    "rsqrtss",
    "sahf",
    "salc",
    "sar",
    "shl",
    "shr",
    "sbb",
    "scasb",
    "scasw",
    "scasd",
    "scasq",
    "seto",
    "setno",
    "setb",
    "setae",
    "setz",
    "setnz",
    "setbe",
    "seta",
    "sets",
    "setns",
    "setp",
    "setnp",
    "setl",
    "setge",
    "setle",
    "setg",
    "sfence",
    "sgdt",
    "shld",
    "shrd",
    "shufpd",
    "shufps",
    "sidt",
    "sldt",
    "smsw",
    "sqrtps",
    "sqrtpd",
    "sqrtsd",
    "sqrtss",
    "stc",
    "std",
    "stgi",
    "sti",
    "skinit",
    "stmxcsr",
    "stosb",
    "stosw",
    "stosd",
    "stosq",
    "str",
    "sub",
    "subpd",
    "subps",
    "subsd",
    "subss",
    "swapgs",
    "syscall",
    "sysenter",
    "sysexit",
    "sysret",
    "test",
    "ucomisd",
    "ucomiss",
    "ud2",
    "unpckhpd",
    "unpckhps",
    "unpcklps",
    "unpcklpd",
    "verr",
    "verw",
    "vmcall",
    "vmclear",
    "vmxon",
    "vmptrld",
    "vmptrst",
    "vmlaunch",
    "vmresume",
    "vmxoff",
    "vmread",
    "vmwrite",
    "vmrun",
    "vmmcall",
    "vmload",
    "vmsave",
    "wait",
    "wbinvd",
    "wrmsr",
    "xadd",
    "xchg",
    "xgetbv",
    "xlatb",
    "xor",
    "xorpd",
    "xorps",
    "xcryptecb",
    "xcryptcbc",
    "xcryptctr",
    "xcryptcfb",
    "xcryptofb",
    "xrstor",
    "xsave",
    "xsetbv",
    "xsha1",
    "xsha256",
    "xstore",
    "aesdec",
    "aesdeclast",
    "aesenc",
    "aesenclast",
    "aesimc",
    "aeskeygenassist",
    "pclmulqdq",
    "getsec",
    "movdqa",
    "maskmovdqu",
    "movdq2q",
    "movdqu",
    "movq2dq",
    "paddq",
    "psubq",
    "pmuludq",
    "pshufhw",
    "pshuflw",
    "pshufd",
    "pslldq",
    "psrldq",
    "punpckhqdq",
    "punpcklqdq",
    "addsubpd",
    "addsubps",
    "haddpd",
    "haddps",
    "hsubpd",
    "hsubps",
    "movddup",
    "movshdup",
    "movsldup",
    "pabsb",
    "pabsw",
    "pabsd",
    "pshufb",
    "phaddw",
    "phaddd",
    "phaddsw",
    "pmaddubsw",
    "phsubw",
    "phsubd",
    "phsubsw",
    "psignb",
    "psignd",
    "psignw",
    "pmulhrsw",
    "palignr",
    "pblendvb",
    "pmuldq",
    "pminsb",
    "pminsd",
    "pminuw",
    "pminud",
    "pmaxsb",
    "pmaxsd",
    "pmaxud",
    "pmaxuw",
    "pmulld",
    "phminposuw",
    "roundps",
    "roundpd",
    "roundss",
    "roundsd",
    "blendpd",
    "pblendw",
    "blendps",
    "blendvpd",
    "blendvps",
    "dpps",
    "dppd",
    "mpsadbw",
    "extractps",
    "insertps",
    "movntdqa",
    "packusdw",
    "pmovsxbw",
    "pmovsxbd",
    "pmovsxbq",
    "pmovsxwd",
    "pmovsxwq",
    "pmovsxdq",
    "pmovzxbw",
    "pmovzxbd",
    "pmovzxbq",
    "pmovzxwd",
    "pmovzxwq",
    "pmovzxdq",
    "pcmpeqq",
    "popcnt",
    "ptest",
    "pcmpestri",
    "pcmpestrm",
    "pcmpgtq",
    "pcmpistri",
    "pcmpistrm",
    "movbe",
    "crc32"
};

/* END itab.c */
