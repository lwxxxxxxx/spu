// Copyright 2024 Ant Group Co., Ltd.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "libspu/mpc/kernel.h"

namespace spu::mpc::swift {

// rank_send : send msg
// rank_hash : send H(msg)
// rank_recv : receiver
// P_send, P_hash -> P_recv : msg
void JointMessagePassing(KernelEvalContext* ctx, NdArrayRef& msg,
                         size_t rank_send, size_t rank_hash, size_t rank_recv,
                         std::string_view tag);

// Pi, Pj jonit generate share of a value that is known to both
NdArrayRef JointSharing(KernelEvalContext* ctx, const NdArrayRef& msg,
                        size_t rank_i, size_t rank_j, std::string_view tag);

class P2A : public UnaryKernel {
 public:
  static constexpr const char* kBindName() { return "p2a"; }

  ce::CExpr latency() const override { return ce::Const(0); }

  ce::CExpr comm() const override { return ce::Const(0); }

  Kind kind() const override { return Kind::Dynamic; }

  NdArrayRef proc(KernelEvalContext* ctx, const NdArrayRef& in) const override;
};

class A2P : public UnaryKernel {
 public:
  static constexpr const char* kBindName() { return "a2p"; }

  ce::CExpr latency() const override { return ce::Const(9); }

  ce::CExpr comm() const override {
    // 3 * jmp
    return ce::K() * 3;
  }

  Kind kind() const override { return Kind::Dynamic; }

  NdArrayRef proc(KernelEvalContext* ctx, const NdArrayRef& in) const override;
};

class A2V : public RevealToKernel {
 public:
  static constexpr const char* kBindName() { return "a2v"; }

  ce::CExpr latency() const override { return ce::Const(3); }

  ce::CExpr comm() const override { return ce::K(); }

  Kind kind() const override { return Kind::Dynamic; }

  NdArrayRef proc(KernelEvalContext* ctx, const NdArrayRef& in,
                  size_t rank_dst) const override;
};

class V2A : public UnaryKernel {
 public:
  static constexpr const char* kBindName() { return "v2a"; }

  ce::CExpr latency() const override { return ce::Const(3); }

  ce::CExpr comm() const override { return ce::K(); }

  Kind kind() const override { return Kind::Dynamic; }

  NdArrayRef proc(KernelEvalContext* ctx, const NdArrayRef& in) const override;
};

class NegateA : public UnaryKernel {
 public:
  static constexpr const char* kBindName() { return "negate_a"; }

  ce::CExpr latency() const override { return ce::Const(0); }

  ce::CExpr comm() const override { return ce::Const(0); }

  NdArrayRef proc(KernelEvalContext* ctx, const NdArrayRef& in) const override;
};

class RandA : public RandKernel {
 public:
  static constexpr const char* kBindName() { return "rand_a"; }

  ce::CExpr latency() const override { return ce::Const(3); }

  ce::CExpr comm() const override { return ce::K(); }

  Kind kind() const override { return Kind::Dynamic; }

  NdArrayRef proc(KernelEvalContext* ctx, const Shape& shape) const override;
};

////////////////////////////////////////////////////////////////////
// add family
////////////////////////////////////////////////////////////////////
class AddAP : public BinaryKernel {
 public:
  static constexpr const char* kBindName() { return "add_ap"; }

  ce::CExpr latency() const override { return ce::Const(0); }

  ce::CExpr comm() const override { return ce::Const(0); }

  NdArrayRef proc(KernelEvalContext* ctx, const NdArrayRef& lhs,
                  const NdArrayRef& rhs) const override;
};

class AddAA : public BinaryKernel {
 public:
  static constexpr const char* kBindName() { return "add_aa"; }

  ce::CExpr latency() const override { return ce::Const(0); }

  ce::CExpr comm() const override { return ce::Const(0); }

  NdArrayRef proc(KernelEvalContext* ctx, const NdArrayRef& lhs,
                  const NdArrayRef& rhs) const override;
};

////////////////////////////////////////////////////////////////////
// multiply family
////////////////////////////////////////////////////////////////////
class MulAP : public BinaryKernel {
 public:
  static constexpr const char* kBindName() { return "mul_ap"; }

  ce::CExpr latency() const override { return ce::Const(0); }

  ce::CExpr comm() const override { return ce::Const(0); }

  NdArrayRef proc(KernelEvalContext* ctx, const NdArrayRef& lhs,
                  const NdArrayRef& rhs) const override;
};

class MulAA : public BinaryKernel {
 public:
  static constexpr const char* kBindName() { return "mul_aa"; }

  ce::CExpr latency() const override { return ce::Const(13); }

  ce::CExpr comm() const override {
    // MulPre + 3 * jmp
    return ce::K() * 10;
  }

  // The comm is incorrect for FM128, since SWIFT only support FM32 and FM64
  Kind kind() const override { return Kind::Dynamic; }

  NdArrayRef proc(KernelEvalContext* ctx, const NdArrayRef& lhs,
                  const NdArrayRef& rhs) const override;
};

////////////////////////////////////////////////////////////////////
// matmul family
////////////////////////////////////////////////////////////////////
class MatMulAP : public MatmulKernel {
 public:
  static constexpr const char* kBindName() { return "mmul_ap"; }

  ce::CExpr latency() const override { return ce::Const(0); }

  ce::CExpr comm() const override { return ce::Const(0); }

  NdArrayRef proc(KernelEvalContext* ctx, const NdArrayRef& x,
                  const NdArrayRef& y) const override;
};

class MatMulAA : public MatmulKernel {
 public:
  static constexpr const char* kBindName() { return "mmul_aa"; }

  ce::CExpr latency() const override { return ce::Const(13); }

  ce::CExpr comm() const override {
    auto m = ce::Variable("m", "rows of lhs");
    auto n = ce::Variable("n", "cols of rhs");
    auto k = ce::Variable("k", "cols of lhs");
    return ce::K() * (m * n * 8 + m * k * 2);
  }

  Kind kind() const override { return Kind::Dynamic; }

  NdArrayRef proc(KernelEvalContext* ctx, const NdArrayRef& x,
                  const NdArrayRef& y) const override;
};

class LShiftA : public ShiftKernel {
 public:
  static constexpr const char* kBindName() { return "lshift_a"; }

  ce::CExpr latency() const override { return ce::Const(0); }

  ce::CExpr comm() const override { return ce::Const(0); }

  NdArrayRef proc(KernelEvalContext* ctx, const NdArrayRef& in,
                  const Sizes& bits) const override;
};

class TruncA : public TruncAKernel {
 public:
  static constexpr const char* kBindName() { return "trunc_a"; }

  ce::CExpr latency() const override {
    // only count online for now
    return ce::Const(9);
  }

  ce::CExpr comm() const override {
    // offline: MatMulAA * 2
    // dimension of the MatMulAA are:
    // {numel, k - bits, numel} and {numel, k, numel}
    // online: A2P
    // only count online for now
    return ce::K() * 3;
  }

  Kind kind() const override { return Kind::Dynamic; }

  NdArrayRef proc(KernelEvalContext* ctx, const NdArrayRef& in, size_t bits,
                  SignType sign) const override;

  bool hasMsbError() const override { return true; }

  TruncLsbRounding lsbRounding() const override {
    return TruncLsbRounding::Random;
  }

  std::pair<NdArrayRef, NdArrayRef> Trgen(KernelEvalContext* ctx, int64_t bits,
                                          FieldType field, int64_t numel) const;
};

}  // namespace spu::mpc::swift
