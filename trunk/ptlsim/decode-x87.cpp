//
// PTLsim: Cycle Accurate x86-64 Simulator
// Decoder for x87 FPU instructions
//
// Copyright 1999-2006 Matt T. Yourst <yourst@yourst.com>
//

#include <decode.h>

//
// x87 assists
//

#define FP_STACK_MASK 0x3f

void assist_x87_fprem(Context& ctx) {
  assert(false);
  ctx.commitarf[REG_rip] = ctx.commitarf[REG_nextrip];
}

#define make_two_input_x87_func_with_pop(name, expr) \
void assist_x87_##name(Context& ctx) { \
  W64& tos = ctx.commitarf[REG_fptos]; \
  W64& st0 = ctx.fpstack[tos >> 3]; \
  W64& st1 = ctx.fpstack[((tos >> 3) + 1) & 0x7]; \
  SSEType st0u(st0); SSEType st1u(st1); \
  (expr); \
  st1 = st1u.w64; \
  X87StatusWord* sw = (X87StatusWord*)&ctx.commitarf[REG_fpsw]; \
  sw->c1 = 0; sw->c2 = 0; \
  clearbit(ctx.commitarf[REG_fptags], tos); \
  tos = (tos + 8) & FP_STACK_MASK; \
  ctx.commitarf[REG_rip] = ctx.commitarf[REG_nextrip]; \
}

// Fix macro problems:
#define old_log2 log2
#undef log2

//
// NOTE: Under no circumstances can we let out-of-range values get passed to the
// standard library math functions! It will screw up the x87 FPU state inside
// PTLsim and we may never recover. Instead, we take control right here.
//

double x87_fyl2xp1(double st1, double st0) {
  double stout;
  asm("fldl %[st1]; fldl %[st0]; fyl2xp1; fstpl %[stout];" : [stout] "=m" (stout) : [st0] "m" (st0), [st1] "m" (st1));
  return stout;
}

double x87_fyl2x(double st1, double st0) {
  double stout;
  asm("fldl %[st1]; fldl %[st0]; fyl2x; fstpl %[stout];" : [stout] "=m" (stout) : [st0] "m" (st0), [st1] "m" (st1));
  return stout;
}

double x87_fpatan(double st1, double st0) {
  double stout;
  asm("fldl %[st1]; fldl %[st0]; fpatan; fstpl %[stout];" : [stout] "=m" (stout) : [st0] "m" (st0), [st1] "m" (st1));
  return stout;
}
 
// st(1) = st(1) * log2(st(0)) and pop st(0)
make_two_input_x87_func_with_pop(fyl2x, st1u.d = x87_fyl2x(st1u.d, st0u.d));

// st(1) = st(1) * log2(st(0) + 1.0) and pop st(0)
// This insn has very strange semantics: if (st0 < -1.0), 
// such that ((st0 + 1.0) < 0), rather than return NaN, it
// returns the old value in st0 but still pops the stack.
make_two_input_x87_func_with_pop(fyl2xp1, st1u.d = x87_fyl2xp1(st1u.d, st0u.d));

// st(1) = arctan(st(1) / st(0))
make_two_input_x87_func_with_pop(fpatan, st1u.d = x87_fpatan(st1u.d, st0u.d));

void assist_x87_fscale(Context& ctx) {
  W64& tos = ctx.commitarf[REG_fptos];
  W64& st0 = ctx.fpstack[tos >> 3];
  W64& st1 = ctx.fpstack[((tos >> 3) + 1) & 0x7];
  SSEType st0u(st0); SSEType st1u(st1);
  st0u.d = st0u.d * math::exp2(math::trunc(st1u.d));
  st0 = st0u.w64;
  X87StatusWord* sw = (X87StatusWord*)&ctx.commitarf[REG_fpsw];
  sw->c1 = 0; sw->c2 = 0;
  ctx.commitarf[REG_rip] = ctx.commitarf[REG_nextrip];
}

#define log2 old_log2

#define make_unary_x87_func(name, expr) \
void assist_x87_##name(Context& ctx) { \
  W64& r = ctx.fpstack[ctx.commitarf[REG_fptos] >> 3]; \
  SSEType ra(r); ra.d = (expr); r = ra.w64; \
  X87StatusWord* sw = (X87StatusWord*)&ctx.commitarf[REG_fpsw]; \
  sw->c1 = 0; sw->c2 = 0; \
  ctx.commitarf[REG_rip] = ctx.commitarf[REG_nextrip]; \
}

make_unary_x87_func(fsqrt, math::sqrt(ra.d));
make_unary_x87_func(fsin, math::sin(ra.d));
make_unary_x87_func(fcos, math::cos(ra.d));
make_unary_x87_func(f2xm1, math::exp2(ra.d) - 1);

void assist_x87_frndint(Context& ctx) {
  W64& r = ctx.fpstack[ctx.commitarf[REG_fptos] >> 3];
  SSEType ra(r);
  switch (ctx.fpcw.rc) {
  case 0: // round to nearest (round)
    ra.d = math::round(ra.d); break;
  case 1: // round down (floor)
    ra.d = math::floor(ra.d); break;
  case 2: // round up (ceil)
    ra.d = math::ceil(ra.d); break;
  case 3: // round towards zero (trunc)
    ra.d = math::trunc(ra.d); break;
  }
  r = ra.w64;
  X87StatusWord* sw = (X87StatusWord*)&ctx.commitarf[REG_fpsw];
  sw->c1 = 0; sw->c2 = 0;
  ctx.commitarf[REG_rip] = ctx.commitarf[REG_nextrip];
}

#define make_two_output_x87_func_with_push(name, expr) \
void assist_x87_##name(Context& ctx) { \
  W64& tos = ctx.commitarf[REG_fptos]; \
  W64& st0 = ctx.fpstack[tos >> 3]; \
  W64& st1 = ctx.fpstack[((tos >> 3) - 1) & 0x7]; \
  SSEType st0u(st0); SSEType st1u(st1); \
  expr; \
  st0 = st0u.w64; st1 = st1u.w64; \
  X87StatusWord* sw = (X87StatusWord*)&ctx.commitarf[REG_fpsw]; \
  sw->c1 = 0; sw->c2 = 0; \
  tos = (tos - 8) & FP_STACK_MASK; \
  setbit(ctx.commitarf[REG_fptags], tos); \
  ctx.commitarf[REG_rip] = ctx.commitarf[REG_nextrip]; \
}

// st(0) = sin(st(0)) and push cos(orig st(0))
make_two_output_x87_func_with_push(fsincos, (st1u.d = math::cos(st0u.d), st0u.d = math::sin(st0u.d)));

// st(0) = tan(st(0)) and push value 1.0
make_two_output_x87_func_with_push(fptan, (st1u.d = 1.0, st0u.d = math::tan(st0u.d)));

make_two_output_x87_func_with_push(fxtract, (st1u.d = math::significand(st0u.d), st0u.d = math::ilogb(st0u.d)));

void assist_x87_fprem1(Context& ctx) {
  W64& tos = ctx.commitarf[REG_fptos];
  W64& st0 = ctx.fpstack[tos >> 3];
  W64& st1 = ctx.fpstack[((tos >> 3) - 1) & 0x7];
  SSEType st0u(st0); SSEType st1u(st1);

  X87StatusWord fpsw;
  asm("fldl %[st1]; fldl %[st0]; fprem1; fstsw %%ax; fstpl %[st0]; ffree %%st(0); fincstp;" : [st0] "+m" (st0u.d), "=a" (fpsw) : [st1] "m" (st1u.d));
  st0 = st0u.w64;

  X87StatusWord* sw = (X87StatusWord*)&ctx.commitarf[REG_fpsw];
  sw->c0 = fpsw.c0;
  sw->c1 = fpsw.c1;
  sw->c2 = fpsw.c2;
  sw->c3 = fpsw.c3;
  ctx.commitarf[REG_rip] = ctx.commitarf[REG_nextrip];
}

void assist_x87_fxam(Context& ctx) {
  W64& r = ctx.fpstack[ctx.commitarf[REG_fptos] >> 3];
  SSEType ra(r);

  X87StatusWord fpsw;
  asm("fxam; fstsw %%ax" : "=a" (fpsw) : "t" (ra.d));

  X87StatusWord* sw = (X87StatusWord*)&ctx.commitarf[REG_fpsw];
  sw->c0 = fpsw.c0;
  sw->c1 = fpsw.c1;
  sw->c2 = fpsw.c2;
  sw->c3 = fpsw.c3;
  ctx.commitarf[REG_rip] = ctx.commitarf[REG_nextrip];
}

// We need a general purpose "copy from user virtual addresses" function

void assist_x87_fld80(Context& ctx) {
  // Virtual address is in sr2
  Waddr addr = ctx.commitarf[REG_ar1];
  X87Reg data;

  PageFaultErrorCode pfec;
  Waddr faultaddr;
  int bytes = ctx.copy_from_user(data, addr, sizeof(X87Reg), pfec, faultaddr);

  if (bytes < sizeof(X87Reg)) {
    propagate_exception_during_assist(ctx, EXCEPTION_x86_page_fault, pfec, faultaddr);
    return;
  }

  // Push on stack
  W64& tos = ctx.commitarf[REG_fptos];
  tos = (tos - 8) & FP_STACK_MASK;
  ctx.fpstack[tos >> 3] = x87_fp_80bit_to_64bit(&data);
  setbit(ctx.commitarf[REG_fptags], tos);
  ctx.commitarf[REG_rip] = ctx.commitarf[REG_nextrip];
}

void assist_x87_fstp80(Context& ctx) {
  // Store and pop from stack
  W64& tos = ctx.commitarf[REG_fptos];
  X87Reg data;
  x87_fp_64bit_to_80bit(&data, ctx.fpstack[tos >> 3]);

  // Virtual address is in sr2
  Waddr addr = ctx.commitarf[REG_ar1];

  PageFaultErrorCode pfec;
  Waddr faultaddr;
  int bytes = ctx.copy_to_user(addr, data, sizeof(X87Reg), pfec, faultaddr);

  if (bytes < sizeof(X87Reg)) {
    propagate_exception_during_assist(ctx, EXCEPTION_x86_page_fault, pfec, faultaddr);
    return;
  }

  clearbit(ctx.commitarf[REG_fptags], tos);
  tos = (tos + 8) & FP_STACK_MASK;
  ctx.commitarf[REG_rip] = ctx.commitarf[REG_nextrip];
}


//
// FCOM and FCOMP (with pop) need to transform SSE-style flags
// into x87 flags and write those flags to fpsw:
//
// COMISD semantics:
// ZF PF CF
// 0  0  0     ra > rb
// 0  0  1     ra < rb
// 1  0  0     ra == rb
// 1  1  1     unordered
//
// x87 FCOM semantics:
// C3 C2 C1 C0
// ZF PF -- CF
// 0  0  0  0  ra > rb
// 0  0  0  1  ra < rb
// 1  0  0  0  ra == rb
// 1  1  0  1  unordered
//
// Notice the pattern: ZF,PF,CF -> C3,C2,C0 (skipping C1)
//
//                           *         *    *
// Flags format: OF - - - SF ZF - AF - PF - CF
//               11       7  6    4    2    0
//               rb       ra ra   ra   ra   rb
//
//
// FPSW format:
//
//  b C3 (TOS) C2 C1 C0 es sf pe ue oe ze de ie
// 15 14 13-11 10  9  8  7  6  5  4  3  2  1  0
//
// SF ZF -- AF -- PF -- CF
//
// Mapping table: 128 (bits 6:0) -> 4 (x87 flag bits)
//
//
// #include <ptlhwdef.h>
//
// void gen_cmpccf_to_fcom_flags() {
//
//   foreach (v, 128) {
//      bool zf = bit(v, log2(FLAG_ZF));
//      bool pf = bit(v, log2(FLAG_PF));
//      bool cf = bit(v, log2(FLAG_CF));
//
//      bool c3 = zf;
//      bool c2 = pf;
//      bool c1 = 0;
//      bool c0 = cf;
//
//      int out = (c3<<3) | (c2<<2) | (c1<<1) | (c0<<0);
//
//      if ((v % 16) == 0) cout << "  ";
//      cout << intstring(out, 2), ", ";
//      if ((v % 16) == 15) cout << endl;
//      }
//    }
//

static const byte translate_cmpccf_to_x87[128] = {
  0,  1,  0,  1,  4,  5,  4,  5,  0,  1,  0,  1,  4,  5,  4,  5,
  0,  1,  0,  1,  4,  5,  4,  5,  0,  1,  0,  1,  4,  5,  4,  5,
  0,  1,  0,  1,  4,  5,  4,  5,  0,  1,  0,  1,  4,  5,  4,  5,
  0,  1,  0,  1,  4,  5,  4,  5,  0,  1,  0,  1,  4,  5,  4,  5,
  8,  9,  8,  9, 12, 13, 12, 13,  8,  9,  8,  9, 12, 13, 12, 13,
  8,  9,  8,  9, 12, 13, 12, 13,  8,  9,  8,  9, 12, 13, 12, 13,
  8,  9,  8,  9, 12, 13, 12, 13,  8,  9,  8,  9, 12, 13, 12, 13,
  8,  9,  8,  9, 12, 13, 12, 13,  8,  9,  8,  9, 12, 13, 12, 13,
};

W64 warned_about_x87 = 0;

void check_warned_about_x87() {
  warned_about_x87++;
  if (warned_about_x87 == 16) {
    stringbuf sb;
    sb << endl,
      "//", endl,
      "// NOTE: This program is using a lot of legacy x87 floating point", endl, 
      "// at ", total_user_insns_committed, " commits, ", sim_cycle, " cycles.", endl,
      "// PTLsim executes x87 code very sub-optimally: it is HIGHLY recommended", endl,
      "// that you recompile the program with SSE/SSE2 support and/or update", endl,
      "// the standard libraries (libc, libm) to an SSE/SSE2-specific version.", endl, 
      "//", endl, endl;
    logfile << sb;
    cerr << sb;
  }
}

bool TraceDecoder::decode_x87() {
  DecodedOperand rd;
  DecodedOperand ra;

  switch (op) {
    //
    // x87 floating point
    //
    // Internal encoding:
    //
    // op = 0x600 | (lowbits(op, 3) << 4) | modrm.reg;
    //
    // 0x600 (0xd8): fadd fmul fcom fcomp fsub fsubr fdiv fdivr 
    // 0x610 (0xd9): fld32 inv fst fstp fldenv fldcw fnstenv fnstcw
    // | (if mod=11) fldreg fxch fnop (n/a) [fchs|fabs|ftst|fxam] fldCONST [f2xm1 fyl2x fptan tpatan fxtract fprem1 fdecstp fincstp] [fprem fyl2xp1 fsqrt fsincos frndint fscale fsin fcos]
    // 0x620 (0xda): fcmovb fcmove fcmovbe fcmovu (inv) (inv|fucompp) (inv) (inv)
    // 0x630 (0xdb): fcmovnb fcmovne fcmovnbe fcmovnu (inv|fnclex|fninit) fucomi fcomi (inv)
    // 0x640 (0xdc): fadd fmul fcom fcomp fsub fsubr fdiv fdivr
    // 0x650 (0xdd): fld64 fisttp fst fstp frstor inv fnsave fnstsw / (if mod=11) ffree (inv) fst fstp fucom fucomp (inv) (inv)
    // 0x660 (0xde): faddp fmulp (inv) (inv) fsubrp fsubp fdivrp fdivp [or fixxx for !11]
    // 0x670 (0xdf): fild fisttp fist fistp fbld fild fbstp fistp
    //

  case 0x600 ... 0x607: // 0xd8 xx: st(0) = st(0) OP [mem32 | st(modrm.rm)]
  case 0x640 ... 0x647: // 0xdc xx: st(modrm.rm) = st(modrm.rm) OP [mem64 | st(0)]
  case 0x660 ... 0x667: { // 0xde xx: [st(modrm.rm) = st(modrm.rm) OP st(0) and pop] | [st(0) = st(0) - mem16int (fiOP)]
    bool d8form = ((op >> 4) == 0x60);
    bool dcform = ((op >> 4) == 0x64);
    bool deform = ((op >> 4) == 0x66);
    bool memform = (modrm.mod != 3);
    int x87op = modrm.reg;
    DECODE(eform, ra, (deform) ? w_mode : (dcform) ? q_mode : d_mode);
    CheckInvalid();
    TransOp ldpra(OP_ld, REG_temp0, REG_fptos, REG_imm, REG_zero, 3, (Waddr)&ctx.fpstack); ldpra.internal = 1; this << ldpra;

    this << TransOp(OP_addm, REG_temp2, REG_fptos, REG_imm, REG_imm, 3, 8*modrm.rm, FP_STACK_MASK);

    if (!memform) {
      TransOp ldprb(OP_ld, REG_temp1, REG_temp2, REG_imm, REG_zero, 3, (Waddr)&ctx.fpstack); ldprb.internal = 1; this << ldprb;
    } else {
      operand_load(REG_temp1, ra, OP_ldx, (d8form) ? DATATYPE_DOUBLE : DATATYPE_INT);
      if (d8form) this << TransOp(OP_cvtf_s2d_lo, REG_temp1, REG_zero, REG_temp1, REG_zero, 2);
      if (deform) this << TransOp(OP_cvtf_q2d, REG_temp1, REG_zero, REG_temp1, REG_zero, 2);
    }
    // fadd fmul fcom fcomp fsub fsubr fdiv fdivr

    static const int x87_translate_opcode[8] = {OP_addf, OP_mulf, OP_cmpccf, OP_cmpccf, OP_subf, OP_subf, OP_divf, OP_divf};
    // the "r" variants have ra and rb flipped:
    static const int x87_d8da_translate_ra[8] = {REG_temp0, REG_temp0, REG_temp0, REG_temp0,  REG_temp0, REG_temp1, REG_temp0, REG_temp1};
    static const int x87_d8da_translate_rb[8] = {REG_temp1, REG_temp1, REG_temp1, REG_temp1,  REG_temp1, REG_temp0, REG_temp1, REG_temp0};
    // incredibly, the 0xdc and 0xde encodings have some of these flips reversed!
    static const int x87_dcde_translate_ra[8] = {REG_temp0, REG_temp0, REG_temp0, REG_temp0,  REG_temp0, REG_temp1, REG_temp0, REG_temp1};
    static const int x87_dcde_translate_rb[8] = {REG_temp1, REG_temp1, REG_temp1, REG_temp1,  REG_temp1, REG_temp0, REG_temp1, REG_temp0};

    int translated_opcode = x87_translate_opcode[x87op];
    int ra = ((dcform|deform) & (!memform)) ? x87_dcde_translate_ra[x87op] : x87_d8da_translate_ra[x87op];
    int rb = ((dcform|deform) & (!memform)) ? x87_dcde_translate_rb[x87op] : x87_d8da_translate_rb[x87op];
    if (logable(1)) logfile << ripstart, ": op ", (void*)(Waddr)op, ", x87op ", x87op, ", ra ", arch_reg_names[ra], ", rb ", arch_reg_names[rb], endl;
    TransOp uop(translated_opcode, REG_temp0, ra, rb, REG_zero, 2); uop.datatype = DATATYPE_DOUBLE; this << uop;

    if (translated_opcode == OP_cmpccf) {
      TransOp ldpxlate(OP_ld, REG_temp0, REG_temp0, REG_imm, REG_zero, 0, (Waddr)&translate_cmpccf_to_x87); ldpxlate.internal = 1; this << ldpxlate;
      this << TransOp(OP_mask, REG_fpsw, REG_fpsw, REG_temp0, REG_imm, 3, 0, MaskControlInfo((64-8), 3, (64-8)));
      this << TransOp(OP_mask, REG_fpsw, REG_fpsw, REG_temp0, REG_imm, 3, 0, MaskControlInfo((64-14), 1, (64-11)));
      // FCOMP requires pop from stack
      if (x87op == 3) {
        this << TransOp(OP_btr, REG_fptags, REG_fptags, REG_fptos, REG_zero, 3); // pop: adjust tag word
        this << TransOp(OP_addm, REG_fptos, REG_fptos, REG_imm, REG_imm, 3, 8, FP_STACK_MASK); // pop
      }
    } else {
      TransOp stp(OP_st, REG_mem, ((dcform|deform) & (!memform)) ? REG_temp2 : REG_fptos, REG_imm, REG_temp0, 3, (Waddr)&ctx.fpstack); stp.internal = 1; this << stp;
      this << TransOp(OP_bts, REG_fptags, REG_fptags, ((dcform|deform) & (!memform)) ? REG_temp2 : REG_fptos, REG_zero, 3);
    }
    if (deform & (!memform)) {
      this << TransOp(OP_btr, REG_fptags, REG_fptags, REG_fptos, REG_zero, 3); // pop: adjust tag word
      this << TransOp(OP_addm, REG_fptos, REG_fptos, REG_imm, REG_imm, 3, 8, FP_STACK_MASK); // pop stack
    }

    check_warned_about_x87();

    break;
  }

  case 0x654 ... 0x655: { // 0xdd xx: fpflags = st(0) fucom st(modrm.rm); optional pop
    if (modrm.mod != 3) MakeInvalid();
    CheckInvalid();

    bool pop = bit(op, 0);
    TransOp ldpra(OP_ld, REG_temp0, REG_fptos, REG_imm, REG_zero, 3, (Waddr)&ctx.fpstack); ldpra.internal = 1; this << ldpra;
    this << TransOp(OP_addm, REG_temp2, REG_fptos, REG_imm, REG_imm, 3, 8*modrm.rm, FP_STACK_MASK);
    TransOp ldprb(OP_ld, REG_temp1, REG_temp2, REG_imm, REG_zero, 3, (Waddr)&ctx.fpstack); ldprb.internal = 1; this << ldprb;
    this << TransOp(OP_cmpccf, REG_temp0, REG_temp0, REG_temp1, REG_zero, 2);
    TransOp ldpxlate(OP_ld, REG_temp0, REG_temp0, REG_imm, REG_zero, 0, (Waddr)&translate_cmpccf_to_x87); ldpxlate.internal = 1; this << ldpxlate;
    this << TransOp(OP_mask, REG_fpsw, REG_fpsw, REG_temp0, REG_imm, 3, 0, MaskControlInfo((64-8), 3, (64-8)));
    this << TransOp(OP_mask, REG_fpsw, REG_fpsw, REG_temp0, REG_imm, 3, 0, MaskControlInfo((64-14), 1, (64-11)));
    // FCOMP requires pop from stack
    if (op == 0x655) {
      this << TransOp(OP_btr, REG_fptags, REG_fptags, REG_fptos, REG_zero, 3); // pop: adjust tag word
      this << TransOp(OP_addm, REG_fptos, REG_fptos, REG_imm, REG_imm, 3, 8, FP_STACK_MASK); // pop
    }
    break;
  }

  case 0x610: { // fld mem32 or fld reg
    if (modrm.mod == 3) {
      CheckInvalid();
      // load from FP stack register
      this << TransOp(OP_addm, REG_temp0, REG_fptos, REG_imm, REG_imm, 3, 8*modrm.rm, FP_STACK_MASK);
      TransOp ldp(OP_ld, REG_temp0, REG_temp0, REG_imm, REG_zero, 3, (Waddr)&ctx.fpstack); ldp.internal = 1; this << ldp;
      this << TransOp(OP_subm, REG_fptos, REG_fptos, REG_imm, REG_imm, 3, 8, FP_STACK_MASK); // push stack
      TransOp stp(OP_st, REG_mem, REG_fptos, REG_imm, REG_temp0, 3, (Waddr)&ctx.fpstack); stp.internal = 1; this << stp;
      this << TransOp(OP_bts, REG_fptags, REG_fptags, REG_fptos, REG_zero, 3);
    } else {
      // load from memory
      // ldd          t0 = [mem]
      // cvtf.s2d.lo  t0 = t0
      // st.lm.p      [CTX.FPSTACK + fptos],t0
      // subm         fptos = fptos,8,0x3f
      DECODE(eform, ra, d_mode);
      CheckInvalid();
      operand_load(REG_temp0, ra, OP_ld, DATATYPE_FLOAT);
      this << TransOp(OP_cvtf_s2d_lo, REG_temp0, REG_zero, REG_temp0, REG_zero, 3);
      this << TransOp(OP_subm, REG_fptos, REG_fptos, REG_imm, REG_imm, 3, 8, FP_STACK_MASK); // push stack
      TransOp stp(OP_st, REG_mem, REG_fptos, REG_imm, REG_temp0, 3, (Waddr)&ctx.fpstack); stp.internal = 1; this << stp;
      this << TransOp(OP_bts, REG_fptags, REG_fptags, REG_fptos, REG_zero, 3);
    }
    break;
  }

  case 0x616: { // misc functions
    if (modrm.mod != 3) MakeInvalid();
    CheckInvalid();
    int subop = modrm.rm;
    if (inrange(subop, 0, 5)) {
      static const int subop_to_assistid[6] = { ASSIST_X87_F2XM1, ASSIST_X87_FYL2X, ASSIST_X87_FPTAN, ASSIST_X87_FPATAN, ASSIST_X87_FXTRACT, ASSIST_X87_FPREM1 };
      microcode_assist(subop_to_assistid[subop], ripstart, rip); end_of_block = 1;
    } else {
      // fdecstp or fincstp
      this << TransOp((subop == 6) ? OP_subm : OP_addm, REG_fptos, REG_fptos, REG_imm, REG_imm, 3, 8, FP_STACK_MASK);
    }
    break;
  };

  case 0x650: { // fld mem64 or ffree
    if (modrm.mod == 3) {
      // ffree (just clear tag bit)
      CheckInvalid();
      this << TransOp(OP_btr, REG_fptags, REG_fptags, REG_fptos, REG_zero, 3);
    } else {
      // load from memory
      // ldq          t0 = [mem]
      // st.lm.p      [CTX.FPSTACK + fptos],t0
      // subm         fptos = fptos,8,0x3f
      DECODE(eform, ra, q_mode);
      CheckInvalid();
      operand_load(REG_temp0, ra, OP_ld, DATATYPE_DOUBLE);
      this << TransOp(OP_subm, REG_fptos, REG_fptos, REG_imm, REG_imm, 3, 8, FP_STACK_MASK); // push stack
      TransOp stp(OP_st, REG_mem, REG_fptos, REG_imm, REG_temp0, 3, (Waddr)&ctx.fpstack); stp.internal = 1; this << stp;
      this << TransOp(OP_bts, REG_fptags, REG_fptags, REG_fptos, REG_zero, 3);
    }
    break;
  }

  case 0x651:   // fisttp
  case 0x677: { // fistp
    if (modrm.rm == 3) {
      MakeInvalid();
    } else {
      DECODE(eform, rd, q_mode);
      CheckInvalid();
      TransOp ldp(OP_ld, REG_temp0, REG_fptos, REG_imm, REG_zero, 3, (Waddr)&ctx.fpstack); ldp.internal = 1; this << ldp;
      this << TransOp(OP_cvtf_d2q, REG_temp0, REG_zero, REG_temp0, REG_zero, (op == 0x651) ? 3 : 2);
      result_store(REG_temp0, REG_temp1, rd, DATATYPE_DOUBLE);
      this << TransOp(OP_btr, REG_fptags, REG_fptags, REG_fptos, REG_zero, 3); // pop: adjust tag word
      this << TransOp(OP_addm, REG_fptos, REG_fptos, REG_imm, REG_imm, 3, 8, FP_STACK_MASK); // pop: adjust top of stack
    }
    break;
  }

  case 0x612:
  case 0x613: { // fst/fstp mem32 or fnop
    if (modrm.mod == 3) {
      // fnop
      CheckInvalid();
      this << TransOp(OP_nop, REG_temp0, REG_zero, REG_zero, REG_zero, 3);
    } else {
      // store st0 to memory
      // ldd          t0 = [mem]
      // fcvt.s2d.lo  t0 = t0
      // st.lm.p      [CTX.FPSTACK + fptos],t0
      // subm         fptos = fptos,8,0x3f
      DECODE(eform, rd, d_mode);
      CheckInvalid();
      TransOp ldp(OP_ld, REG_temp0, REG_fptos, REG_imm, REG_zero, 3, (Waddr)&ctx.fpstack); ldp.internal = 1; this << ldp;
      this << TransOp(OP_cvtf_d2s_ins, REG_temp0, REG_zero, REG_temp0, REG_zero, 3);
      result_store(REG_temp0, REG_temp1, rd, DATATYPE_FLOAT);

      if (bit(op, 0)) {
        this << TransOp(OP_btr, REG_fptags, REG_fptags, REG_fptos, REG_zero, 3); // pop: adjust tag word
        this << TransOp(OP_addm, REG_fptos, REG_fptos, REG_imm, REG_imm, 3, 8, FP_STACK_MASK); // pop: fstp
      }
    }
    break;
  }

  case 0x652:
  case 0x653: { // fst/fstp mem64 or fst st(0) to FP stack reg
    if (modrm.mod == 3) {
      CheckInvalid();
      // fst st(0) to FP stack reg
      this << TransOp(OP_addm, REG_temp1, REG_fptos, REG_imm, REG_imm, 3, 8*modrm.rm, FP_STACK_MASK);

      TransOp ldp(OP_ld, REG_temp0, REG_fptos, REG_imm, REG_zero, 3, (Waddr)&ctx.fpstack); ldp.internal = 1; this << ldp;
      TransOp stp(OP_st, REG_mem, REG_temp1, REG_imm, REG_temp0, 3, (Waddr)&ctx.fpstack); stp.internal = 1; this << stp;

      if (bit(op, 0)) {
        this << TransOp(OP_btr, REG_fptags, REG_fptags, REG_fptos, REG_zero, 3); // pop: adjust tag word
        this << TransOp(OP_addm, REG_fptos, REG_fptos, REG_imm, REG_imm, 3, 8, FP_STACK_MASK); // pop: adjust top of stack
      }
    } else {
      // store st0 to memory
      // ldd          t0 = [mem]
      // fcvt.s2d.lo  t0 = t0
      // st.lm.p      [CTX.FPSTACK + fptos],t0
      // subm         fptos = fptos,8,0x3f
      DECODE(eform, rd, q_mode);
      CheckInvalid();

      TransOp ldp(OP_ld, REG_temp0, REG_fptos, REG_imm, REG_zero, 3, (Waddr)&ctx.fpstack);
      ldp.internal = 1;
      this << ldp;
      result_store(REG_temp0, REG_temp1, rd, DATATYPE_DOUBLE);

      if (bit(op, 0)) {
        this << TransOp(OP_btr, REG_fptags, REG_fptags, REG_fptos, REG_zero, 3); // pop: adjust tag word
        this << TransOp(OP_addm, REG_fptos, REG_fptos, REG_imm, REG_imm, 3, 8, FP_STACK_MASK); // pop: adjust top of stack
      }
    }
    break;
  }

  case 0x657: { // fstsw mem16
    //
    // FPSW format:
    // 
    // b  C3 (TOS) C2 C1 C0 es sf pe ue oe ze de ie
    // 15 14 13-11 10  9  8  7  6  5  4  3  2  1  0
    //                             T  O  S (shadow_fptos)
    //
    // MaskControlInfo(ms, mc, ds);
    //

    DECODE(eform, rd, w_mode);
    if (modrm.mod == 3) MakeInvalid();
    CheckInvalid();
    this << TransOp(OP_mask, REG_fpsw, REG_fpsw, REG_fptos, REG_imm, 3, 0, MaskControlInfo((64-11), 3, (64-(11-3))));
    result_store(REG_fpsw, REG_temp1, rd);
    break;
  }

  case 0x674: { // fstsw ax
    if ((modrm.mod != 3) | (modrm.rm != 0)) MakeInvalid();
    CheckInvalid();
    this << TransOp(OP_mask, REG_fpsw, REG_fpsw, REG_fptos, REG_imm, 3, 0, MaskControlInfo((64-11), 3, (64-(11-3))));
    this << TransOp(OP_mov, REG_rax, REG_rax, REG_fpsw, REG_zero, 1);
    break;
  }

  case 0x611: { // fxch
    if (modrm.mod == 0x3) {
      CheckInvalid();
      // load from FP stack register
      this << TransOp(OP_addm, REG_temp2, REG_fptos, REG_imm, REG_imm, 3, 8*modrm.rm, FP_STACK_MASK);
      TransOp ldptos(OP_ld, REG_temp0, REG_fptos, REG_imm, REG_zero, 3, (Waddr)&ctx.fpstack); ldptos.internal = 1; this << ldptos;
      TransOp ldpalt(OP_ld, REG_temp1, REG_temp2, REG_imm, REG_zero, 3, (Waddr)&ctx.fpstack); ldpalt.internal = 1; this << ldpalt;

      TransOp stptos(OP_st, REG_mem,   REG_fptos, REG_imm, REG_temp1, 3, (Waddr)&ctx.fpstack); stptos.internal = 1; this << stptos;
      TransOp stpalt(OP_st, REG_mem,   REG_temp2, REG_imm, REG_temp0, 3, (Waddr)&ctx.fpstack); stpalt.internal = 1; this << stpalt;
    } else {
      MakeInvalid();
    }
    break;
  }

  case 0x615: { // fldCONST
    if (modrm.mod == 0x3) {
      CheckInvalid();
      // fld1 fld2t fldl2e fldpi fldlg2 fldln2 fldz (inv):
      static const double constants[8] = {1.0, 3.3219280948873623479, 1.4426950408889634074, 3.1415926535897932385, .30102999566398119521, .69314718055994530942, 0, 0};
      // load from constant
      this << TransOp(OP_mov, REG_temp0, REG_zero, REG_imm, REG_zero, 3, ((W64*)&constants)[modrm.rm]);
      this << TransOp(OP_subm, REG_fptos, REG_fptos, REG_imm, REG_imm, 3, 8, FP_STACK_MASK); // push stack
      TransOp stp(OP_st, REG_mem, REG_fptos, REG_imm, REG_temp0, 3, (Waddr)&ctx.fpstack); stp.internal = 1; this << stp;
      this << TransOp(OP_bts, REG_fptags, REG_fptags, REG_fptos, REG_zero, 3); // push: set used bit in tag word
    } else {
      // fldcw
      DECODE(eform, ra, w_mode);
      CheckInvalid();
      operand_load(REG_temp1, ra, OP_ld);
      TransOp stp(OP_st, REG_mem, REG_zero, REG_imm, REG_temp1, 1, (Waddr)&ctx.fpcw); stp.internal = 1; this << stp;
    }
    break;
  }

  case 0x617: { // fnstcw | fprem fyl2xp1 fsqrt fsincos frndint fscale fsin fcos
    if (modrm.mod == 3) {
      CheckInvalid();
      int x87op = modrm.rm;
      // Microcoded FP functions, except for sqrt
      if (x87op == 2) {
        // sqrt can be inlined
        TransOp ldp(OP_ld, REG_temp0, REG_fptos, REG_imm, REG_zero, 3, (Waddr)&ctx.fpstack); ldp.internal = 1; this << ldp;
        this << TransOp(OP_sqrtf, REG_temp0, REG_temp0, REG_temp0, REG_zero, 2);
        TransOp stp(OP_st, REG_mem, REG_fptos, REG_imm, REG_temp0, 3, (Waddr)&ctx.fpstack); stp.internal = 1; this << stp;
      } else {
        static const int x87op_to_assist_idx[8] = {
          ASSIST_X87_FPREM, ASSIST_X87_FYL2XP1, ASSIST_X87_FSQRT, ASSIST_X87_FSINCOS,
          ASSIST_X87_FRNDINT, ASSIST_X87_FSCALE, ASSIST_X87_FSIN, ASSIST_X87_FCOS
        };

        if (x87op == 0) MakeInvalid(); // we don't support very old fprem form
        microcode_assist(x87op_to_assist_idx[x87op], ripstart, rip);
        end_of_block = 1;
      }
    } else {
      // fnstcw
      DECODE(eform, rd, w_mode);
      CheckInvalid();
      TransOp ldp(OP_ld, REG_temp1, REG_zero, REG_imm, REG_zero, 1, (Waddr)&ctx.fpcw); ldp.internal = 1; this << ldp;
      result_store(REG_temp1, REG_temp0, rd);
    }
    break;
  }

  case 0x670: { // ffreep (free and pop: not documented but widely used)
    if (modrm.mod == 0x3) {
      // ffreep
      CheckInvalid();
      this << TransOp(OP_btr, REG_fptags, REG_fptags, REG_fptos, REG_zero, 3); // pop: clear used bit in tag word
      this << TransOp(OP_addm, REG_fptos, REG_fptos, REG_imm, REG_imm, 3, 8, FP_STACK_MASK); // pop
    } else {
      // fild mem16
      // ldxw         t0 = [mem]
      // fcvt.q2d.lo  t0 = t0
      // st.lm.p      [CTX.FPSTACK + fptos],t0
      // subm         fptos = fptos,8,0x3f
      // bts          fptags = fptags,fptos
      DECODE(eform, ra, w_mode);
      CheckInvalid();
      operand_load(REG_temp0, ra, OP_ldx, 1);
      this << TransOp(OP_cvtf_q2d, REG_temp0, REG_zero, REG_temp0, REG_zero, 3);
      this << TransOp(OP_subm, REG_fptos, REG_fptos, REG_imm, REG_imm, 3, 8, FP_STACK_MASK); // push stack
      TransOp stp(OP_st, REG_mem, REG_fptos, REG_imm, REG_temp0, 3, (Waddr)&ctx.fpstack); stp.internal = 1; this << stp;
      this << TransOp(OP_bts, REG_fptags, REG_fptags, REG_fptos, REG_zero, 3);
    }
    break;
  }

  case 0x671 ... 0x673: { // [fisttp | fist | fistp] mem16
    DECODE(eform, rd, w_mode);
    CheckInvalid();
    TransOp ldp(OP_ld, REG_temp0, REG_fptos, REG_imm, REG_zero, 3, (Waddr)&ctx.fpstack); ldp.internal = 1; this << ldp;
    this << TransOp(OP_cvtf_d2i, REG_temp0, REG_zero, REG_temp0, REG_zero, (op == 0x671) ? 1 : 0);
    result_store(REG_temp0, REG_temp1, rd);

    int x87op = modrm.rm;
    if ((x87op == 1) | (x87op == 3)) {
      this << TransOp(OP_btr, REG_fptags, REG_fptags, REG_fptos, REG_zero, 3); // pop: adjust tag word
      this << TransOp(OP_addm, REG_fptos, REG_fptos, REG_imm, REG_imm, 3, 8, FP_STACK_MASK); // pop: adjust top of stack
    }
    break;
  }

  case 0x614: { // fchs fabs ? ? ftst fxam ? ?
    if (modrm.mod != 3) MakeInvalid();
    CheckInvalid();

    switch (modrm.rm) {
    case 0: { // fchs
      TransOp ldp(OP_ld, REG_temp0, REG_fptos, REG_imm, REG_zero, 3, (Waddr)&ctx.fpstack); ldp.internal = 1; this << ldp;
      this << TransOp(OP_xor, REG_temp0, REG_temp0, REG_imm, REG_zero, 3, (1LL << 63));
      TransOp stp(OP_st, REG_mem, REG_fptos, REG_imm, REG_temp0, 3, (Waddr)&ctx.fpstack); stp.internal = 1; this << stp;
      break;
    } 
    case 1: { // fabs
      TransOp ldp(OP_ld, REG_temp0, REG_fptos, REG_imm, REG_zero, 3, (Waddr)&ctx.fpstack); ldp.internal = 1; this << ldp;
      this << TransOp(OP_and, REG_temp0, REG_temp0, REG_imm, REG_zero, 3, ~(1LL << 63));
      TransOp stp(OP_st, REG_mem, REG_fptos, REG_imm, REG_temp0, 3, (Waddr)&ctx.fpstack); stp.internal = 1; this << stp;
      break;
    }
    case 4: { // ftst: compare st(0) to 0.0
      TransOp ldp(OP_ld, REG_temp0, REG_fptos, REG_imm, REG_zero, 3, (Waddr)&ctx.fpstack); ldp.internal = 1; this << ldp;
      this << TransOp(OP_cmpccf, REG_temp0, REG_temp0, REG_zero, REG_zero, 2);
      TransOp ldpxlate(OP_ld, REG_temp0, REG_temp0, REG_imm, REG_zero, 0, (Waddr)&translate_cmpccf_to_x87); ldpxlate.internal = 1; this << ldpxlate;
      this << TransOp(OP_mask, REG_fpsw, REG_fpsw, REG_temp0, REG_imm, 3, 0, MaskControlInfo((64-8), 3, (64-8)));
      this << TransOp(OP_mask, REG_fpsw, REG_fpsw, REG_temp0, REG_imm, 3, 0, MaskControlInfo((64-14), 1, (64-11)));
      break;
    }
    case 5: { // fxam
      microcode_assist(ASSIST_X87_FXAM, ripstart, rip);
      end_of_block = 1;
      break;
    }
    default:
      MakeInvalid();
      break;
    }
    break;
  }

  case 0x620 ... 0x627: { // fcmovb fcmove fcmovbe fcmovu
    DECODE(eform, ra, d_mode);
    int x87op = modrm.reg;
    CheckInvalid();
    if (modrm.mod == 3) {
      // conditional moves, OR fucompp
      if ((x87op == 4) | (x87op == 6) | (x87op == 7)) MakeInvalid();
      CheckInvalid();
      if (x87op == 5) {
        // fucompp was just slapped down here for some random reason...
        TransOp ldpra(OP_ld, REG_temp0, REG_fptos, REG_imm, REG_zero, 3, (Waddr)&ctx.fpstack); ldpra.internal = 1; this << ldpra;
        this << TransOp(OP_addm, REG_temp2, REG_fptos, REG_imm, REG_imm, 3, 8*modrm.rm, FP_STACK_MASK);
        TransOp ldprb(OP_ld, REG_temp1, REG_temp2, REG_imm, REG_zero, 3, (Waddr)&ctx.fpstack); ldprb.internal = 1; this << ldprb;
        this << TransOp(OP_cmpccf, REG_temp0, REG_temp0, REG_temp1, REG_zero, 2);
        TransOp ldpxlate(OP_ld, REG_temp0, REG_temp0, REG_imm, REG_zero, 0, (Waddr)&translate_cmpccf_to_x87); ldpxlate.internal = 1; this << ldpxlate;
        this << TransOp(OP_mask, REG_fpsw, REG_fpsw, REG_temp0, REG_imm, 3, 0, MaskControlInfo((64-8), 3, (64-8)));
        this << TransOp(OP_mask, REG_fpsw, REG_fpsw, REG_temp0, REG_imm, 3, 0, MaskControlInfo((64-14), 1, (64-11)));
        // FUCOMPP requires pop from stack twice
        this << TransOp(OP_btr, REG_fptags, REG_fptags, REG_fptos, REG_zero, 3); // pop: adjust tag word
        this << TransOp(OP_addm, REG_fptos, REG_fptos, REG_imm, REG_imm, 3, 8, FP_STACK_MASK); // pop
        this << TransOp(OP_btr, REG_fptags, REG_fptags, REG_fptos, REG_zero, 3); // pop: adjust tag word
        this << TransOp(OP_addm, REG_fptos, REG_fptos, REG_imm, REG_imm, 3, 8, FP_STACK_MASK); // pop
      } else {
        // fcmovCC
        this << TransOp(OP_addm, REG_temp1, REG_fptos, REG_imm, REG_imm, 3, 8*modrm.rm, FP_STACK_MASK);
        TransOp ldp0(OP_ld, REG_temp0, REG_fptos, REG_imm, REG_zero, 3, (Waddr)&ctx.fpstack); ldp0.internal = 1; this << ldp0;
        TransOp ldp1(OP_ld, REG_temp1, REG_temp1, REG_imm, REG_zero, 3, (Waddr)&ctx.fpstack); ldp1.internal = 1; this << ldp1;
        
        int cmptype = lowbits(op, 2);
        int rcond;
        int cond;
        bool invert = 0; // ((op & 0xff0) == 0x630);
        
        switch (lowbits(op, 2)) {
        case 0: // fcmovb (CF = 1)
          rcond = REG_cf;
          cond = (invert) ? COND_nc : COND_c;
          break;
        case 1: // fcmove (ZF = 1)
          rcond = REG_zf;
          cond = (invert) ? COND_ne : COND_e;
          break;
        case 2: // fcmovbe (ZF = 1 or CF = 1)
          this << TransOp(OP_collcc, REG_temp2, REG_zf, REG_cf, REG_of, 3, 0, 0, FLAGS_DEFAULT_ALU);
          cond = (invert) ? COND_nbe : COND_be;
          rcond = REG_temp2;
          break;
        case 3: // fcmovu (PF = 1)
          this << TransOp(OP_collcc, REG_temp2, REG_zf, REG_cf, REG_of, 3, 0, 0, FLAGS_DEFAULT_ALU);
          cond = (invert) ? COND_np : COND_p;
          rcond = REG_temp2;
          break;
        }
        
        TransOp sel(OP_sel, REG_temp0, REG_temp0, REG_temp1, rcond, 3);
        sel.cond = cond;
        this << sel;
        
        TransOp stp(OP_st, REG_mem, REG_fptos, REG_imm, REG_temp0, 3, (Waddr)&ctx.fpstack);
        stp.internal = 1;
        this << stp;
      }
    } else {
      // st(0) = st(0) OP mem32int
      TransOp ldpra(OP_ld, REG_temp0, REG_fptos, REG_imm, REG_zero, 3, (Waddr)&ctx.fpstack); ldpra.internal = 1; this << ldpra;
      operand_load(REG_temp1, ra, OP_ldx);
      this << TransOp(OP_cvtf_q2d, REG_temp1, REG_zero, REG_temp1, REG_zero, 2);

      static const int x87_translate_opcode[8] = {OP_addf, OP_mulf, OP_cmpccf, OP_cmpccf, OP_subf, OP_subf, OP_divf, OP_divf};
      static const int x87_d8da_translate_ra[8] = {REG_temp0, REG_temp0, REG_temp0, REG_temp0,  REG_temp0, REG_temp1, REG_temp0, REG_temp1};
      static const int x87_d8da_translate_rb[8] = {REG_temp1, REG_temp1, REG_temp1, REG_temp1,  REG_temp1, REG_temp0, REG_temp1, REG_temp0};
      // fadd fmul fcom fcomp fsub fsubr fdiv fdivr

      int translated_opcode = x87_translate_opcode[x87op];
      this << TransOp(translated_opcode, REG_temp0, x87_d8da_translate_ra[x87op], x87_d8da_translate_rb[x87op], REG_zero, 2);

      if (translated_opcode == OP_cmpccf) {
        TransOp ldpxlate(OP_ld, REG_temp0, REG_temp0, REG_imm, REG_zero, 0, (Waddr)&translate_cmpccf_to_x87); ldpxlate.internal = 1; this << ldpxlate;
        this << TransOp(OP_mask, REG_fpsw, REG_fpsw, REG_temp0, REG_imm, 3, 0, MaskControlInfo((64-8), 3, (64-8)));
        this << TransOp(OP_mask, REG_fpsw, REG_fpsw, REG_temp0, REG_imm, 3, 0, MaskControlInfo((64-14), 1, (64-11)));
        // FCOMP requires pop from stack
        if (x87op == 3) {
          this << TransOp(OP_btr, REG_fptags, REG_fptags, REG_fptos, REG_zero, 3); // pop: adjust tag word
          this << TransOp(OP_addm, REG_fptos, REG_fptos, REG_imm, REG_imm, 3, 8, FP_STACK_MASK); // pop
        }
      } else {
        TransOp stp(OP_st, REG_mem, REG_fptos, REG_imm, REG_temp0, 3, (Waddr)&ctx.fpstack); stp.internal = 1; this << stp;
        this << TransOp(OP_bts, REG_fptags, REG_fptags, REG_fptos, REG_zero, 3);
      }

      check_warned_about_x87();
    }
    break;
  }

  case 0x630 ... 0x633: { // [fcmovnb fcmovne fcmovnbe fcmovnu] | [fild fisttp fist fistp]
    int x87op = modrm.reg;

    DECODE(eform, rd, d_mode);
    CheckInvalid();

    if (modrm.mod == 3) {
      // fcmovnb fcmovne fcmovnbe fcmovnu
      this << TransOp(OP_addm, REG_temp1, REG_fptos, REG_imm, REG_imm, 3, 8*modrm.rm, FP_STACK_MASK);
      TransOp ldp0(OP_ld, REG_temp0, REG_fptos, REG_imm, REG_zero, 3, (Waddr)&ctx.fpstack); ldp0.internal = 1; this << ldp0;
      TransOp ldp1(OP_ld, REG_temp1, REG_temp1, REG_imm, REG_zero, 3, (Waddr)&ctx.fpstack); ldp1.internal = 1; this << ldp1;
        
      int cmptype = lowbits(op, 2);
      int rcond;
      int cond;
      bool invert = 1;
        
      switch (lowbits(op, 2)) {
      case 0: // fcmovb (CF = 1)
        rcond = REG_cf;
        cond = (invert) ? COND_nc : COND_c;
        break;
      case 1: // fcmove (ZF = 1)
        rcond = REG_zf;
        cond = (invert) ? COND_ne : COND_e;
        break;
      case 2: // fcmovbe (ZF = 1 or CF = 1)
        this << TransOp(OP_collcc, REG_temp2, REG_zf, REG_cf, REG_of, 3, 0, 0, FLAGS_DEFAULT_ALU);
        cond = (invert) ? COND_nbe : COND_be;
        rcond = REG_temp2;
        break;
      case 3: // fcmovu (PF = 1)
        this << TransOp(OP_collcc, REG_temp2, REG_zf, REG_cf, REG_of, 3, 0, 0, FLAGS_DEFAULT_ALU);
        cond = (invert) ? COND_np : COND_p;
        rcond = REG_temp2;
        break;
      }
        
      TransOp sel(OP_sel, REG_temp0, REG_temp0, REG_temp1, rcond, 3);
      sel.cond = cond;
      this << sel;
        
      TransOp stp(OP_st, REG_mem, REG_fptos, REG_imm, REG_temp0, 3, (Waddr)&ctx.fpstack);
      stp.internal = 1;
      this << stp;
    } else {
      switch (x87op) {
      case 0: { // fild mem32
        operand_load(REG_temp0, rd, OP_ldx, 1);
        this << TransOp(OP_cvtf_q2d, REG_temp0, REG_zero, REG_temp0, REG_zero, 3);
        this << TransOp(OP_subm, REG_fptos, REG_fptos, REG_imm, REG_imm, 3, 8, FP_STACK_MASK); // push stack
        TransOp stp(OP_st, REG_mem, REG_fptos, REG_imm, REG_temp0, 3, (Waddr)&ctx.fpstack); stp.internal = 1; this << stp;
        this << TransOp(OP_bts, REG_fptags, REG_fptags, REG_fptos, REG_zero, 3);
        break;
      }
      case 1:   // fisttp w32
      case 2:   // fist w32
      case 3: { // fistp w32
        TransOp ldp(OP_ld, REG_temp0, REG_fptos, REG_imm, REG_zero, 3, (Waddr)&ctx.fpstack); ldp.internal = 1; this << ldp;
        this << TransOp(OP_cvtf_d2i, REG_temp0, REG_zero, REG_temp0, REG_zero, (x87op == 1));
        result_store(REG_temp0, REG_temp1, rd);

        if ((x87op == 1) | (x87op == 3)) {
          this << TransOp(OP_btr, REG_fptags, REG_fptags, REG_fptos, REG_zero, 3); // pop: adjust tag word
          this << TransOp(OP_addm, REG_fptos, REG_fptos, REG_imm, REG_imm, 3, 8, FP_STACK_MASK); // pop: adjust top of stack
        }

        break;
      }
      }
    }
    break;
  }

  case 0x635: // 0xdb fucomi or fld mem80
  case 0x636: // 0xdb fcomi
  case 0x637: // 0xdb fstp mem80
  case 0x675: // 0xdf fucomip
  case 0x676: { // 0xdf fcomip
    if (modrm.mod == 3) {
      if (op == 0x637) MakeInvalid();
      CheckInvalid();
      //
      // For cmpccf, uop.size bits have following meaning:
      // 00 = single precision ordered compare
      // 01 = single precision unordered compare
      // 10 = double precision ordered compare
      // 11 = double precision unordered compare
      //
      this << TransOp(OP_addm, REG_temp1, REG_fptos, REG_imm, REG_imm, 3, 8*modrm.rm, FP_STACK_MASK);
      TransOp ldp0(OP_ld, REG_temp0, REG_fptos, REG_imm, REG_zero, 3, (Waddr)&ctx.fpstack); ldp0.internal = 1; this << ldp0;
      TransOp ldp1(OP_ld, REG_temp1, REG_temp1, REG_imm, REG_zero, 3, (Waddr)&ctx.fpstack); ldp1.internal = 1; this << ldp1;
        
      //
      // comisX and ucomisX set {zf pf cf} according to the comparison,
      // and always set {of sf af} to zero. The equivalent x87 version 
      // is fucomi/fcomi/fucomip/fcomip:
      //
      bool unordered = bit(op, 0);
      this << TransOp(OP_cmpccf, REG_temp0, REG_temp0, REG_temp1, REG_zero, (unordered ? 3 : 2), 0, 0, FLAGS_DEFAULT_ALU);

      if ((op >> 4) == 0x67) {
        this << TransOp(OP_btr, REG_fptags, REG_fptags, REG_fptos, REG_zero, 3); // pop: adjust tag word
        this << TransOp(OP_addm, REG_fptos, REG_fptos, REG_imm, REG_imm, 3, 8, FP_STACK_MASK); // pop: fstp
      }
    } else {
      if (op == 0x675) {
        // fild mem64
        // ldq          t0 = [mem]
        // fcvt.q2d.lo  t0 = t0
        // st.lm.p      [CTX.FPSTACK + fptos],t0
        // subm         fptos = fptos,8,0x3f
        // bts          fptags = fptags,fptos
        DECODE(eform, ra, q_mode);
        CheckInvalid();
        operand_load(REG_temp0, ra, OP_ld);
        this << TransOp(OP_cvtf_q2d, REG_temp0, REG_zero, REG_temp0, REG_zero, 3);
        this << TransOp(OP_subm, REG_fptos, REG_fptos, REG_imm, REG_imm, 3, 8, FP_STACK_MASK); // push stack
        TransOp stp(OP_st, REG_mem, REG_fptos, REG_imm, REG_temp0, 3, (Waddr)&ctx.fpstack); stp.internal = 1; this << stp;
        this << TransOp(OP_bts, REG_fptags, REG_fptags, REG_fptos, REG_zero, 3);
      } else if ((op == 0x635) | (op == 0x637)) {
        // fld mem80 or fstp mem80
        DECODE(eform, ra, q_mode);
        CheckInvalid();

        // Get effective address into sr2
        int basereg = arch_pseudo_reg_to_arch_reg[ra.mem.basereg];
        int indexreg = arch_pseudo_reg_to_arch_reg[ra.mem.indexreg];
        basereg = bias_by_segreg(basereg);
        TransOp addop(OP_adda, REG_ar1, basereg, REG_imm, indexreg, (ctx.use64) ? 3 : 2, ra.mem.offset);
        addop.extshift = ra.mem.scale;
        this << addop;

        microcode_assist((op == 0x635) ? ASSIST_FLD80 : ASSIST_FSTP80, ripstart, rip);
        end_of_block = 1;
        break;
      } else {
        MakeInvalid();
      }
    }
    break;
  }

  default: {
    MakeInvalid();
    break;
  }
  }

  return true;
}