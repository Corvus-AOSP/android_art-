/*
 * Copyright (C) 2012 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "codegen_x86.h"
#include "compiler/codegen/codegen_util.h"
#include "compiler/codegen/ralloc_util.h"
#include "x86_lir.h"

namespace art {

/* This file contains codegen for the X86 ISA */

LIR* X86Codegen::OpFpRegCopy(CompilationUnit *cu, int r_dest, int r_src)
{
  int opcode;
  /* must be both DOUBLE or both not DOUBLE */
  DCHECK_EQ(X86_DOUBLEREG(r_dest), X86_DOUBLEREG(r_src));
  if (X86_DOUBLEREG(r_dest)) {
    opcode = kX86MovsdRR;
  } else {
    if (X86_SINGLEREG(r_dest)) {
      if (X86_SINGLEREG(r_src)) {
        opcode = kX86MovssRR;
      } else {  // Fpr <- Gpr
        opcode = kX86MovdxrRR;
      }
    } else {  // Gpr <- Fpr
      DCHECK(X86_SINGLEREG(r_src));
      opcode = kX86MovdrxRR;
    }
  }
  DCHECK_NE((EncodingMap[opcode].flags & IS_BINARY_OP), 0ULL);
  LIR* res = RawLIR(cu, cu->current_dalvik_offset, opcode, r_dest, r_src);
  if (r_dest == r_src) {
    res->flags.is_nop = true;
  }
  return res;
}

bool X86Codegen::InexpensiveConstant(int reg, int value)
{
  return true;
}

/*
 * Load a immediate using a shortcut if possible; otherwise
 * grab from the per-translation literal pool.  If target is
 * a high register, build constant into a low register and copy.
 *
 * No additional register clobbering operation performed. Use this version when
 * 1) r_dest is freshly returned from AllocTemp or
 * 2) The codegen is under fixed register usage
 */
LIR* X86Codegen::LoadConstantNoClobber(CompilationUnit *cu, int r_dest, int value)
{
  int r_dest_save = r_dest;
  if (X86_FPREG(r_dest)) {
    if (value == 0) {
      return NewLIR2(cu, kX86XorpsRR, r_dest, r_dest);
    }
    DCHECK(X86_SINGLEREG(r_dest));
    r_dest = AllocTemp(cu);
  }

  LIR *res;
  if (value == 0) {
    res = NewLIR2(cu, kX86Xor32RR, r_dest, r_dest);
  } else {
    // Note, there is no byte immediate form of a 32 bit immediate move.
    res = NewLIR2(cu, kX86Mov32RI, r_dest, value);
  }

  if (X86_FPREG(r_dest_save)) {
    NewLIR2(cu, kX86MovdxrRR, r_dest_save, r_dest);
    FreeTemp(cu, r_dest);
  }

  return res;
}

LIR* X86Codegen::OpUnconditionalBranch(CompilationUnit* cu, LIR* target)
{
  LIR* res = NewLIR1(cu, kX86Jmp8, 0 /* offset to be patched during assembly*/ );
  res->target = target;
  return res;
}

LIR* X86Codegen::OpCondBranch(CompilationUnit* cu, ConditionCode cc, LIR* target)
{
  LIR* branch = NewLIR2(cu, kX86Jcc8, 0 /* offset to be patched */,
                        X86ConditionEncoding(cc));
  branch->target = target;
  return branch;
}

LIR* X86Codegen::OpReg(CompilationUnit *cu, OpKind op, int r_dest_src)
{
  X86OpCode opcode = kX86Bkpt;
  switch (op) {
    case kOpNeg: opcode = kX86Neg32R; break;
    case kOpNot: opcode = kX86Not32R; break;
    case kOpBlx: opcode = kX86CallR; break;
    default:
      LOG(FATAL) << "Bad case in OpReg " << op;
  }
  return NewLIR1(cu, opcode, r_dest_src);
}

LIR* X86Codegen::OpRegImm(CompilationUnit *cu, OpKind op, int r_dest_src1, int value)
{
  X86OpCode opcode = kX86Bkpt;
  bool byte_imm = IS_SIMM8(value);
  DCHECK(!X86_FPREG(r_dest_src1));
  switch (op) {
    case kOpLsl: opcode = kX86Sal32RI; break;
    case kOpLsr: opcode = kX86Shr32RI; break;
    case kOpAsr: opcode = kX86Sar32RI; break;
    case kOpAdd: opcode = byte_imm ? kX86Add32RI8 : kX86Add32RI; break;
    case kOpOr:  opcode = byte_imm ? kX86Or32RI8  : kX86Or32RI;  break;
    case kOpAdc: opcode = byte_imm ? kX86Adc32RI8 : kX86Adc32RI; break;
    //case kOpSbb: opcode = kX86Sbb32RI; break;
    case kOpAnd: opcode = byte_imm ? kX86And32RI8 : kX86And32RI; break;
    case kOpSub: opcode = byte_imm ? kX86Sub32RI8 : kX86Sub32RI; break;
    case kOpXor: opcode = byte_imm ? kX86Xor32RI8 : kX86Xor32RI; break;
    case kOpCmp: opcode = byte_imm ? kX86Cmp32RI8 : kX86Cmp32RI; break;
    case kOpMov: return LoadConstantNoClobber(cu, r_dest_src1, value);
    case kOpMul:
      opcode = byte_imm ? kX86Imul32RRI8 : kX86Imul32RRI;
      return NewLIR3(cu, opcode, r_dest_src1, r_dest_src1, value);
    default:
      LOG(FATAL) << "Bad case in OpRegImm " << op;
  }
  return NewLIR2(cu, opcode, r_dest_src1, value);
}

LIR* X86Codegen::OpRegReg(CompilationUnit *cu, OpKind op, int r_dest_src1, int r_src2)
{
    X86OpCode opcode = kX86Nop;
    bool src2_must_be_cx = false;
    switch (op) {
        // X86 unary opcodes
      case kOpMvn:
        OpRegCopy(cu, r_dest_src1, r_src2);
        return OpReg(cu, kOpNot, r_dest_src1);
      case kOpNeg:
        OpRegCopy(cu, r_dest_src1, r_src2);
        return OpReg(cu, kOpNeg, r_dest_src1);
        // X86 binary opcodes
      case kOpSub: opcode = kX86Sub32RR; break;
      case kOpSbc: opcode = kX86Sbb32RR; break;
      case kOpLsl: opcode = kX86Sal32RC; src2_must_be_cx = true; break;
      case kOpLsr: opcode = kX86Shr32RC; src2_must_be_cx = true; break;
      case kOpAsr: opcode = kX86Sar32RC; src2_must_be_cx = true; break;
      case kOpMov: opcode = kX86Mov32RR; break;
      case kOpCmp: opcode = kX86Cmp32RR; break;
      case kOpAdd: opcode = kX86Add32RR; break;
      case kOpAdc: opcode = kX86Adc32RR; break;
      case kOpAnd: opcode = kX86And32RR; break;
      case kOpOr:  opcode = kX86Or32RR; break;
      case kOpXor: opcode = kX86Xor32RR; break;
      case kOp2Byte:
        // Use shifts instead of a byte operand if the source can't be byte accessed.
        if (r_src2 >= 4) {
          NewLIR2(cu, kX86Mov32RR, r_dest_src1, r_src2);
          NewLIR2(cu, kX86Sal32RI, r_dest_src1, 24);
          return NewLIR2(cu, kX86Sar32RI, r_dest_src1, 24);
        } else {
          opcode = kX86Movsx8RR;
        }
        break;
      case kOp2Short: opcode = kX86Movsx16RR; break;
      case kOp2Char: opcode = kX86Movzx16RR; break;
      case kOpMul: opcode = kX86Imul32RR; break;
      default:
        LOG(FATAL) << "Bad case in OpRegReg " << op;
        break;
    }
    CHECK(!src2_must_be_cx || r_src2 == rCX);
    return NewLIR2(cu, opcode, r_dest_src1, r_src2);
}

LIR* X86Codegen::OpRegMem(CompilationUnit *cu, OpKind op, int r_dest, int rBase,
              int offset)
{
  X86OpCode opcode = kX86Nop;
  switch (op) {
      // X86 binary opcodes
    case kOpSub: opcode = kX86Sub32RM; break;
    case kOpMov: opcode = kX86Mov32RM; break;
    case kOpCmp: opcode = kX86Cmp32RM; break;
    case kOpAdd: opcode = kX86Add32RM; break;
    case kOpAnd: opcode = kX86And32RM; break;
    case kOpOr:  opcode = kX86Or32RM; break;
    case kOpXor: opcode = kX86Xor32RM; break;
    case kOp2Byte: opcode = kX86Movsx8RM; break;
    case kOp2Short: opcode = kX86Movsx16RM; break;
    case kOp2Char: opcode = kX86Movzx16RM; break;
    case kOpMul:
    default:
      LOG(FATAL) << "Bad case in OpRegMem " << op;
      break;
  }
  return NewLIR3(cu, opcode, r_dest, rBase, offset);
}

LIR* X86Codegen::OpRegRegReg(CompilationUnit *cu, OpKind op, int r_dest, int r_src1,
                 int r_src2)
{
  if (r_dest != r_src1 && r_dest != r_src2) {
    if (op == kOpAdd) { // lea special case, except can't encode rbp as base
      if (r_src1 == r_src2) {
        OpRegCopy(cu, r_dest, r_src1);
        return OpRegImm(cu, kOpLsl, r_dest, 1);
      } else if (r_src1 != rBP) {
        return NewLIR5(cu, kX86Lea32RA, r_dest, r_src1 /* base */,
                       r_src2 /* index */, 0 /* scale */, 0 /* disp */);
      } else {
        return NewLIR5(cu, kX86Lea32RA, r_dest, r_src2 /* base */,
                       r_src1 /* index */, 0 /* scale */, 0 /* disp */);
      }
    } else {
      OpRegCopy(cu, r_dest, r_src1);
      return OpRegReg(cu, op, r_dest, r_src2);
    }
  } else if (r_dest == r_src1) {
    return OpRegReg(cu, op, r_dest, r_src2);
  } else {  // r_dest == r_src2
    switch (op) {
      case kOpSub:  // non-commutative
        OpReg(cu, kOpNeg, r_dest);
        op = kOpAdd;
        break;
      case kOpSbc:
      case kOpLsl: case kOpLsr: case kOpAsr: case kOpRor: {
        int t_reg = AllocTemp(cu);
        OpRegCopy(cu, t_reg, r_src1);
        OpRegReg(cu, op, t_reg, r_src2);
        LIR* res = OpRegCopy(cu, r_dest, t_reg);
        FreeTemp(cu, t_reg);
        return res;
      }
      case kOpAdd:  // commutative
      case kOpOr:
      case kOpAdc:
      case kOpAnd:
      case kOpXor:
        break;
      default:
        LOG(FATAL) << "Bad case in OpRegRegReg " << op;
    }
    return OpRegReg(cu, op, r_dest, r_src1);
  }
}

LIR* X86Codegen::OpRegRegImm(CompilationUnit *cu, OpKind op, int r_dest, int r_src,
                 int value)
{
  if (op == kOpMul) {
    X86OpCode opcode = IS_SIMM8(value) ? kX86Imul32RRI8 : kX86Imul32RRI;
    return NewLIR3(cu, opcode, r_dest, r_src, value);
  } else if (op == kOpAnd) {
    if (value == 0xFF && r_src < 4) {
      return NewLIR2(cu, kX86Movzx8RR, r_dest, r_src);
    } else if (value == 0xFFFF) {
      return NewLIR2(cu, kX86Movzx16RR, r_dest, r_src);
    }
  }
  if (r_dest != r_src) {
    if (false && op == kOpLsl && value >= 0 && value <= 3) { // lea shift special case
      // TODO: fix bug in LEA encoding when disp == 0
      return NewLIR5(cu, kX86Lea32RA, r_dest,  r5sib_no_base /* base */,
                     r_src /* index */, value /* scale */, 0 /* disp */);
    } else if (op == kOpAdd) { // lea add special case
      return NewLIR5(cu, kX86Lea32RA, r_dest, r_src /* base */,
                     r4sib_no_index /* index */, 0 /* scale */, value /* disp */);
    }
    OpRegCopy(cu, r_dest, r_src);
  }
  return OpRegImm(cu, op, r_dest, value);
}

LIR* X86Codegen::OpThreadMem(CompilationUnit* cu, OpKind op, int thread_offset)
{
  X86OpCode opcode = kX86Bkpt;
  switch (op) {
    case kOpBlx: opcode = kX86CallT;  break;
    default:
      LOG(FATAL) << "Bad opcode: " << op;
      break;
  }
  return NewLIR1(cu, opcode, thread_offset);
}

LIR* X86Codegen::OpMem(CompilationUnit* cu, OpKind op, int rBase, int disp)
{
  X86OpCode opcode = kX86Bkpt;
  switch (op) {
    case kOpBlx: opcode = kX86CallM;  break;
    default:
      LOG(FATAL) << "Bad opcode: " << op;
      break;
  }
  return NewLIR2(cu, opcode, rBase, disp);
}

LIR* X86Codegen::LoadConstantValueWide(CompilationUnit *cu, int r_dest_lo,
                                       int r_dest_hi, int val_lo, int val_hi)
{
    LIR *res;
    if (X86_FPREG(r_dest_lo)) {
      DCHECK(X86_FPREG(r_dest_hi));  // ignore r_dest_hi
      if (val_lo == 0 && val_hi == 0) {
        return NewLIR2(cu, kX86XorpsRR, r_dest_lo, r_dest_lo);
      } else {
        if (val_lo == 0) {
          res = NewLIR2(cu, kX86XorpsRR, r_dest_lo, r_dest_lo);
        } else {
          res = LoadConstantNoClobber(cu, r_dest_lo, val_lo);
        }
        if (val_hi != 0) {
          LoadConstantNoClobber(cu, r_dest_hi, val_hi);
          NewLIR2(cu, kX86PsllqRI, r_dest_hi, 32);
          NewLIR2(cu, kX86OrpsRR, r_dest_lo, r_dest_hi);
        }
      }
    } else {
      res = LoadConstantNoClobber(cu, r_dest_lo, val_lo);
      LoadConstantNoClobber(cu, r_dest_hi, val_hi);
    }
    return res;
}

LIR* X86Codegen::LoadBaseIndexedDisp(CompilationUnit *cu, int rBase, int r_index, int scale,
                                     int displacement, int r_dest, int r_dest_hi, OpSize size,
                                     int s_reg) {
  LIR *load = NULL;
  LIR *load2 = NULL;
  bool is_array = r_index != INVALID_REG;
  bool pair = false;
  bool is64bit = false;
  X86OpCode opcode = kX86Nop;
  switch (size) {
    case kLong:
    case kDouble:
      is64bit = true;
      if (X86_FPREG(r_dest)) {
        opcode = is_array ? kX86MovsdRA : kX86MovsdRM;
        if (X86_SINGLEREG(r_dest)) {
          DCHECK(X86_FPREG(r_dest_hi));
          DCHECK_EQ(r_dest, (r_dest_hi - 1));
          r_dest = S2d(r_dest, r_dest_hi);
        }
        r_dest_hi = r_dest + 1;
      } else {
        pair = true;
        opcode = is_array ? kX86Mov32RA  : kX86Mov32RM;
      }
      // TODO: double store is to unaligned address
      DCHECK_EQ((displacement & 0x3), 0);
      break;
    case kWord:
    case kSingle:
      opcode = is_array ? kX86Mov32RA : kX86Mov32RM;
      if (X86_FPREG(r_dest)) {
        opcode = is_array ? kX86MovssRA : kX86MovssRM;
        DCHECK(X86_SINGLEREG(r_dest));
      }
      DCHECK_EQ((displacement & 0x3), 0);
      break;
    case kUnsignedHalf:
      opcode = is_array ? kX86Movzx16RA : kX86Movzx16RM;
      DCHECK_EQ((displacement & 0x1), 0);
      break;
    case kSignedHalf:
      opcode = is_array ? kX86Movsx16RA : kX86Movsx16RM;
      DCHECK_EQ((displacement & 0x1), 0);
      break;
    case kUnsignedByte:
      opcode = is_array ? kX86Movzx8RA : kX86Movzx8RM;
      break;
    case kSignedByte:
      opcode = is_array ? kX86Movsx8RA : kX86Movsx8RM;
      break;
    default:
      LOG(FATAL) << "Bad case in LoadBaseIndexedDispBody";
  }

  if (!is_array) {
    if (!pair) {
      load = NewLIR3(cu, opcode, r_dest, rBase, displacement + LOWORD_OFFSET);
    } else {
      if (rBase == r_dest) {
        load2 = NewLIR3(cu, opcode, r_dest_hi, rBase,
                        displacement + HIWORD_OFFSET);
        load = NewLIR3(cu, opcode, r_dest, rBase, displacement + LOWORD_OFFSET);
      } else {
        load = NewLIR3(cu, opcode, r_dest, rBase, displacement + LOWORD_OFFSET);
        load2 = NewLIR3(cu, opcode, r_dest_hi, rBase,
                        displacement + HIWORD_OFFSET);
      }
    }
    if (rBase == rX86_SP) {
      AnnotateDalvikRegAccess(cu, load, (displacement + (pair ? LOWORD_OFFSET : 0)) >> 2,
                              true /* is_load */, is64bit);
      if (pair) {
        AnnotateDalvikRegAccess(cu, load2, (displacement + HIWORD_OFFSET) >> 2,
                                true /* is_load */, is64bit);
      }
    }
  } else {
    if (!pair) {
      load = NewLIR5(cu, opcode, r_dest, rBase, r_index, scale,
                     displacement + LOWORD_OFFSET);
    } else {
      if (rBase == r_dest) {
        load2 = NewLIR5(cu, opcode, r_dest_hi, rBase, r_index, scale,
                        displacement + HIWORD_OFFSET);
        load = NewLIR5(cu, opcode, r_dest, rBase, r_index, scale,
                       displacement + LOWORD_OFFSET);
      } else {
        load = NewLIR5(cu, opcode, r_dest, rBase, r_index, scale,
                       displacement + LOWORD_OFFSET);
        load2 = NewLIR5(cu, opcode, r_dest_hi, rBase, r_index, scale,
                        displacement + HIWORD_OFFSET);
      }
    }
  }

  return load;
}

/* Load value from base + scaled index. */
LIR* X86Codegen::LoadBaseIndexed(CompilationUnit *cu, int rBase,
                     int r_index, int r_dest, int scale, OpSize size) {
  return LoadBaseIndexedDisp(cu, rBase, r_index, scale, 0,
                             r_dest, INVALID_REG, size, INVALID_SREG);
}

LIR* X86Codegen::LoadBaseDisp(CompilationUnit *cu, int rBase, int displacement,
                  int r_dest, OpSize size, int s_reg) {
  return LoadBaseIndexedDisp(cu, rBase, INVALID_REG, 0, displacement,
                             r_dest, INVALID_REG, size, s_reg);
}

LIR* X86Codegen::LoadBaseDispWide(CompilationUnit *cu, int rBase, int displacement,
                      int r_dest_lo, int r_dest_hi, int s_reg) {
  return LoadBaseIndexedDisp(cu, rBase, INVALID_REG, 0, displacement,
                             r_dest_lo, r_dest_hi, kLong, s_reg);
}

LIR* X86Codegen::StoreBaseIndexedDisp(CompilationUnit *cu, int rBase, int r_index, int scale,
                                      int displacement, int r_src, int r_src_hi, OpSize size,
                                      int s_reg) {
  LIR *store = NULL;
  LIR *store2 = NULL;
  bool is_array = r_index != INVALID_REG;
  bool pair = false;
  bool is64bit = false;
  X86OpCode opcode = kX86Nop;
  switch (size) {
    case kLong:
    case kDouble:
      is64bit = true;
      if (X86_FPREG(r_src)) {
        opcode = is_array ? kX86MovsdAR : kX86MovsdMR;
        if (X86_SINGLEREG(r_src)) {
          DCHECK(X86_FPREG(r_src_hi));
          DCHECK_EQ(r_src, (r_src_hi - 1));
          r_src = S2d(r_src, r_src_hi);
        }
        r_src_hi = r_src + 1;
      } else {
        pair = true;
        opcode = is_array ? kX86Mov32AR  : kX86Mov32MR;
      }
      // TODO: double store is to unaligned address
      DCHECK_EQ((displacement & 0x3), 0);
      break;
    case kWord:
    case kSingle:
      opcode = is_array ? kX86Mov32AR : kX86Mov32MR;
      if (X86_FPREG(r_src)) {
        opcode = is_array ? kX86MovssAR : kX86MovssMR;
        DCHECK(X86_SINGLEREG(r_src));
      }
      DCHECK_EQ((displacement & 0x3), 0);
      break;
    case kUnsignedHalf:
    case kSignedHalf:
      opcode = is_array ? kX86Mov16AR : kX86Mov16MR;
      DCHECK_EQ((displacement & 0x1), 0);
      break;
    case kUnsignedByte:
    case kSignedByte:
      opcode = is_array ? kX86Mov8AR : kX86Mov8MR;
      break;
    default:
      LOG(FATAL) << "Bad case in LoadBaseIndexedDispBody";
  }

  if (!is_array) {
    if (!pair) {
      store = NewLIR3(cu, opcode, rBase, displacement + LOWORD_OFFSET, r_src);
    } else {
      store = NewLIR3(cu, opcode, rBase, displacement + LOWORD_OFFSET, r_src);
      store2 = NewLIR3(cu, opcode, rBase, displacement + HIWORD_OFFSET, r_src_hi);
    }
    if (rBase == rX86_SP) {
      AnnotateDalvikRegAccess(cu, store, (displacement + (pair ? LOWORD_OFFSET : 0)) >> 2,
                              false /* is_load */, is64bit);
      if (pair) {
        AnnotateDalvikRegAccess(cu, store2, (displacement + HIWORD_OFFSET) >> 2,
                                false /* is_load */, is64bit);
      }
    }
  } else {
    if (!pair) {
      store = NewLIR5(cu, opcode, rBase, r_index, scale,
                      displacement + LOWORD_OFFSET, r_src);
    } else {
      store = NewLIR5(cu, opcode, rBase, r_index, scale,
                      displacement + LOWORD_OFFSET, r_src);
      store2 = NewLIR5(cu, opcode, rBase, r_index, scale,
                       displacement + HIWORD_OFFSET, r_src_hi);
    }
  }

  return store;
}

/* store value base base + scaled index. */
LIR* X86Codegen::StoreBaseIndexed(CompilationUnit *cu, int rBase, int r_index, int r_src,
                      int scale, OpSize size)
{
  return StoreBaseIndexedDisp(cu, rBase, r_index, scale, 0,
                              r_src, INVALID_REG, size, INVALID_SREG);
}

LIR* X86Codegen::StoreBaseDisp(CompilationUnit *cu, int rBase, int displacement,
                               int r_src, OpSize size)
{
    return StoreBaseIndexedDisp(cu, rBase, INVALID_REG, 0,
                                displacement, r_src, INVALID_REG, size,
                                INVALID_SREG);
}

LIR* X86Codegen::StoreBaseDispWide(CompilationUnit *cu, int rBase, int displacement,
                                   int r_src_lo, int r_src_hi)
{
  return StoreBaseIndexedDisp(cu, rBase, INVALID_REG, 0, displacement,
                              r_src_lo, r_src_hi, kLong, INVALID_SREG);
}

}  // namespace art
