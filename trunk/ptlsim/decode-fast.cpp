//
// PTLsim: Cycle Accurate x86-64 Simulator
// Decoder for simple x86 instructions
//
// Copyright 1999-2006 Matt T. Yourst <yourst@yourst.com>
//

#include <decode.h>

bool TraceDecoder::decode_fast() {
  DecodedOperand rd;
  DecodedOperand ra;

  switch (op) {
  case 0x00 ... 0x0e:
  case 0x10 ... 0x3f: {
    // Arithmetic: add, or, adc, sbb, and, sub, xor, cmp
    // Low 3 bits of opcode determine the format:
    switch (bits(op, 0, 3)) {
    case 0: DECODE(eform, rd, b_mode); DECODE(gform, ra, b_mode); break;
    case 1: DECODE(eform, rd, v_mode); DECODE(gform, ra, v_mode); break;
    case 2: DECODE(gform, rd, b_mode); DECODE(eform, ra, b_mode); break;
    case 3: DECODE(gform, rd, v_mode); DECODE(eform, ra, v_mode); break;
    case 4: rd.type = OPTYPE_REG; rd.reg.reg = APR_al; DECODE(iform, ra, b_mode); break;
    case 5: DECODE(varreg_def32, rd, 0); DECODE(iform, ra, v_mode); break;
    default: invalid |= true; break;
    }
    CheckInvalid();

    // add and sub always add carry from rc iff rc is not REG_zero
    static const byte translate_opcode[8] = {OP_add, OP_or, OP_add, OP_sub, OP_and, OP_sub, OP_xor, OP_sub};

    int subop = bits(op, 3, 3);
    int translated_opcode = translate_opcode[subop];
    int rcreg = ((subop == 2) | (subop == 3)) ? REG_cf : REG_zero;
    alu_reg_or_mem(translated_opcode, rd, ra, FLAGS_DEFAULT_ALU, rcreg, (subop == 7));

    break;
  }

  case 0x40 ... 0x4f: {
    // inc/dec in 32-bit mode only: for x86-64 this is not possible since it's the REX prefix
    ra.gform_ext(*this, v_mode, bits(op, 0, 3), false, true);
    int sizeshift = reginfo[ra.reg.reg].sizeshift;
    int r = arch_pseudo_reg_to_arch_reg[ra.reg.reg];
    CheckInvalid();

    this << TransOp(bit(op, 3) ? OP_sub : OP_add, r, r, REG_imm, REG_zero, sizeshift, +1, 0, SETFLAG_ZF|SETFLAG_OF); // save old rdreg
    break;
    break;
  }

  case 0x50 ... 0x5f: {
    // push (0x50..0x57) or pop (0x58..0x5f) reg (defaults to 64 bit; pushing bytes not possible)
    ra.gform_ext(*this, v_mode, bits(op, 0, 3), ctx.use64, true);
    int r = arch_pseudo_reg_to_arch_reg[ra.reg.reg];
    int sizeshift = reginfo[ra.reg.reg].sizeshift;
    if (ctx.use64 && (sizeshift == 2)) sizeshift = 3; // There is no way to encode 32-bit pushes and pops in 64-bit mode:
    int size = (1 << sizeshift);
    CheckInvalid();

    if (op < 0x58) {
      // push
      this << TransOp(OP_st, REG_mem, REG_rsp, REG_imm, r, sizeshift, -size);
      this << TransOp(OP_sub, REG_rsp, REG_rsp, REG_imm, REG_zero, (ctx.use64 ? 3 : 2), size);

    } else {
      // pop
      this << TransOp(OP_ld, r, REG_rsp, REG_zero, REG_zero, sizeshift);
      this << TransOp(OP_add, REG_rsp, REG_rsp, REG_imm, REG_zero, (ctx.use64 ? 3 : 2), size);
    }
    break;
  }

  case 0x1b6 ... 0x1b7: {
    // zero extensions: movzx rd,byte / movzx rd,word
    int bytemode = (op == 0x1b6) ? b_mode : v_mode;
    DECODE(gform, rd, v_mode);
    DECODE(eform, ra, bytemode);
    int rasizeshift = bit(op, 0);
    CheckInvalid();
    signext_reg_or_mem(rd, ra, rasizeshift, true);
    break;
  }

  case 0x63: 
  case 0x1be ... 0x1bf: {
    // sign extensions: movsx movsxd
    int bytemode = (op == 0x1be) ? b_mode : v_mode;
    DECODE(gform, rd, v_mode);
    DECODE(eform, ra, bytemode);
    int rasizeshift = (op == 0x63) ? 2 : (op == 0x1be) ? 0 : (op == 0x1bf) ? 1 : 3;
    CheckInvalid();
    signext_reg_or_mem(rd, ra, rasizeshift);
    break;
  }

  case 0x68:
  case 0x6a: {
    // push immediate
    DECODE(iform64, ra, (op == 0x68) ? v_mode : b_mode);
    int sizeshift = (opsize_prefix) ? 1 : ((ctx.use64) ? 3 : 2);
    int size = (1 << sizeshift);
    CheckInvalid();

    int r = REG_temp0;
    immediate(r, (op == 0x68) ? 2 : 0, ra.imm.imm);

    this << TransOp(OP_st, REG_mem, REG_rsp, REG_imm, r, sizeshift, -size);
    this << TransOp(OP_sub, REG_rsp, REG_rsp, REG_imm, REG_zero, 3, size);

    break;
  }

  case 0x69:
  case 0x6b: {
    // multiplies with three operands including an immediate
    // 0x69: imul reg16/32/64, rm16/32/64, simm16/simm32
    // 0x6b: imul reg16/32/64, rm16/32/64, simm8
    int bytemode = (op == 0x6b) ? b_mode : v_mode;

    DECODE(gform, rd, v_mode);
    DECODE(eform, ra, v_mode);

    DecodedOperand rimm;
    DECODE(iform, rimm, bytemode);

    CheckInvalid();
    alu_reg_or_mem(OP_mull, rd, ra, FLAG_CF|FLAG_OF, REG_imm, false, false, true, rimm.imm.imm);
    break;
  }

  case 0x1af: {
    // multiplies with two operands
    // 0x69: imul reg16/32/64, rm16/32/64
    // 0x6b: imul reg16/32/64, rm16/32/64
    DECODE(gform, rd, v_mode);
    DECODE(eform, ra, v_mode);
    int rdreg = arch_pseudo_reg_to_arch_reg[ra.reg.reg];
    int rdshift = reginfo[rd.reg.reg].sizeshift;

    CheckInvalid();
    alu_reg_or_mem(OP_mull, rd, ra, FLAG_CF|FLAG_OF, (rdshift < 2) ? rdreg : REG_zero);
    break;
  }

  case 0x70 ... 0x7f:
  case 0x180 ... 0x18f: {
    // near conditional branches with 8-bit displacement:
    DECODE(iform, ra, (inrange(op, 0x180, 0x18f) ? v_mode : b_mode));
    CheckInvalid();
    if (!last_flags_update_was_atomic) 
      this << TransOp(OP_collcc, REG_temp0, REG_zf, REG_cf, REG_of, 3, 0, 0, FLAGS_DEFAULT_ALU);
    int condcode = bits(op, 0, 4);
    TransOp transop(OP_br, REG_rip, cond_code_to_flag_regs[condcode].ra, cond_code_to_flag_regs[condcode].rb, REG_zero, 3, 0);
    transop.cond = condcode;
    transop.riptaken = (Waddr)rip + ra.imm.imm;
    transop.ripseq = (Waddr)rip;
    bb.rip_taken = (Waddr)rip + ra.imm.imm;
    bb.rip_not_taken = (Waddr)rip;
    // (branch id implied)

    this << transop;
    end_of_block = true;
    break;
  }

  case 0x80 ... 0x83: {
    // GRP1b, GRP1s, GRP1ss:
    switch (bits(op, 0, 2)) {
    case 0: DECODE(eform, rd, b_mode); DECODE(iform, ra, b_mode); break; // GRP1b
    case 1: DECODE(eform, rd, v_mode); DECODE(iform, ra, v_mode); break; // GRP1S
    case 2: invalid |= true; break;
    case 3: DECODE(eform, rd, v_mode); DECODE(iform, ra, b_mode); break; // GRP1Ss (sign ext byte)
    }
    // function in modrm.reg: add or adc sbb and sub xor cmp
    CheckInvalid();

    // add and sub always add carry from rc iff rc is not REG_zero
    static const byte translate_opcode[8] = {OP_add, OP_or, OP_add, OP_sub, OP_and, OP_sub, OP_xor, OP_sub};

    int subop = modrm.reg;
    int translated_opcode = translate_opcode[subop];
    int rcreg = ((subop == 2) | (subop == 3)) ? REG_cf : REG_zero;
    alu_reg_or_mem(translated_opcode, rd, ra, FLAGS_DEFAULT_ALU, rcreg, (subop == 7));

    break;
  }

  case 0x84 ... 0x85: {
    // test
    DECODE(eform, rd, (op & 1) ? v_mode : b_mode);
    DECODE(gform, ra, (op & 1) ? v_mode : b_mode);
    CheckInvalid();
    alu_reg_or_mem(OP_and, rd, ra, FLAGS_DEFAULT_ALU, REG_zero, true);
    break;
  }

  case 0x88 ... 0x8b: {
    // moves
    int bytemode = bit(op, 0) ? v_mode : b_mode;
    switch (bit(op, 1)) {
    case 0: DECODE(eform, rd, bytemode); DECODE(gform, ra, bytemode); break;
    case 1: DECODE(gform, rd, bytemode); DECODE(eform, ra, bytemode); break;
    }
    CheckInvalid();
    move_reg_or_mem(rd, ra);
    break;
  }

  case 0x8d: {
    // lea (zero extends result: no merging)
    DECODE(gform, rd, v_mode);
    DECODE(eform, ra, v_mode);
    CheckInvalid();
    int destreg = arch_pseudo_reg_to_arch_reg[rd.reg.reg];
    int sizeshift = reginfo[rd.reg.reg].sizeshift;

    ra.mem.size = sizeshift;

    address_generate_and_load_or_store(destreg, REG_zero, ra, OP_add);
    break;
  }

  case 0x8f: {
    // pop Ev: pop to reg or memory
    DECODE(eform, rd, v_mode);
    CheckInvalid();

    int sizeshift = (rd.type == OPTYPE_REG) ? reginfo[rd.reg.reg].sizeshift : rd.mem.size;
    if (sizeshift == 2) sizeshift = 3; // There is no way to encode 32-bit pushes and pops in 64-bit mode:

    this << TransOp(OP_ld, REG_temp7, REG_rsp, REG_zero, REG_zero, sizeshift);

    ra.type = OPTYPE_REG;
    ra.reg.reg = 0; // not used
    // There is no way to encode 32-bit pushes and pops in 64-bit mode:
    if (rd.type == OPTYPE_MEM && rd.mem.size == 2) rd.mem.size = 3;
    move_reg_or_mem(rd, ra, REG_temp7);

    // Do this last since technically rsp update is not visible at address generation time:
    this << TransOp(OP_add, REG_rsp, REG_rsp, REG_imm, REG_zero, 3, (1 << sizeshift));

    break;
  }

  case 0x90: {
    // 0x90 (xchg eax,eax) is a NOP and in x86-64 is treated as such (i.e. does not zero upper 32 bits as usual)
    // NOTE! We still have to output something so %rip gets incremented correctly!
    CheckInvalid();
    this << TransOp(OP_nop, REG_temp0, REG_zero, REG_zero, REG_zero, 3);
    break;
  }

  case 0x98: {
    // cbw cwde cdqe
    int rashift = (opsize_prefix) ? 0 : ((rex.mode64) ? 2 : 1);
    int rdshift = rashift + 1;
    CheckInvalid();
    TransOp transop(OP_maskb, REG_rax, (rdshift < 3) ? REG_rax : REG_zero, REG_rax, REG_imm, rdshift, 0, MaskControlInfo(0, (1<<rashift)*8, 0));
    transop.cond = 2; // sign extend
    this << transop;
    break;
  }

  case 0x99: {
    // cwd cdq cqo
    CheckInvalid();
    int rashift = (opsize_prefix) ? 1 : ((rex.mode64) ? 3 : 2);

    TransOp bt(OP_bt, REG_temp0, REG_rax, REG_imm, REG_zero, 2, ((1<<rashift)*8)-1, 0, SETFLAG_CF);
    bt.nouserflags = 1; // it still generates flags, but does not rename the user flags
    this << bt;

    TransOp sel(OP_sel, REG_temp0, REG_zero, REG_imm, REG_temp0, 3, -1LL);
    sel.cond = COND_c;
    this << sel, endl;

    // move in value
    this << TransOp(OP_mov, REG_rdx, (rashift < 2) ? REG_rdx : REG_zero, REG_temp0, REG_zero, rashift);

    // zero out high bits of rax since technically both rdx and rax are modified:
    if (rashift == 2) this << TransOp(OP_mov, REG_rax, REG_zero, REG_rax, REG_zero, 2);
    break;
  }

  case 0x9e: { // sahf: %flags[7:0] = %ah
    // extract value from %ah
    CheckInvalid();
    this << TransOp(OP_maskb, REG_temp0, REG_zero, REG_rax, REG_imm, 3, 0, MaskControlInfo(0, 8, 8));
    // only low 8 bits affected (OF not included)
    this << TransOp(OP_movrcc, REG_temp0, REG_temp0, REG_zero, REG_zero, 3, 0, 0, SETFLAG_ZF|SETFLAG_CF);
    break;
  }

  case 0x9f: { // lahf: %ah = %flags[7:0]
    CheckInvalid();
    this << TransOp(OP_collcc, REG_temp0, REG_zf, REG_cf, REG_of, 3, 0, 0, FLAGS_DEFAULT_ALU);
    this << TransOp(OP_maskb, REG_rax, REG_rax, REG_temp0, REG_imm, 3, 0, MaskControlInfo(56, 8, 56));
    break;
  }

  case 0xa0 ... 0xa3: {
    // mov rAX,Ov and vice versa
    rd.gform_ext(*this, (op & 1) ? v_mode : b_mode, REG_rax);
    DECODE(iform64, ra, (ctx.use64 ? q_mode : addrsize_prefix ? w_mode : d_mode));
    CheckInvalid();

    ra.mem.offset = ra.imm.imm;
    ra.mem.offset = (ctx.use64) ? ra.mem.offset : lowbits(ra.mem.offset, (addrsize_prefix) ? 16 : 32);
    ra.mem.basereg = APR_zero;
    ra.mem.indexreg = APR_zero;
    ra.mem.scale = APR_zero;
    ra.mem.size = reginfo[rd.reg.reg].sizeshift;
    ra.type = OPTYPE_MEM;
    if (inrange(op, 0xa2, 0xa3)) {
      result_store(REG_rax, REG_temp0, ra);
    } else {
      operand_load(REG_rax, ra);
    }
    break;
  }

  case 0xa8 ... 0xa9: {
    // test al|ax,imm8|immV
    rd.gform_ext(*this, (op & 1) ? v_mode : b_mode, REG_rax);
    DECODE(iform, ra, (op & 1) ? v_mode : b_mode);
    CheckInvalid();
    alu_reg_or_mem(OP_and, rd, ra, FLAGS_DEFAULT_ALU, REG_zero, true);
    break;
  }

  case 0xb0 ... 0xb7: {
    // mov reg,imm8
    rd.gform_ext(*this, b_mode, bits(op, 0, 3), false, true);
    DECODE(iform, ra, b_mode);
    CheckInvalid();
    int rdreg = arch_pseudo_reg_to_arch_reg[rd.reg.reg];
    this << TransOp(OP_mov, rdreg, rdreg, REG_imm, REG_zero, 0, ra.imm.imm);
    break;
  }

  case 0xb8 ... 0xbf: {
    // mov reg,imm16|imm32|imm64
    rd.gform_ext(*this, v_mode, bits(op, 0, 3), false, true);
    DECODE(iform64, ra, v_mode);
    CheckInvalid();
    int rdreg = arch_pseudo_reg_to_arch_reg[rd.reg.reg];
    int sizeshift = reginfo[rd.reg.reg].sizeshift;
    this << TransOp(OP_mov, rdreg, (sizeshift >= 2) ? REG_zero : rdreg, REG_imm, REG_zero, sizeshift, ra.imm.imm);
    break;
  }

  case 0xc0 ... 0xc1: 
  case 0xd0 ... 0xd1: 
  case 0xd2 ... 0xd3: {
    /*
      rol ror rcl rcr shl shr shl sar:
      Shifts and rotates, either by an imm8, implied 1, or %cl

      The shift and rotate instructions have some of the most bizarre semantics in the
      entire x86 instruction set: they may or may not modify flags depending on the
      rotation count operand, which we may not even know until the instruction
      issues. The specific rules are as follows:

      - If the count is zero, no flags are modified
      - If the count is one, both OF and CF are modified.
      - If the count is greater than one, only the CF is modified.
      (Technically the value in OF is undefined, but on K8 and P4,
      it retains the old value, so we try to be compatible).
      - Shifts also alter the ZAPS flags while rotates do not.

      For constant counts, this is easy to determine while translating:
        
      op   rd = ra,0       op rd = ra,1              op rd = ra,N
      Becomes:             Becomes:                  Becomes
      (nop)                op rd = ra,1 [set of cf]  op rd = ra,N [set cf]
        
      For variable counts, things are more complex. Since the shift needs
      to determine its output flags at runtime based on both the shift count
      and the input flags (CF, OF, ZAPS), we need to specify the latest versions
      in program order of all the existing flags. However, this would require
      three operands to the shift uop not even counting the value and count
      operands.
        
      Therefore, we use a collcc (collect flags) uop to get all
      the most up to date flags into one result, using three operands for
      ZAPS, CF, OF. This forms a zero word with all the correct flags
      attached, which is then forwarded as the rc operand to the shift.
        
      This may add additional scheduling constraints in the case that one
      of the operands to the shift itself sets the flags, but this is
      fairly rare (generally the shift amount is read from a table and
      loads don't generate flags.
        
      Conveniently, this also lets us directly implement the 65-bit
      rcl/rcr uops in hardware with little additional complexity.
        
      Example:
        
      shl         rd,rc
        
      Becomes:
        
      collcc       t0 = zf,cf,of
      sll<size>   rd = rd,rc,t0

    */

    DECODE(eform, rd, bit(op, 0) ? v_mode : b_mode);
    if (inrange(op, 0xc0, 0xc1)) {
      // byte immediate
      DECODE(iform, ra, b_mode);
    } else if (inrange(op, 0xd0, 0xd1)) {
      ra.type = OPTYPE_IMM;
      ra.imm.imm = 1;
    } else {
      ra.type = OPTYPE_REG;
      ra.reg.reg = APR_cl;
    }

    // Mask off the appropriate number of immediate bits:
    int size = (rd.type == OPTYPE_REG) ? reginfo[rd.reg.reg].sizeshift : rd.mem.size;
    ra.imm.imm = bits(ra.imm.imm, 0, (size == 3) ? 6 : 5);
    int count = ra.imm.imm;

    bool isrot = (bit(modrm.reg, 2) == 0);

    //
    // Variable rotations always set all the flags, possibly merging them with some
    // of the earlier flag values in program order depending on the count. Otherwise
    // the static count (0, 1, >1) determines which flags are set.
    //
    W32 setflags = (ra.type == OPTYPE_REG) ? FLAGS_DEFAULT_ALU : (!count) ? 0 : // count == 0
      (count == 1) ? (isrot ? (SETFLAG_OF|SETFLAG_CF) : (SETFLAG_ZF|SETFLAG_OF|SETFLAG_CF)) : // count == 1
      (isrot ? (SETFLAG_CF) : (SETFLAG_ZF|SETFLAG_CF)); // count > 1

    static const byte translate_opcode[8] = {OP_rotl, OP_rotr, OP_rotcl, OP_rotcr, OP_shl, OP_shr, OP_shl, OP_sar};
    static const byte translate_simple_opcode[8] = {OP_nop, OP_nop, OP_nop, OP_nop, OP_shls, OP_shrs, OP_shls, OP_sars};

    bool simple = ((ra.type == OPTYPE_IMM) & (ra.imm.imm <= SIMPLE_SHIFT_LIMIT) & (translate_simple_opcode[modrm.reg] != OP_nop));
    int translated_opcode = (simple) ? translate_simple_opcode[modrm.reg] : translate_opcode[modrm.reg];

    CheckInvalid();

    // Generate the flag collect uop here:
    if (ra.type == OPTYPE_REG) {
      this << TransOp(OP_collcc, REG_temp5, REG_zf, REG_cf, REG_of, 3, 0, 0, FLAGS_DEFAULT_ALU);
    }
    int rcreg = (ra.type == OPTYPE_REG) ? REG_temp5 : (translated_opcode == OP_rotcl || translated_opcode == OP_rotcr) ? REG_cf : REG_zero;

    alu_reg_or_mem(translated_opcode, rd, ra, setflags, rcreg);

    break;
  }

  case 0xc2 ... 0xc3: {
    // ret near, with and without pop count
    int addend = 0;
    if (op == 0xc2) {
      DECODE(iform, ra, w_mode);
      addend = (W16)ra.imm.imm;
    }

    int sizeshift = (ctx.use64) ? (opsize_prefix ? 1 : 3) : (opsize_prefix ? 1 : 2);
    int size = (1 << sizeshift);
    addend = size + addend;

    CheckInvalid();

    this << TransOp(OP_ld, REG_temp7, REG_rsp, REG_zero, REG_zero, sizeshift);
    this << TransOp(OP_add, REG_rsp, REG_rsp, REG_imm, REG_zero, 3, addend);
    if (!last_flags_update_was_atomic)
      this << TransOp(OP_collcc, REG_temp5, REG_zf, REG_cf, REG_of, 3, 0, 0, FLAGS_DEFAULT_ALU);
    TransOp jmp(OP_jmp, REG_rip, REG_temp7, REG_zero, REG_zero, 3);
    jmp.extshift = BRANCH_HINT_POP_RAS;
    this << jmp;

    end_of_block = true;

    break;
  }

  case 0xc6 ... 0xc7: {
    // move reg_or_mem,imm8|imm16|imm32|imm64 (signed imm for 32-bit to 64-bit form)
    int bytemode = bit(op, 0) ? v_mode : b_mode;
    DECODE(eform, rd, bytemode); DECODE(iform, ra, bytemode);
    CheckInvalid();
    move_reg_or_mem(rd, ra);
    break;
  }

  case 0xc8: {
    // enter imm16,imm8
    // Format: 0xc8 imm16 imm8
    DECODE(iform, rd, w_mode);
    DECODE(iform, ra, b_mode);
    int bytes = (W16)rd.imm.imm;
    int level = (byte)ra.imm.imm;
    // we only support nesting level 0
    if (level != 0) invalid |= true;

    CheckInvalid();

    int sizeshift = (ctx.use64) ? (opsize_prefix ? 1 : 3) : (opsize_prefix ? 1 : 2);

    // Exactly equivalent to:
    // push %rbp
    // mov %rbp,%rsp
    // sub %rsp,imm8

    this << TransOp(OP_st, REG_mem, REG_rsp, REG_imm, REG_rbp, sizeshift, -(1 << sizeshift));
    this << TransOp(OP_sub, REG_rsp, REG_rsp, REG_imm, REG_zero, 3, (1 << sizeshift));

    this << TransOp(OP_mov, REG_rbp, REG_zero, REG_rsp, REG_zero, sizeshift);
    this << TransOp(OP_sub, REG_rsp, REG_rsp, REG_imm, REG_zero, sizeshift, bytes);
    break;
  }

  case 0xc9: {
    // leave
    int sizeshift = (ctx.use64) ? (opsize_prefix ? 1 : 3) : (opsize_prefix ? 1 : 2);
    // Exactly equivalent to:
    // mov %rsp,%rbp
    // pop %rbp

    CheckInvalid();

    // Make idempotent by checking new rsp (aka rbp) alignment first:
    this << TransOp(OP_ld, REG_temp0, REG_rbp, REG_zero, REG_zero, sizeshift);

    this << TransOp(OP_mov, REG_rsp, REG_zero, REG_rbp, REG_zero, sizeshift);
    this << TransOp(OP_ld, REG_rbp, REG_rsp, REG_zero, REG_zero, sizeshift);
    this << TransOp(OP_add, REG_rsp, REG_rsp, REG_imm, REG_zero, 3, (1 << sizeshift));

    break;
  }

  case 0xe8:
  case 0xe9:
  case 0xeb: {
    bool iscall = (op == 0xe8);
    // CALL or JMP rel16/rel32/rel64
    // near conditional branches with 8-bit displacement:
    DECODE(iform, ra, (op == 0xeb) ? b_mode : v_mode);
    CheckInvalid();

    bb.rip_taken = (Waddr)rip + (W64s)ra.imm.imm;
    bb.rip_not_taken = bb.rip_taken;

    int sizeshift = (ctx.use64) ? 3 : 2;

    if (iscall) {
      immediate(REG_temp0, 3, (Waddr)rip);
      this << TransOp(OP_st, REG_mem, REG_rsp, REG_imm, REG_temp0, sizeshift, -(1 << sizeshift));
      this << TransOp(OP_sub, REG_rsp, REG_rsp, REG_imm, REG_zero, sizeshift, (1 << sizeshift));
    }

    if (!last_flags_update_was_atomic)
      this << TransOp(OP_collcc, REG_temp0, REG_zf, REG_cf, REG_of, 3, 0, 0, FLAGS_DEFAULT_ALU);
    TransOp transop(OP_bru, REG_rip, REG_zero, REG_zero, REG_zero, 3);
    transop.extshift = (iscall) ? BRANCH_HINT_PUSH_RAS : 0;
    transop.riptaken = (Waddr)rip + (W64s)ra.imm.imm;
    transop.ripseq = (Waddr)rip + (W64s)ra.imm.imm;
    this << transop;

    end_of_block = true;
    break;
  }

  case 0xf5: {
    // cmc
    // TransOp(int opcode, int rd, int ra, int rb, int rc, int size, W64s rbimm = 0, W64s rcimm = 0, W32 setflags = 0)
    CheckInvalid();
    this << TransOp(OP_xorcc, REG_temp0, REG_cf, REG_imm, REG_zero, 3, FLAG_CF, 0, SETFLAG_CF);
    break;
  }

    //
    // NOTE: Some forms of this are handled by the complex decoder:
    //
  case 0xf6 ... 0xf7: {
    // COMPLEX: handle mul and div in the complex decoder
    if (modrm.reg >= 4) return false;

    // GRP3b and GRP3S
    DECODE(eform, rd, (op & 1) ? v_mode : b_mode);
    CheckInvalid();

    switch (modrm.reg) {
    case 0: // test
      DECODE(iform, ra, (op & 1) ? v_mode : b_mode);
      CheckInvalid();
      alu_reg_or_mem(OP_and, rd, ra, FLAGS_DEFAULT_ALU, REG_zero, true);
      break;
    case 1: // (invalid)
      MakeInvalid();
      break;
    case 2: { // not
      // As an exception to the rule, NOT does not generate any flags. Go figure.
      alu_reg_or_mem(OP_nor, rd, rd, 0, REG_zero);
      break;
    }
    case 3: { // neg r1 => sub r1 = 0, r1
      alu_reg_or_mem(OP_sub, rd, rd, FLAGS_DEFAULT_ALU, REG_zero, false, true);
      break;
    }
    default:
      abort();
    }
    break;
  }

  case 0xf8: { // clc
    CheckInvalid();
    this << TransOp(OP_andcc, REG_temp0, REG_zero, REG_zero, REG_zero, 3, 0, 0, SETFLAG_CF);
    break;
  }
  case 0xf9: { // stc
    CheckInvalid();
    this << TransOp(OP_orcc, REG_temp0, REG_zero, REG_imm, REG_zero, 3, FLAG_CF, 0, SETFLAG_CF);
    break;
  }

  case 0xfc: { // cld
    CheckInvalid();
    // bit 63 of mxcsr is the direction flag:
    this << TransOp(OP_and, REG_iflags, REG_iflags, REG_imm, REG_zero, 3, (W64)~(1LL<<63));
    break;
  }

  case 0xfd: { // std
    CheckInvalid();
    // bit 63 of mxcsr is the direction flag:
    this << TransOp(OP_or, REG_iflags, REG_iflags, REG_imm, REG_zero, 3, (W64)(1LL<<63));
    //assert(false);
    break;
  }

  case 0xfe: {
    // Group 4: inc/dec Eb in register or memory
    // Increments are unusual in that they do NOT update CF.
    DECODE(eform, rd, b_mode);
    CheckInvalid();
    ra.type = OPTYPE_IMM;
    ra.imm.imm = +1;
    alu_reg_or_mem((bit(modrm.reg, 0)) ? OP_sub : OP_add, rd, ra, SETFLAG_ZF|SETFLAG_OF, REG_zero);
    break;
  }

  case 0xff: {
    switch (modrm.reg) {
    case 0:
    case 1: {
      // inc/dec Ev in register or memory
      // Increments are unusual in that they do NOT update CF.
      DECODE(eform, rd, v_mode);
      CheckInvalid();
      ra.type = OPTYPE_IMM;
      ra.imm.imm = +1;
      alu_reg_or_mem((bit(modrm.reg, 0)) ? OP_sub : OP_add, rd, ra, SETFLAG_ZF|SETFLAG_OF, REG_zero);
      break;
    }
    case 2:
    case 4: {
      bool iscall = (modrm.reg == 2);
      // call near Ev
      DECODE(eform, ra, v_mode);
      CheckInvalid();
      // destination unknown:
      bb.rip_taken = 0;
      bb.rip_not_taken = 0;

      if (!last_flags_update_was_atomic)
        this << TransOp(OP_collcc, REG_temp0, REG_zf, REG_cf, REG_of, 3, 0, 0, FLAGS_DEFAULT_ALU);

      int sizeshift = (ctx.use64) ? 3 : 2;
      if (ra.type == OPTYPE_REG) {
        int rareg = arch_pseudo_reg_to_arch_reg[ra.reg.reg];
        int rashift = reginfo[ra.reg.reg].sizeshift;
        // there is no way to encode a 32-bit jump address in x86-64 mode:
        if (ctx.use64 && (rashift == 2)) rashift = 3;
        if (iscall) {
          immediate(REG_temp6, 3, (Waddr)rip);
          this << TransOp(OP_st, REG_mem, REG_rsp, REG_imm, REG_temp6, sizeshift, -(1 << sizeshift));
          this << TransOp(OP_sub, REG_rsp, REG_rsp, REG_imm, REG_zero, sizeshift, 1 << sizeshift);
        }
        // We do not know the taken or not-taken directions yet so just leave them as zero:
        TransOp transop(OP_jmp, REG_rip, rareg, REG_zero, REG_zero, rashift);
        transop.extshift = (iscall) ? BRANCH_HINT_PUSH_RAS : 0;
        this << transop;
      } else if (ra.type == OPTYPE_MEM) {
        // there is no way to encode a 32-bit jump address in x86-64 mode:
        if (ctx.use64 && (ra.mem.size == 2)) ra.mem.size = 3;
        operand_load(REG_temp0, ra);
        if (iscall) {
          immediate(REG_temp6, 3, (Waddr)rip);
          this << TransOp(OP_st, REG_mem, REG_rsp, REG_imm, REG_temp6, sizeshift, -(1 << sizeshift));
          this << TransOp(OP_sub, REG_rsp, REG_rsp, REG_imm, REG_zero, sizeshift, 1 << sizeshift);
        }
        // We do not know the taken or not-taken directions yet so just leave them as zero:
        TransOp transop(OP_jmp, REG_rip, REG_temp0, REG_zero, REG_zero, ra.mem.size);
        transop.extshift = (iscall) ? BRANCH_HINT_PUSH_RAS : 0;
        this << transop;
      }

      end_of_block = true;
      break;
    }
    case 6: {
      // push Ev: push reg or memory
      DECODE(eform, ra, v_mode);
      CheckInvalid();
      rd.type = OPTYPE_REG;
      rd.reg.reg = 0; // not used
      // There is no way to encode 32-bit pushes and pops in 64-bit mode:
      if (ctx.use64 && ra.type == OPTYPE_MEM && ra.mem.size == 2) ra.mem.size = 3;
      move_reg_or_mem(rd, ra, REG_temp7);

      int sizeshift = (ra.type == OPTYPE_REG) ? reginfo[ra.reg.reg].sizeshift : ra.mem.size;
      if (ctx.use64 && sizeshift == 2) sizeshift = 3; // There is no way to encode 32-bit pushes and pops in 64-bit mode:
      this << TransOp(OP_st, REG_mem, REG_rsp, REG_imm, REG_temp7, sizeshift, -(1 << sizeshift));
      this << TransOp(OP_sub, REG_rsp, REG_rsp, REG_imm, REG_zero, (ctx.use64 ? 3 : 2), (1 << sizeshift));

      break;
    }
    default:
      MakeInvalid();
      break;
    }
    break;
  }

  case 0x140 ... 0x14f: {
    // cmov: conditional moves
    DECODE(gform, rd, v_mode);
    DECODE(eform, ra, v_mode);
    CheckInvalid();

    int srcreg;
    int destreg = arch_pseudo_reg_to_arch_reg[rd.reg.reg];
    int sizeshift = reginfo[rd.reg.reg].sizeshift;

    if (ra.type == OPTYPE_REG) {
      srcreg = arch_pseudo_reg_to_arch_reg[ra.reg.reg];
    } else {
      assert(ra.type == OPTYPE_MEM);
      operand_load(REG_temp7, ra);
      srcreg = REG_temp7;
    }

    int condcode = bits(op, 0, 4);
    const CondCodeToFlagRegs& cctfr = cond_code_to_flag_regs[condcode];

    int condreg;
    if (cctfr.req2) {
      this << TransOp(OP_collcc, REG_temp0, REG_zf, REG_cf, REG_of, 3, 0, 0, FLAGS_DEFAULT_ALU);
      condreg = REG_temp0;
    } else {
      condreg = (cctfr.ra != REG_zero) ? cctfr.ra : cctfr.rb;
    }
    assert(condreg != REG_zero);

    TransOp transop(OP_sel, destreg, destreg, srcreg, condreg, sizeshift);
    transop.cond = condcode;
    this << transop, endl;
    break;
  }

  case 0x190 ... 0x19f: {
    // conditional sets
    DECODE(eform, rd, v_mode);
    CheckInvalid();

    int r;

    if (rd.type == OPTYPE_REG) {
      r = arch_pseudo_reg_to_arch_reg[rd.reg.reg];
    } else {
      assert(rd.type == OPTYPE_MEM);
      r = REG_temp7;
    }

    int condcode = bits(op, 0, 4);
    const CondCodeToFlagRegs& cctfr = cond_code_to_flag_regs[condcode];

    int condreg;
    if (cctfr.req2) {
      this << TransOp(OP_collcc, REG_temp0, REG_zf, REG_cf, REG_of, 3, 0, 0, FLAGS_DEFAULT_ALU);
      condreg = REG_temp0;
    } else {
      condreg = (cctfr.ra != REG_zero) ? cctfr.ra : cctfr.rb;
    }
    assert(condreg != REG_zero);

    TransOp transop(OP_set, r, (rd.type == OPTYPE_MEM) ? REG_zero : r, REG_imm, condreg, 0, +1);
    transop.cond = condcode;
    this << transop, endl;

    if (rd.type == OPTYPE_MEM) {
      rd.mem.size = 0;
      result_store(r, REG_temp0, rd);
    }
    break;
  }

  case 0x1a3: // bt ra,rb
  case 0x1bb: // btc ra,rb
  case 0x1b3: // btr ra,rb
  case 0x1ab: { // bts ra,rb
    DECODE(eform, rd, v_mode);
    DECODE(gform, ra, v_mode);
    CheckInvalid();

    if (rd.type == OPTYPE_REG) {
      int rdreg = arch_pseudo_reg_to_arch_reg[rd.reg.reg];
      int rareg = arch_pseudo_reg_to_arch_reg[ra.reg.reg];
      int opcode;
      switch (op) {
      case 0x1a3: opcode = OP_bt; break;
      case 0x1ab: opcode = OP_bts; break;
      case 0x1b3: opcode = OP_btr; break;
      case 0x1bb: opcode = OP_btc; break;
      }
        
      // bt has no output - just flags:
      this << TransOp(opcode, (opcode == OP_bt) ? REG_temp0 : rdreg, rdreg, rareg, REG_zero, 3, 0, 0, SETFLAG_CF);
      break;
    } else {
      // Mem form is more complicated:
      int rareg = arch_pseudo_reg_to_arch_reg[ra.reg.reg];

      int basereg = arch_pseudo_reg_to_arch_reg[rd.mem.basereg];
      int indexreg = arch_pseudo_reg_to_arch_reg[rd.mem.indexreg];

      // [ra + rb*scale + imm32]
      basereg = bias_by_segreg(basereg);
      TransOp addop(OP_adda, REG_temp1, basereg, REG_imm, indexreg, 3, rd.mem.offset);
      addop.extshift = rd.mem.scale;
      this << addop;

      this << TransOp(OP_sar, REG_temp2, rareg, REG_imm, REG_zero, 3, 3); // byte index

      this << TransOp(OP_ld, REG_temp0, REG_temp1, REG_temp2, REG_zero, 0);

      int opcode;
      switch (op) {
      case 0x1a3: opcode = OP_bt; break;
      case 0x1ab: opcode = OP_bts; break;
      case 0x1b3: opcode = OP_btr; break;
      case 0x1bb: opcode = OP_btc; break;
      }

      this << TransOp(opcode, REG_temp0, REG_temp0, rareg, REG_zero, 0, 0, 0, SETFLAG_CF);

      if (opcode != OP_bt) {
        this << TransOp(OP_st, REG_mem, REG_temp1, REG_temp2, REG_temp0, 0);
      }

      break;
    }
  }

  case 0x1ba: { // bt|btc|btr|bts ra,imm
    DECODE(eform, rd, v_mode);
    DECODE(iform, ra, b_mode);
    // Mem form is too complicated and very rare: we don't support it
    if (rd.type != OPTYPE_REG) MakeInvalid();
    CheckInvalid();

    int rdreg = arch_pseudo_reg_to_arch_reg[rd.reg.reg];

    int opcode;
    switch (modrm.reg) {
    case 4: opcode = OP_bt; break;
    case 5: opcode = OP_bts; break;
    case 6: opcode = OP_btr; break;
    case 7: opcode = OP_btc; break;
    default: MakeInvalid();
    }

    // bt has no output - just flags:
    this << TransOp(opcode, (opcode == OP_bt) ? REG_temp0 : rdreg, rdreg, REG_imm, REG_zero, 3, ra.imm.imm, 0, SETFLAG_CF);
    break;
  }

  case 0x118: {
    // prefetchN [eform]
    DECODE(eform, ra, b_mode);
    CheckInvalid();

    static const byte x86_prefetch_to_pt2x_cachelevel[8] = {2, 1, 2, 3};
    int level = x86_prefetch_to_pt2x_cachelevel[modrm.reg];
    operand_load(REG_temp0, ra, OP_ld_pre, level);
    break;
  }

  case 0x10d: {
    // prefetchw [eform] (NOTE: this is an AMD-only insn from K6 onwards)
    DECODE(eform, ra, b_mode);
    CheckInvalid();

    int level = 2;
    operand_load(REG_temp0, ra, OP_ld_pre, level);
    break;
  }

  case 0x1bc: 
  case 0x1bd: {
    // bsf/bsr:
    DECODE(gform, rd, v_mode); DECODE(eform, ra, v_mode);
    CheckInvalid();
    alu_reg_or_mem((op == 0x1bc) ? OP_ctz: OP_clz, rd, ra, FLAGS_DEFAULT_ALU, REG_zero);
    break;
  }

  case 0x1c8 ... 0x1cf: {
    // bswap
    rd.gform_ext(*this, v_mode, bits(op, 0, 3), false, true);
    CheckInvalid();
    int rdreg = arch_pseudo_reg_to_arch_reg[rd.reg.reg];
    int sizeshift = reginfo[rd.reg.reg].sizeshift;
    this << TransOp(OP_bswap, rdreg, (sizeshift >= 2) ? REG_zero : rdreg, rdreg, REG_zero, sizeshift);
    break;
  }

  default: {
    // Let the slow decoder handle it or mark it invalid
    return false;
  }
  }

  return true;
}