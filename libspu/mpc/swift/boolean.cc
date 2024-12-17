// Copyright 2021 Ant Group Co., Ltd.
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

#include "libspu/mpc/swift/boolean.h"
#include "libspu/mpc/swift/arithmetic.h"

#include <functional>

#include "libspu/core/bit_utils.h"
#include "libspu/mpc/common/communicator.h"
#include "libspu/mpc/common/prg_state.h"
#include "libspu/mpc/common/pv2k.h"
#include "libspu/mpc/swift/type.h"
#include "libspu/mpc/swift/value.h"
#include "libspu/mpc/utils/ring_ops.h"

namespace spu::mpc::swift{
namespace{

size_t getNumBits(const NdArrayRef& in) {
  if (in.eltype().isa<Pub2kTy>()) {
    const auto field = in.eltype().as<Pub2kTy>()->field();
    return DISPATCH_ALL_FIELDS(field,
                               [&]() { return maxBitWidth<ring2k_t>(in); });
  } else if (in.eltype().isa<BShrTy>()) {
    return in.eltype().as<BShrTy>()->nbits();
  } else {
    SPU_THROW("should not be here, {}", in.eltype());
  }
}
}

NdArrayRef B2P::proc(KernelEvalContext* ctx, const NdArrayRef& in) const {
	const auto field = in.eltype().as<Ring2k>()->field();
	auto* comm = ctx->getState<Communicator>();
	auto rank = comm->getRank();
	auto ty = makeType<RingTy>(field);
	auto jmp = Jmp();

	NdArrayRef out(makeType<Pub2kTy>(field), in.shape());

	NdArrayRef alpha1(ty, in.shape());
	NdArrayRef alpha2(ty, in.shape());
	NdArrayRef beta(ty, in.shape());

	if (rank == 0) {
		alpha1 = getFirstShare(in);
		alpha2 = getSecondShare(in);
	}
	if (rank == 1){
		alpha1 = getFirstShare(in);
		beta = getSecondShare(in);
	}
	if(rank == 2){
		alpha2 = getFirstShare(in);
		beta = getSecondShare(in);
	}

	// P1, P2 -> P0 : beta
	// P0, P1 -> P2 : alpha1
	// P2, P0 -> P1 : alpha2
	beta = jmp.proc(ctx, beta, 1, 2, 0, "beta");
	alpha1 = jmp.proc(ctx, alpha1, 0, 1, 2, "alpha1");
	alpha2 = jmp.proc(ctx, alpha2, 2, 0, 1, "alpha2");

	out = ring_xor(ring_xor(beta, alpha1), alpha2);

	return out.as(makeType<Pub2kTy>(field));
}

NdArrayRef P2B::proc(KernelEvalContext* ctx, const NdArrayRef& in) const {
	const auto field = in.eltype().as<RingTy>()->field();
	auto* comm = ctx->getState<Communicator>();

	auto rank = comm->getRank();

	return DISPATCH_ALL_FIELDS(field, [&]() {
		using shr_el_t = ring2k_t;
		using shr_t = std::array<shr_el_t, 3>;
		using pshr_el_t = ring2k_t;

		NdArrayRef out(makeType<BShrTy>(field), in.shape());
		NdArrayView<shr_t> _out(out);
		NdArrayView<pshr_el_t> _in(in);

		// 0, 0, v
		// 0, v, 0
		// 0, v, 0

		pforeach(0, in.numel(), [&](int64_t idx) {
			_out[idx][0] = ring2k_t(0);
			_out[idx][1] = rank == 0 ? ring2k_t(0) : _in[idx];
			_out[idx][2] = rank == 0 ? _in[idx] : ring2k_t(0);
		});

		return out;
	});
}

  NdArrayRef XorBP::proc(KernelEvalContext* ctx, const NdArrayRef& lhs,
    const NdArrayRef& rhs) const {
    auto* comm = ctx->getState<Communicator>();
    const auto* lhs_ty = lhs.eltype().as<BShrTy>();

    const size_t out_nbits = std::min(getNumBits(lhs), getNumBits(rhs));
    const auto field = lhs_ty->field();

    auto rank = comm->getRank();

    return DISPATCH_ALL_FIELDS(field, [&]() {
      using el_t = ring2k_t;
      using ashr_t = std::array<el_t, 3>;

      NdArrayRef out(makeType<BShrTy>(field, out_nbits), lhs.shape());
      NdArrayView<ashr_t> _out(out);
      NdArrayView<ashr_t> _lhs(lhs);
      NdArrayView<el_t> _rhs(rhs);

      pforeach(0, lhs.numel(), [&](int64_t idx) {
        _out[idx][0] = _lhs[idx][0];
        _out[idx][1] = _lhs[idx][1];
        _out[idx][2] = _lhs[idx][2];
        if (rank == 0) _out[idx][2] ^= _rhs[idx];
        if (rank == 1 || rank == 2) _out[idx][1] ^= _rhs[idx];
        });
      return out.as(makeType<BShrTy>(field, out_nbits));
      });
  }

  NdArrayRef XorBB::proc(KernelEvalContext* ctx, const NdArrayRef& lhs,
    const NdArrayRef& rhs) const {
    const auto* lhs_ty = lhs.eltype().as<BShrTy>();

    const auto field = lhs_ty->field();

		const size_t out_nbits = std::min(getNumBits(lhs), getNumBits(rhs));

    return DISPATCH_ALL_FIELDS(field, [&]() {
      using shr_t = std::array<ring2k_t, 3>;

      NdArrayRef out(makeType<BShrTy>(field, out_nbits), lhs.shape());
      NdArrayView<shr_t> _out(out);
      NdArrayView<shr_t> _lhs(lhs);
      NdArrayView<shr_t> _rhs(rhs);

      pforeach(0, lhs.numel(), [&](int64_t idx) {
        _out[idx][0] = _lhs[idx][0] ^ _rhs[idx][0];
        _out[idx][1] = _lhs[idx][1] ^ _rhs[idx][1];
        _out[idx][2] = _lhs[idx][2] ^ _rhs[idx][2];
        });
      return out.as(makeType<BShrTy>(field, out_nbits));
      });
  }

  NdArrayRef AndBP::proc(KernelEvalContext*, const NdArrayRef& lhs,
    const NdArrayRef& rhs) const {
    const auto* lhs_ty = lhs.eltype().as<BShrTy>();
    const auto field = lhs_ty->field();
		const size_t out_nbits = std::min(getNumBits(lhs), getNumBits(rhs));

    return DISPATCH_ALL_FIELDS(field, [&]() {
      using el_t = ring2k_t;
      using shr_t = std::array<el_t, 3>;

      NdArrayRef out(makeType<BShrTy>(field, out_nbits), lhs.shape());
      NdArrayView<shr_t> _out(out);
      NdArrayView<shr_t> _lhs(lhs);
      NdArrayView<el_t> _rhs(rhs);

      pforeach(0, lhs.numel(), [&](int64_t idx) {
        _out[idx][0] = _lhs[idx][0] & _rhs[idx];
        _out[idx][1] = _lhs[idx][1] & _rhs[idx];
        _out[idx][2] = _lhs[idx][2] & _rhs[idx];
        });
      return out.as(makeType<BShrTy>(field, out_nbits));
      });
  }

  NdArrayRef AndPre_semi(KernelEvalContext* ctx, const NdArrayRef& lhs,
    const NdArrayRef& rhs) {
    // semi-honest mult based on RSS
    // store the shares like RSS
    // P0 : x0  x1  dummy
    // P1 : x1  x2  dummy
    // P2 : x2  x0  dummy
    const auto field = lhs.eltype().as<Ring2k>()->field();
    auto* comm = ctx->getState<Communicator>();
    auto* prg_state = ctx->getState<PrgState>();

		const size_t out_nbits = std::min(getNumBits(lhs), getNumBits(rhs));

    return DISPATCH_ALL_FIELDS(field, [&]() {
      using el_t = ring2k_t;
      using shr_t = std::array<el_t, 3>;

      std::vector<el_t> r0(lhs.numel());
      std::vector<el_t> r1(lhs.numel());

      prg_state->fillPrssPair(r0.data(), r1.data(), r0.size(),
        PrgState::GenPrssCtrl::Both);

      NdArrayView<shr_t> _lhs(lhs);
      NdArrayView<shr_t> _rhs(rhs);

      // z1 = (x1 & y1) ^ (x1 & y2) ^ (x2 & y1) ^ (r0 ^ r1);
      pforeach(0, lhs.numel(), [&](int64_t idx) {
        r0[idx] = (_lhs[idx][0] & _rhs[idx][0]) ^ (_lhs[idx][0] & _rhs[idx][1]) ^
          (_lhs[idx][1] & _rhs[idx][0]) ^ (r0[idx] ^ r1[idx]);
        });

      r1 = comm->rotate<el_t>(r0, "andpre");  // comm => 1, k

      NdArrayRef out(makeType<BShrTy>(field, out_nbits), lhs.shape());
      NdArrayView<shr_t> _out(out);

      pforeach(0, lhs.numel(), [&](int64_t idx) {
        _out[idx][0] = r0[idx];
        _out[idx][1] = r1[idx];
        });

      return out.as(makeType<BShrTy>(field, out_nbits));
      });
  }

  NdArrayRef AndBB::proc(KernelEvalContext* ctx, const NdArrayRef& lhs,
    const NdArrayRef& rhs) const {
    const auto field = lhs.eltype().as<RingTy>()->field();
    auto* comm = ctx->getState<Communicator>();
    auto* prg_state = ctx->getState<PrgState>();
		const size_t out_nbits = std::min(getNumBits(lhs), getNumBits(rhs));
    auto rank = comm->getRank();
    auto ty = makeType<RingTy>(field);
    auto shape = lhs.shape();
    auto numel = lhs.numel();
    auto jmp = Jmp();

    NdArrayRef alpha_z1(ty, shape);
    NdArrayRef alpha_z2(ty, shape);
    NdArrayRef gamma_z(ty, shape);
    NdArrayRef out(makeType<BShrTy>(field, out_nbits), shape);
    NdArrayRef d(makeType<BShrTy>(field, out_nbits), shape);
    NdArrayRef e(makeType<BShrTy>(field, out_nbits), shape);

    NdArrayRef chi_1(ty, shape);
    NdArrayRef chi_2(ty, shape);
    NdArrayRef Phi(ty, shape);

    NdArrayRef beta_z1_start(ty, shape);
    NdArrayRef beta_z2_start(ty, shape);

    NdArrayRef beta_plus_gamma_z(ty, shape);

    // P0, Pj together sample random alpha_j
    auto [r0, r1] =
      prg_state->genPrssPair(field, lhs.shape(), PrgState::GenPrssCtrl::Both);
    if (rank == 0) {
      alpha_z2 = r0;
      alpha_z1 = r1;
    }
    if (rank == 1) {
      alpha_z1 = r0;
      gamma_z = r1;
    }
    if (rank == 2) {
      alpha_z2 = r1;
      gamma_z = r0;
    }

    return DISPATCH_ALL_FIELDS(field, [&]() {
      using el_t = ring2k_t;
      using shr_t = std::array<el_t, 3>;

      NdArrayView<shr_t> _d(d);
      NdArrayView<shr_t> _e(e);
      NdArrayView<shr_t> _out(out);
      NdArrayView<shr_t> _lhs(lhs);
      NdArrayView<shr_t> _rhs(rhs);
      NdArrayView<el_t> _alpha_z1(alpha_z1);
      NdArrayView<el_t> _alpha_z2(alpha_z2);
      NdArrayView<el_t> _gamma_z(gamma_z);

      // generate RSS of e, d
      // refer to Table 3 in Swift
      // and init out
      if (rank == 0) {
        pforeach(0, numel, [&](int64_t idx) {
          _d[idx][0] = _lhs[idx][1];
          _d[idx][1] = _lhs[idx][0];
          _e[idx][0] = _rhs[idx][1];
          _e[idx][1] = _rhs[idx][0];

          _out[idx][0] = _alpha_z1[idx];
          _out[idx][1] = _alpha_z2[idx];
          });
      }
      else if (rank == 1) {
        pforeach(0, numel, [&](int64_t idx) {
          _d[idx][0] = _lhs[idx][0];
          _d[idx][1] = _lhs[idx][2];
          _e[idx][0] = _rhs[idx][0];
          _e[idx][1] = _rhs[idx][2];

          _out[idx][0] = _alpha_z1[idx];
          _out[idx][2] = _gamma_z[idx];
          });
      }
      else if (rank == 2) {
        pforeach(0, numel, [&](int64_t idx) {
          _d[idx][0] = _lhs[idx][2];
          _d[idx][1] = _lhs[idx][0];
          _e[idx][0] = _rhs[idx][2];
          _e[idx][1] = _rhs[idx][0];

          _out[idx][0] = _alpha_z2[idx];
          _out[idx][2] = _gamma_z[idx];
          });
      }

      // p0, p1 : chi_1 = f1
      // p0, p2 : chi_2 = f0
      // p1, p2 : Phi = f2 ^ gamma_x & gamma_y
      auto f = AndPre_semi(ctx, d, e);

      NdArrayView<shr_t> _f(f);
      NdArrayView<el_t> _chi_1(chi_1);
      NdArrayView<el_t> _chi_2(chi_2);
      NdArrayView<el_t> _Phi(Phi);
      if (rank == 0) {
        pforeach(0, numel, [&](int64_t idx) {
          _chi_1[idx] = _f[idx][1];
          _chi_2[idx] = _f[idx][0];
          });
      }
      else if (rank == 1) {
        pforeach(0, numel, [&](int64_t idx) {
          _chi_1[idx] = _f[idx][0];
          _Phi[idx] = _f[idx][1] ^ (_lhs[idx][2] & _rhs[idx][2]);
          });
      }
      else if (rank == 2) {
        pforeach(0, numel, [&](int64_t idx) {
          _chi_2[idx] = _f[idx][1];
          _Phi[idx] = _f[idx][0] ^ (_lhs[idx][2] & _rhs[idx][2]);
          });
      }

      NdArrayView<el_t> _beta_z1_start(beta_z1_start);
      NdArrayView<el_t> _beta_z2_start(beta_z2_start);
      // [beta*_z] = -(beta_x + gamma_x)[alpha_y] - (beta_y + gamma_y)[alpha_x]
      //             +[alpha_z] + [chi]
      if (rank == 0) {
        pforeach(0, numel, [&](int64_t idx) {
          _beta_z1_start[idx] = (_lhs[idx][2] & _rhs[idx][0]) ^
            (_rhs[idx][2] & _lhs[idx][0]) ^ _alpha_z1[idx] ^
            _chi_1[idx];
          _beta_z2_start[idx] =( _lhs[idx][2] & _rhs[idx][1]) ^
            (_rhs[idx][2] & _lhs[idx][1]) ^ _alpha_z2[idx] ^
            _chi_2[idx];
          });
      }
      else if (rank == 1) {
        pforeach(0, numel, [&](int64_t idx) {
          _beta_z1_start[idx] = ((_lhs[idx][1] ^ _lhs[idx][2]) & _rhs[idx][0]) ^
            ((_rhs[idx][1] ^ _rhs[idx][2]) & _lhs[idx][0]) ^
            _alpha_z1[idx] ^ _chi_1[idx];
          });
      }
      else if (rank == 2) {
        pforeach(0, numel, [&](int64_t idx) {
          _beta_z2_start[idx] = ((_lhs[idx][1] ^ _lhs[idx][2]) & _rhs[idx][0]) ^
            ((_rhs[idx][1] ^ _rhs[idx][2]) & _lhs[idx][0]) ^
            _alpha_z2[idx] ^ _chi_2[idx];
          });
      }

      beta_z1_start = jmp.proc(ctx, beta_z1_start, 0, 1, 2, "beta_z1_start");
      beta_z2_start = jmp.proc(ctx, beta_z2_start, 0, 2, 1, "beta_z2_start");
      auto beta_z_start = ring_xor(beta_z1_start, beta_z2_start);

      NdArrayView<el_t> _beta_z_start(beta_z_start);
      NdArrayView<el_t> _beta_plus_gamma_z(beta_plus_gamma_z);
      if (rank == 1 || rank == 2) {
        pforeach(0, numel, [&](int64_t idx) {
          // beta_z = beta*_z + beta_x * beta_y + Phi
          _out[idx][1] =
            _beta_z_start[idx] ^ (_lhs[idx][1] & _rhs[idx][1]) ^ _Phi[idx];

          _beta_plus_gamma_z[idx] = _out[idx][1] ^ _out[idx][2];
          });
      }
      beta_plus_gamma_z =
        jmp.proc(ctx, beta_plus_gamma_z, 1, 2, 0, "beta_plus_gamma_z");
      if (rank == 0) {
        pforeach(0, numel,
          [&](int64_t idx) { _out[idx][2] = _beta_plus_gamma_z[idx]; });
      }

      return out.as(makeType<BShrTy>(field, out_nbits));
      });
  }

	NdArrayRef LShiftB::proc(KernelEvalContext*, const NdArrayRef& in,
													const Sizes& shift) const {
		const auto field = in.eltype().as<Ring2k>()->field();

		size_t out_nbits = in.eltype().as<BShare>()->nbits() +
											*std::max_element(shift.begin(), shift.end());
		out_nbits = std::clamp(out_nbits, static_cast<size_t>(0), SizeOf(field) * 8);

		NdArrayRef out(makeType<BShrTy>(field, out_nbits), in.shape());

		auto in1 = getFirstShare(in);
		auto in2 = getSecondShare(in);
		auto in3 = getThirdShare(in);
		
		auto out1 = getFirstShare(out);
		auto out2 = getSecondShare(out);
		auto out3 = getThirdShare(out);
		
		ring_assign(out1, ring_lshift(in1, shift));
		ring_assign(out2, ring_lshift(in2, shift));
		ring_assign(out3, ring_lshift(in3, shift));

		return out.as(makeType<BShrTy>(field, out_nbits));
	}

	NdArrayRef RShiftB::proc(KernelEvalContext*, const NdArrayRef& in,
													const Sizes& shift) const {
		const auto field = in.eltype().as<Ring2k>()->field();

		int64_t nbits = in.eltype().as<BShare>()->nbits();
		int64_t out_nbits =
				nbits - std::min(nbits, *std::min_element(shift.begin(), shift.end()));
		SPU_ENFORCE(nbits <= static_cast<int64_t>(SizeOf(field) * 8));

		NdArrayRef out(makeType<BShrTy>(field, out_nbits), in.shape());
		
		auto in1 = getFirstShare(in);
		auto in2 = getSecondShare(in);
		auto in3 = getThirdShare(in);
		
		auto out1 = getFirstShare(out);
		auto out2 = getSecondShare(out);
		auto out3 = getThirdShare(out);
		
		ring_assign(out1, ring_rshift(in1, shift));
		ring_assign(out2, ring_rshift(in2, shift));
		ring_assign(out3, ring_rshift(in3, shift));

		return out.as(makeType<BShrTy>(field, out_nbits));
	}

	NdArrayRef ARShiftB::proc(KernelEvalContext*, const NdArrayRef& in,
														const Sizes& shift) const {
		const auto field = in.eltype().as<Ring2k>()->field();

		// arithmetic right shift expects to work on ring, or the behaviour is
		// undefined.

		NdArrayRef out(makeType<BShrTy>(field, SizeOf(field) * 8), in.shape());
		
		auto in1 = getFirstShare(in);
		auto in2 = getSecondShare(in);
		auto in3 = getThirdShare(in);
		
		auto out1 = getFirstShare(out);
		auto out2 = getSecondShare(out);
		auto out3 = getThirdShare(out);
		
		ring_assign(out1, ring_arshift(in1, shift));
		ring_assign(out2, ring_arshift(in2, shift));
		ring_assign(out3, ring_arshift(in3, shift));

		return out.as(makeType<BShrTy>(field, SizeOf(field) * 8));
	}

	NdArrayRef BitrevB::proc(KernelEvalContext*, const NdArrayRef& in, size_t start,
													size_t end) const {
		const auto field = in.eltype().as<Ring2k>()->field();

		SPU_ENFORCE(start <= end);
		SPU_ENFORCE(end <= SizeOf(field) * 8);
		const size_t out_nbits = std::max(getNumBits(in), end);

		NdArrayRef out(makeType<BShrTy>(field, out_nbits), in.shape());
		
		auto in1 = getFirstShare(in);
		auto in2 = getSecondShare(in);
		auto in3 = getThirdShare(in);
		
		auto out1 = getFirstShare(out);
		auto out2 = getSecondShare(out);
		auto out3 = getThirdShare(out);
		
		ring_assign(out1, ring_bitrev(in1, start, end));
		ring_assign(out2, ring_bitrev(in2, start, end));
		ring_assign(out3, ring_bitrev(in3, start, end));

		return out.as(makeType<BShrTy>(field, out_nbits));
	}

	NdArrayRef BitIntlB::proc(KernelEvalContext*, const NdArrayRef& in,
														size_t stride) const {
		const auto field = in.eltype().as<Ring2k>()->field();
		const auto nbits = getNumBits(in);
		SPU_ENFORCE(absl::has_single_bit(nbits));

		NdArrayRef out(in.eltype(), in.shape());
		auto numel = in.numel();

		DISPATCH_ALL_FIELDS(field, [&]() {
			using el_t = ring2k_t;
			using shr_t = std::array<el_t, 3>;
			NdArrayView<shr_t> _in(in);
			NdArrayView<shr_t> _out(out);

			pforeach(0, numel, [&](int64_t idx) {
				_out[idx][0] = BitIntl<ring2k_t>(_in[idx][0], stride, nbits);
				_out[idx][1] = BitIntl<ring2k_t>(_in[idx][1], stride, nbits);
				_out[idx][2] = BitIntl<ring2k_t>(_in[idx][2], stride, nbits);
			});
		});

		return out.as(in.eltype());
	}

	NdArrayRef BitDeintlB::proc(KernelEvalContext*, const NdArrayRef& in,
														size_t stride) const {
		const auto field = in.eltype().as<Ring2k>()->field();
		const auto nbits = getNumBits(in);
		SPU_ENFORCE(absl::has_single_bit(nbits));

		NdArrayRef out(in.eltype(), in.shape());
		auto numel = in.numel();

		DISPATCH_ALL_FIELDS(field, [&]() {
			using el_t = ring2k_t;
			using shr_t = std::array<el_t, 3>;
			NdArrayView<shr_t> _in(in);
			NdArrayView<shr_t> _out(out);

			pforeach(0, numel, [&](int64_t idx) {
				_out[idx][0] = BitDeintl<ring2k_t>(_in[idx][0], stride, nbits);
				_out[idx][1] = BitDeintl<ring2k_t>(_in[idx][1], stride, nbits);
				_out[idx][2] = BitDeintl<ring2k_t>(_in[idx][2], stride, nbits);
			});
		});

		return out.as(in.eltype());
	}


} // namespace spu::mpc::swift