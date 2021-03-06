/*
  Copyright (c) INRIA and Microsoft Corporation. All rights reserved.
  Licensed under the Apache 2.0 License.

  This file was generated by KreMLin <https://github.com/FStarLang/kremlin>
  KreMLin invocation: ../krml -fparentheses -fcurly-braces -fno-shadow -header copyright-header.txt -minimal -tmpdir dist/minimal -skip-compilation -extract-uints -add-include <inttypes.h> -add-include <stdbool.h> -add-include "kremlin/internal/compat.h" -add-include "kremlin/lowstar_endianness.h" -add-include "kremlin/internal/types.h" -bundle FStar.UInt64+FStar.UInt32+FStar.UInt16+FStar.UInt8=[rename=FStar_UInt_8_16_32_64] -bundle C.Endianness= -library FStar.UInt128 -bundle FStar.UInt128= -bundle *,WindowsWorkaroundSigh fstar_uint128.c -o libkremlib.a .extract/prims.krml .extract/FStar_Pervasives_Native.krml .extract/FStar_Pervasives.krml .extract/FStar_Reflection_Types.krml .extract/FStar_Reflection_Data.krml .extract/FStar_Order.krml .extract/FStar_Reflection_Basic.krml .extract/FStar_Preorder.krml .extract/FStar_Calc.krml .extract/FStar_Squash.krml .extract/FStar_Classical.krml .extract/FStar_StrongExcludedMiddle.krml .extract/FStar_FunctionalExtensionality.krml .extract/FStar_List_Tot_Base.krml .extract/FStar_List_Tot_Properties.krml .extract/FStar_List_Tot.krml .extract/FStar_Seq_Base.krml .extract/FStar_Seq_Properties.krml .extract/FStar_Seq.krml .extract/FStar_Mul.krml .extract/FStar_Math_Lib.krml .extract/FStar_Math_Lemmas.krml .extract/FStar_BitVector.krml .extract/FStar_UInt.krml .extract/FStar_UInt32.krml .extract/FStar_Int.krml .extract/FStar_Int16.krml .extract/FStar_Ghost.krml .extract/FStar_ErasedLogic.krml .extract/FStar_UInt64.krml .extract/FStar_Set.krml .extract/FStar_PropositionalExtensionality.krml .extract/FStar_PredicateExtensionality.krml .extract/FStar_TSet.krml .extract/FStar_Monotonic_Heap.krml .extract/FStar_Heap.krml .extract/FStar_Map.krml .extract/FStar_Monotonic_HyperHeap.krml .extract/FStar_Monotonic_HyperStack.krml .extract/FStar_HyperStack.krml .extract/FStar_Monotonic_Witnessed.krml .extract/FStar_HyperStack_ST.krml .extract/FStar_HyperStack_All.krml .extract/FStar_Char.krml .extract/FStar_Exn.krml .extract/FStar_ST.krml .extract/FStar_All.krml .extract/FStar_List.krml .extract/FStar_String.krml .extract/FStar_Reflection_Const.krml .extract/FStar_Reflection_Derived.krml .extract/FStar_Reflection_Derived_Lemmas.krml .extract/FStar_Date.krml .extract/FStar_Universe.krml .extract/FStar_GSet.krml .extract/FStar_ModifiesGen.krml .extract/FStar_Range.krml .extract/FStar_Tactics_Types.krml .extract/FStar_Tactics_Result.krml .extract/FStar_Tactics_Effect.krml .extract/FStar_Tactics_Util.krml .extract/FStar_Tactics_Builtins.krml .extract/FStar_Reflection_Formula.krml .extract/FStar_Reflection.krml .extract/FStar_Tactics_Derived.krml .extract/FStar_Tactics_Logic.krml .extract/FStar_Tactics.krml .extract/FStar_BigOps.krml .extract/LowStar_Monotonic_Buffer.krml .extract/LowStar_Buffer.krml .extract/Spec_Loops.krml .extract/LowStar_BufferOps.krml .extract/C_Loops.krml .extract/FStar_UInt8.krml .extract/FStar_Kremlin_Endianness.krml .extract/FStar_UInt63.krml .extract/FStar_Dyn.krml .extract/FStar_Int63.krml .extract/FStar_Int64.krml .extract/FStar_Int32.krml .extract/FStar_Int8.krml .extract/FStar_UInt16.krml .extract/FStar_Int_Cast.krml .extract/FStar_UInt128.krml .extract/C_Endianness.krml .extract/WasmSupport.krml .extract/FStar_Float.krml .extract/FStar_IO.krml .extract/C.krml .extract/LowStar_Modifies.krml .extract/FStar_Bytes.krml .extract/C_String.krml .extract/FStar_HyperStack_IO.krml .extract/C_Failure.krml .extract/TestLib.krml .extract/FStar_Int_Cast_Full.krml
  F* version: 74c6d2a5
  KreMLin version: 1bd260eb
*/



#ifndef __FStar_UInt_8_16_32_64_H
#define __FStar_UInt_8_16_32_64_H


#include <inttypes.h>
#include <stdbool.h>
#include "kremlin/internal/compat.h"
#include "kremlin/lowstar_endianness.h"
#include "kremlin/internal/types.h"

extern Prims_int FStar_UInt64_n;

extern Prims_int FStar_UInt64_v(uint64_t x);

extern uint64_t FStar_UInt64_uint_to_t(Prims_int x);

extern uint64_t FStar_UInt64_add(uint64_t a, uint64_t b);

extern uint64_t FStar_UInt64_add_underspec(uint64_t a, uint64_t b);

extern uint64_t FStar_UInt64_add_mod(uint64_t a, uint64_t b);

extern uint64_t FStar_UInt64_sub(uint64_t a, uint64_t b);

extern uint64_t FStar_UInt64_sub_underspec(uint64_t a, uint64_t b);

extern uint64_t FStar_UInt64_sub_mod(uint64_t a, uint64_t b);

extern uint64_t FStar_UInt64_mul(uint64_t a, uint64_t b);

extern uint64_t FStar_UInt64_mul_underspec(uint64_t a, uint64_t b);

extern uint64_t FStar_UInt64_mul_mod(uint64_t a, uint64_t b);

extern uint64_t FStar_UInt64_mul_div(uint64_t a, uint64_t b);

extern uint64_t FStar_UInt64_div(uint64_t a, uint64_t b);

extern uint64_t FStar_UInt64_rem(uint64_t a, uint64_t b);

extern uint64_t FStar_UInt64_logand(uint64_t x, uint64_t y);

extern uint64_t FStar_UInt64_logxor(uint64_t x, uint64_t y);

extern uint64_t FStar_UInt64_logor(uint64_t x, uint64_t y);

extern uint64_t FStar_UInt64_lognot(uint64_t x);

extern uint64_t FStar_UInt64_shift_right(uint64_t a, uint32_t s);

extern uint64_t FStar_UInt64_shift_left(uint64_t a, uint32_t s);

extern bool FStar_UInt64_eq(uint64_t a, uint64_t b);

extern bool FStar_UInt64_gt(uint64_t a, uint64_t b);

extern bool FStar_UInt64_gte(uint64_t a, uint64_t b);

extern bool FStar_UInt64_lt(uint64_t a, uint64_t b);

extern bool FStar_UInt64_lte(uint64_t a, uint64_t b);

extern uint64_t FStar_UInt64_minus(uint64_t a);

extern uint32_t FStar_UInt64_n_minus_one;

uint64_t FStar_UInt64_eq_mask(uint64_t a, uint64_t b);

uint64_t FStar_UInt64_gte_mask(uint64_t a, uint64_t b);

extern Prims_string FStar_UInt64_to_string(uint64_t uu____722);

extern uint64_t FStar_UInt64_of_string(Prims_string uu____734);

extern Prims_int FStar_UInt32_n;

extern Prims_int FStar_UInt32_v(uint32_t x);

extern uint32_t FStar_UInt32_uint_to_t(Prims_int x);

extern uint32_t FStar_UInt32_add(uint32_t a, uint32_t b);

extern uint32_t FStar_UInt32_add_underspec(uint32_t a, uint32_t b);

extern uint32_t FStar_UInt32_add_mod(uint32_t a, uint32_t b);

extern uint32_t FStar_UInt32_sub(uint32_t a, uint32_t b);

extern uint32_t FStar_UInt32_sub_underspec(uint32_t a, uint32_t b);

extern uint32_t FStar_UInt32_sub_mod(uint32_t a, uint32_t b);

extern uint32_t FStar_UInt32_mul(uint32_t a, uint32_t b);

extern uint32_t FStar_UInt32_mul_underspec(uint32_t a, uint32_t b);

extern uint32_t FStar_UInt32_mul_mod(uint32_t a, uint32_t b);

extern uint32_t FStar_UInt32_mul_div(uint32_t a, uint32_t b);

extern uint32_t FStar_UInt32_div(uint32_t a, uint32_t b);

extern uint32_t FStar_UInt32_rem(uint32_t a, uint32_t b);

extern uint32_t FStar_UInt32_logand(uint32_t x, uint32_t y);

extern uint32_t FStar_UInt32_logxor(uint32_t x, uint32_t y);

extern uint32_t FStar_UInt32_logor(uint32_t x, uint32_t y);

extern uint32_t FStar_UInt32_lognot(uint32_t x);

extern uint32_t FStar_UInt32_shift_right(uint32_t a, uint32_t s);

extern uint32_t FStar_UInt32_shift_left(uint32_t a, uint32_t s);

extern bool FStar_UInt32_eq(uint32_t a, uint32_t b);

extern bool FStar_UInt32_gt(uint32_t a, uint32_t b);

extern bool FStar_UInt32_gte(uint32_t a, uint32_t b);

extern bool FStar_UInt32_lt(uint32_t a, uint32_t b);

extern bool FStar_UInt32_lte(uint32_t a, uint32_t b);

extern uint32_t FStar_UInt32_minus(uint32_t a);

extern uint32_t FStar_UInt32_n_minus_one;

uint32_t FStar_UInt32_eq_mask(uint32_t a, uint32_t b);

uint32_t FStar_UInt32_gte_mask(uint32_t a, uint32_t b);

extern Prims_string FStar_UInt32_to_string(uint32_t uu____722);

extern uint32_t FStar_UInt32_of_string(Prims_string uu____734);

extern Prims_int FStar_UInt16_n;

extern Prims_int FStar_UInt16_v(uint16_t x);

extern uint16_t FStar_UInt16_uint_to_t(Prims_int x);

extern uint16_t FStar_UInt16_add(uint16_t a, uint16_t b);

extern uint16_t FStar_UInt16_add_underspec(uint16_t a, uint16_t b);

extern uint16_t FStar_UInt16_add_mod(uint16_t a, uint16_t b);

extern uint16_t FStar_UInt16_sub(uint16_t a, uint16_t b);

extern uint16_t FStar_UInt16_sub_underspec(uint16_t a, uint16_t b);

extern uint16_t FStar_UInt16_sub_mod(uint16_t a, uint16_t b);

extern uint16_t FStar_UInt16_mul(uint16_t a, uint16_t b);

extern uint16_t FStar_UInt16_mul_underspec(uint16_t a, uint16_t b);

extern uint16_t FStar_UInt16_mul_mod(uint16_t a, uint16_t b);

extern uint16_t FStar_UInt16_mul_div(uint16_t a, uint16_t b);

extern uint16_t FStar_UInt16_div(uint16_t a, uint16_t b);

extern uint16_t FStar_UInt16_rem(uint16_t a, uint16_t b);

extern uint16_t FStar_UInt16_logand(uint16_t x, uint16_t y);

extern uint16_t FStar_UInt16_logxor(uint16_t x, uint16_t y);

extern uint16_t FStar_UInt16_logor(uint16_t x, uint16_t y);

extern uint16_t FStar_UInt16_lognot(uint16_t x);

extern uint16_t FStar_UInt16_shift_right(uint16_t a, uint32_t s);

extern uint16_t FStar_UInt16_shift_left(uint16_t a, uint32_t s);

extern bool FStar_UInt16_eq(uint16_t a, uint16_t b);

extern bool FStar_UInt16_gt(uint16_t a, uint16_t b);

extern bool FStar_UInt16_gte(uint16_t a, uint16_t b);

extern bool FStar_UInt16_lt(uint16_t a, uint16_t b);

extern bool FStar_UInt16_lte(uint16_t a, uint16_t b);

extern uint16_t FStar_UInt16_minus(uint16_t a);

extern uint32_t FStar_UInt16_n_minus_one;

uint16_t FStar_UInt16_eq_mask(uint16_t a, uint16_t b);

uint16_t FStar_UInt16_gte_mask(uint16_t a, uint16_t b);

extern Prims_string FStar_UInt16_to_string(uint16_t uu____722);

extern uint16_t FStar_UInt16_of_string(Prims_string uu____734);

extern Prims_int FStar_UInt8_n;

extern Prims_int FStar_UInt8_v(uint8_t x);

extern uint8_t FStar_UInt8_uint_to_t(Prims_int x);

extern uint8_t FStar_UInt8_add(uint8_t a, uint8_t b);

extern uint8_t FStar_UInt8_add_underspec(uint8_t a, uint8_t b);

extern uint8_t FStar_UInt8_add_mod(uint8_t a, uint8_t b);

extern uint8_t FStar_UInt8_sub(uint8_t a, uint8_t b);

extern uint8_t FStar_UInt8_sub_underspec(uint8_t a, uint8_t b);

extern uint8_t FStar_UInt8_sub_mod(uint8_t a, uint8_t b);

extern uint8_t FStar_UInt8_mul(uint8_t a, uint8_t b);

extern uint8_t FStar_UInt8_mul_underspec(uint8_t a, uint8_t b);

extern uint8_t FStar_UInt8_mul_mod(uint8_t a, uint8_t b);

extern uint8_t FStar_UInt8_mul_div(uint8_t a, uint8_t b);

extern uint8_t FStar_UInt8_div(uint8_t a, uint8_t b);

extern uint8_t FStar_UInt8_rem(uint8_t a, uint8_t b);

extern uint8_t FStar_UInt8_logand(uint8_t x, uint8_t y);

extern uint8_t FStar_UInt8_logxor(uint8_t x, uint8_t y);

extern uint8_t FStar_UInt8_logor(uint8_t x, uint8_t y);

extern uint8_t FStar_UInt8_lognot(uint8_t x);

extern uint8_t FStar_UInt8_shift_right(uint8_t a, uint32_t s);

extern uint8_t FStar_UInt8_shift_left(uint8_t a, uint32_t s);

extern bool FStar_UInt8_eq(uint8_t a, uint8_t b);

extern bool FStar_UInt8_gt(uint8_t a, uint8_t b);

extern bool FStar_UInt8_gte(uint8_t a, uint8_t b);

extern bool FStar_UInt8_lt(uint8_t a, uint8_t b);

extern bool FStar_UInt8_lte(uint8_t a, uint8_t b);

extern uint8_t FStar_UInt8_minus(uint8_t a);

extern uint32_t FStar_UInt8_n_minus_one;

uint8_t FStar_UInt8_eq_mask(uint8_t a, uint8_t b);

uint8_t FStar_UInt8_gte_mask(uint8_t a, uint8_t b);

extern Prims_string FStar_UInt8_to_string(uint8_t uu____722);

extern uint8_t FStar_UInt8_of_string(Prims_string uu____734);

typedef uint8_t FStar_UInt8_byte;

#define __FStar_UInt_8_16_32_64_H_DEFINED
#endif
