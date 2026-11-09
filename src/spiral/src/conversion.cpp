#include "client.h"
namespace spiral_u64_backend {
static void special_distribute(MatPoly &out, const MatPoly &a) {
    // a: m_conv x 1 nttd
    // out: 2*m_conv x 2, nttd
    for (size_t i = 0; i < m_conv; i++) {
        for (size_t nz = 0; nz < crt_count * coeff_count; nz++) {
            uint64_t val = a.data[i * (crt_count * coeff_count) + nz];
            size_t r = i * 2;
            size_t c = 0;
            out.data[(r * 2 + c) * crt_count * coeff_count + nz] = val;
            r = i*2 + 1;
            c = 1;
            out.data[(r * 2 + c) * crt_count * coeff_count + nz] = val;
        }
    }
}

void scalToMat(
    size_t m_conv,
    MatPoly &out_reg,               // n1 x n0
    const MatPoly &cv,              // n0 x 1
    const MatPoly &W,                // n1 x (n0 * m_conv)
    MatPoly &cv_0,
    MatPoly &cv_1,
    MatPoly &cv_ntti,
    MatPoly &square_cv,
    MatPoly &ginv_c,
    MatPoly &ginv_c_nttd,
    MatPoly &prod_W_ginv,
    MatPoly &padded_cv_1,
    MatPoly &ginv_c_raw,
    MatPoly &ginv_c_raw_nttd
) {
    // assert(out_reg.isNTT);
    // assert(cv.isNTT);
    // assert(W.isNTT);
    // assert(cv.rows == n0);
    // assert(cv.cols == 1);
    // assert(W.rows == n1);
    // assert(W.cols == n0 * m_conv);

    pick(cv_0, cv, 0, 0);
    pick(cv_1, cv, 1, 0);
    from_ntt(cv_ntti, cv_0);
    gadget_invert(m_conv, ginv_c_raw, cv_ntti, 1);
    to_ntt_no_reduce(ginv_c_raw_nttd, ginv_c_raw);
    special_distribute(ginv_c_nttd, ginv_c_raw_nttd);
    multiply(prod_W_ginv, W, ginv_c_nttd);
    place(padded_cv_1, cv_1, 1, 0);
    place(padded_cv_1, cv_1, 2, 1);

    add(out_reg, prod_W_ginv, padded_cv_1);
}

void scalToMatFast(
    size_t m_conv,
    MatPoly &out_reg,               // n1 x n0
    const MatPoly &cv,              // n0 x 1
    const MatPoly &W,                // n1 x (n0 * m_conv)
    MatPoly &cv_0,
    MatPoly &cv_1,
    MatPoly &ginv_c,
    MatPoly &ginv_c_nttd,
    MatPoly &prod_W_ginv,
    MatPoly &padded_cv_1,
    const MatPoly &ginv_c_raw_nttd
) {
    // assert(out_reg.isNTT);
    // assert(cv.isNTT);
    // assert(W.isNTT);
    // assert(cv.rows == n0);
    // assert(cv.cols == 1);
    // assert(W.rows == n1);
    // assert(W.cols == n0 * m_conv);

    pick(cv_0, cv, 0, 0);
    pick(cv_1, cv, 1, 0);
    special_distribute(ginv_c_nttd, ginv_c_raw_nttd);
    multiply(prod_W_ginv, W, ginv_c_nttd);
    place(padded_cv_1, cv_1, 1, 0);
    place(padded_cv_1, cv_1, 2, 1);

    add(out_reg, prod_W_ginv, padded_cv_1);
}

void scalToMat(
    size_t m_conv,
    MatPoly &out_reg,               // n1 x n0
    const MatPoly &cv,              // n0 x 1
    const MatPoly &W                // n1 x (n0 * m_conv)
) {
    MatPoly cv_0(1, 1);
    MatPoly cv_1(1, 1);
    MatPoly cv_ntti(1, 1, false);
    MatPoly square_cv(n0, n0, false);
    MatPoly ginv_c(n0 * m_conv, n0, false);
    MatPoly ginv_c_nttd(n0 * m_conv, n0);
    MatPoly prod_W_ginv(n1, n0);
    MatPoly padded_cv_1(n1, n0);
    MatPoly ginv_c_raw(m_conv, 1, false);
    MatPoly ginv_c_raw_nttd(m_conv, 1);

    scalToMat(
        m_conv,
        out_reg,
        cv,
        W,
        cv_0,
        cv_1,
        cv_ntti,
        square_cv,
        ginv_c,
        ginv_c_nttd,
        prod_W_ginv,
        padded_cv_1,
        ginv_c_raw,
        ginv_c_raw_nttd
    );
}

void scalToMatFast(
    size_t m_conv,
    MatPoly &out_reg,               // n1 x n0
    const MatPoly &cv,              // n0 x 1
    const MatPoly &ginv_c_raw_nttd, // m_conv x 1
    const MatPoly &W                // n1 x (n0 * m_conv)
) {
    MatPoly cv_0(1, 1);
    MatPoly cv_1(1, 1);
    MatPoly cv_ntti(1, 1, false);
    MatPoly square_cv(n0, n0, false);
    MatPoly ginv_c(n0 * m_conv, n0, false);
    MatPoly ginv_c_nttd(n0 * m_conv, n0);
    MatPoly prod_W_ginv(n1, n0);
    MatPoly padded_cv_1(n1, n0);
    MatPoly ginv_c_raw(m_conv, 1, false);

    scalToMatFast(
        m_conv,
        out_reg,
        cv,
        W,
        cv_0,
        cv_1,
        ginv_c,
        ginv_c_nttd,
        prod_W_ginv,
        padded_cv_1,
        ginv_c_raw_nttd
    );
}

void regevToGSW(
    size_t m_conv, size_t t, MatPoly &out,
    const vector<MatPoly> &cv_v, size_t cv_v_offset, const MatPoly &W, const MatPoly &V
) {
    MatPoly cv_ntti(2, 1, false);
    MatPoly cv0_ntti(1, 1, false);
    MatPoly cv1_ntti(1, 1, false);
    MatPoly cv(m_conv, 1);
    MatPoly scalToMatResult(n1, n0);
    MatPoly ginv_tmp(m_conv, 1, false);
    MatPoly ginv_Chat_nttd(2 * m_conv, t);
    MatPoly prod(n1, t);
    MatPoly result(n1, n1 * t);
    MatPoly result_permuted(n1, n1 * t);
    for (size_t i = 0; i < t; i++) {
        from_ntt(cv_ntti, cv_v[cv_v_offset + i]);
        pick(cv0_ntti, cv_ntti, 0, 0);
        pick(cv1_ntti, cv_ntti, 1, 0);
        gadget_invert(m_conv, ginv_tmp, cv0_ntti, 1);
        to_ntt_no_reduce(cv, ginv_tmp);
        place(ginv_Chat_nttd, cv, 0, i);

        scalToMatFast(m_conv, scalToMatResult, cv_v[cv_v_offset + i], cv, W);
        place(result, scalToMatResult, 0, t + (n0 * i));

        gadget_invert(m_conv, ginv_tmp, cv1_ntti, 1);
        to_ntt_no_reduce(cv, ginv_tmp);
        place(ginv_Chat_nttd, cv, m_conv, i);
        // scalToMat(m_conv, scalToMatResult, cv_v[cv_v_offset + i], W);
    }
    multiply(prod, V, ginv_Chat_nttd);
    place(result, prod, 0, 0);

    // permute
    for (size_t i = 0; i < t; i++) {
        cop(result_permuted, result, 0, i, 0, (n0+1)*i, n1, 1);
        cop(result_permuted, result, 0, t + n0*i, 0, (n0+1)*i + 1, n1, n0);
    }

    out = result_permuted;
}


}
