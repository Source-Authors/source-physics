// Copyright (C) Ipion Software GmbH 1999-2000. All rights reserved.

// IVP_EXPORT_PRIVATE

#include <ivp_physics.hxx>

#if defined(LINUX) || defined(SUN) || defined(__APPLE__) || (defined(__MWERKS__) && __POWERPC__)
#include <alloca.h>
#endif
#include <ivp_great_matrix.hxx>
#include <ivp_debug_manager.hxx>
#include <ivu_memory.hxx>

#include <algorithm>
#include <cstring>
#include <utility>

#define IVP_UNILATERAL_SOLVER_MAX_STEP 250
#define IVP_STATIC_FRICTION_TEST_EPS 0.00001f  // is a percentage value
#define IVP_NUMERIC_TEST_EVERY_N 7

// ---------------------------------------------------------------------------
// SIMD backend selection for IVP_VecFPU.
//
// DirectXMath is a float32-only library (XMVECTOR = 4 x float). It is used
// when IVP_DOUBLE is float. When IVP_DOUBLE is double, routing data through
// DirectXMath would silently truncate every value to float and break the LCP
// solver (SOLVER_EPS = 1e-7 is below float resolution), so the double build
// uses SSE2 packed doubles instead (the same instruction family DirectXMath
// itself is built on). Define IVP_VECFPU_NO_SIMD to force the scalar path.
// ---------------------------------------------------------------------------
#if !defined(IVP_VECFPU_NO_SIMD) && defined(IVP_NO_DOUBLE)
#include <DirectXMath.h>
#define IVP_VECFPU_BACKEND_DXMATH 1
#elif !defined(IVP_VECFPU_NO_SIMD) &&                             \
    (defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64) || \
     (defined(_M_IX86_FP) && _M_IX86_FP >= 2))
#include <emmintrin.h>
#define IVP_VECFPU_BACKEND_SSE2 1
#endif

namespace {

#if defined(IVP_VECFPU_BACKEND_DXMATH)

static_assert(sizeof(IVP_DOUBLE) == sizeof(float),
              "DirectXMath backend requires IVP_DOUBLE == float");

inline DirectX::XMVECTOR XM_CALLCONV load4(const IVP_DOUBLE* p) {
  return DirectX::XMLoadFloat4(reinterpret_cast<const DirectX::XMFLOAT4*>(p));
}

inline void XM_CALLCONV store4(IVP_DOUBLE* p, DirectX::FXMVECTOR v) {
  DirectX::XMStoreFloat4(reinterpret_cast<DirectX::XMFLOAT4*>(p), v);
}

// dst[i] += src[i] * f
inline void axpy(IVP_DOUBLE* dst, const IVP_DOUBLE* src, IVP_DOUBLE f, int n) {
  using namespace DirectX;
  const XMVECTOR vf = XMVectorReplicate(f);
  int i = 0;
  for (; i + 8 <= n; i += 8) {
    const XMVECTOR s0 = load4(src + i);
    const XMVECTOR s1 = load4(src + i + 4);
    const XMVECTOR t0 = load4(dst + i);
    const XMVECTOR t1 = load4(dst + i + 4);
    store4(dst + i, XMVectorMultiplyAdd(s0, vf, t0));
    store4(dst + i + 4, XMVectorMultiplyAdd(s1, vf, t1));
  }
  if (i + 4 <= n) {
    store4(dst + i, XMVectorMultiplyAdd(load4(src + i), vf, load4(dst + i)));
    i += 4;
  }
  for (; i < n; i++) {
    dst[i] += src[i] * f;
  }
}

// dst[i] *= f
inline void scale(IVP_DOUBLE* dst, IVP_DOUBLE f, int n) {
  using namespace DirectX;
  const XMVECTOR vf = XMVectorReplicate(f);
  int i = 0;
  for (; i + 8 <= n; i += 8) {
    const XMVECTOR t0 = load4(dst + i);
    const XMVECTOR t1 = load4(dst + i + 4);
    store4(dst + i, XMVectorMultiply(t0, vf));
    store4(dst + i + 4, XMVectorMultiply(t1, vf));
  }
  if (i + 4 <= n) {
    store4(dst + i, XMVectorMultiply(load4(dst + i), vf));
    i += 4;
  }
  for (; i < n; i++) {
    dst[i] *= f;
  }
}

inline void swap_rows(IVP_DOUBLE* a, IVP_DOUBLE* b, int n) {
  using namespace DirectX;
  int i = 0;
  for (; i + 4 <= n; i += 4) {
    const XMVECTOR va = load4(a + i);
    const XMVECTOR vb = load4(b + i);
    store4(a + i, vb);
    store4(b + i, va);
  }
  for (; i < n; i++) {
    std::swap(a[i], b[i]);
  }
}

inline IVP_DOUBLE dot(const IVP_DOUBLE* a, const IVP_DOUBLE* b, int n) {
  using namespace DirectX;
  XMVECTOR acc0 = XMVectorZero();
  XMVECTOR acc1 = XMVectorZero();
  int i = 0;
  for (; i + 8 <= n; i += 8) {
    acc0 = XMVectorMultiplyAdd(load4(a + i), load4(b + i), acc0);
    acc1 = XMVectorMultiplyAdd(load4(a + i + 4), load4(b + i + 4), acc1);
  }
  if (i + 4 <= n) {
    acc0 = XMVectorMultiplyAdd(load4(a + i), load4(b + i), acc0);
    i += 4;
  }
  acc0 = XMVectorAdd(acc0, acc1);
  IVP_DOUBLE sum = XMVectorGetX(XMVector4Dot(acc0, XMVectorSplatOne()));
  for (; i < n; i++) {
    sum += a[i] * b[i];
  }
  return sum;
}

#elif defined(IVP_VECFPU_BACKEND_SSE2)

static_assert(sizeof(IVP_DOUBLE) == sizeof(double),
              "SSE2 backend requires IVP_DOUBLE == double");

// dst[i] += src[i] * f  (mul then add, bit-identical to the scalar code)
inline void axpy(IVP_DOUBLE* dst, const IVP_DOUBLE* src, IVP_DOUBLE f, int n) {
  const __m128d vf = _mm_set1_pd(f);
  int i = 0;
  for (; i + 4 <= n; i += 4) {
    const __m128d s0 = _mm_loadu_pd(src + i);
    const __m128d s1 = _mm_loadu_pd(src + i + 2);
    const __m128d t0 = _mm_loadu_pd(dst + i);
    const __m128d t1 = _mm_loadu_pd(dst + i + 2);
    _mm_storeu_pd(dst + i, _mm_add_pd(_mm_mul_pd(s0, vf), t0));
    _mm_storeu_pd(dst + i + 2, _mm_add_pd(_mm_mul_pd(s1, vf), t1));
  }
  if (i + 2 <= n) {
    _mm_storeu_pd(dst + i, _mm_add_pd(_mm_mul_pd(_mm_loadu_pd(src + i), vf),
                                      _mm_loadu_pd(dst + i)));
    i += 2;
  }
  if (i < n) {
    dst[i] += src[i] * f;
  }
}

inline void scale(IVP_DOUBLE* dst, IVP_DOUBLE f, int n) {
  const __m128d vf = _mm_set1_pd(f);
  int i = 0;
  for (; i + 4 <= n; i += 4) {
    const __m128d t0 = _mm_loadu_pd(dst + i);
    const __m128d t1 = _mm_loadu_pd(dst + i + 2);
    _mm_storeu_pd(dst + i, _mm_mul_pd(t0, vf));
    _mm_storeu_pd(dst + i + 2, _mm_mul_pd(t1, vf));
  }
  if (i + 2 <= n) {
    _mm_storeu_pd(dst + i, _mm_mul_pd(_mm_loadu_pd(dst + i), vf));
    i += 2;
  }
  if (i < n) {
    dst[i] *= f;
  }
}

inline void swap_rows(IVP_DOUBLE* a, IVP_DOUBLE* b, int n) {
  int i = 0;
  for (; i + 2 <= n; i += 2) {
    const __m128d va = _mm_loadu_pd(a + i);
    const __m128d vb = _mm_loadu_pd(b + i);
    _mm_storeu_pd(a + i, vb);
    _mm_storeu_pd(b + i, va);
  }
  if (i < n) {
    std::swap(a[i], b[i]);
  }
}

inline IVP_DOUBLE dot(const IVP_DOUBLE* a, const IVP_DOUBLE* b, int n) {
  __m128d acc0 = _mm_setzero_pd();
  __m128d acc1 = _mm_setzero_pd();
  int i = 0;
  for (; i + 4 <= n; i += 4) {
    acc0 =
        _mm_add_pd(acc0, _mm_mul_pd(_mm_loadu_pd(a + i), _mm_loadu_pd(b + i)));
    acc1 = _mm_add_pd(
        acc1, _mm_mul_pd(_mm_loadu_pd(a + i + 2), _mm_loadu_pd(b + i + 2)));
  }
  if (i + 2 <= n) {
    acc0 =
        _mm_add_pd(acc0, _mm_mul_pd(_mm_loadu_pd(a + i), _mm_loadu_pd(b + i)));
    i += 2;
  }
  acc0 = _mm_add_pd(acc0, acc1);
  IVP_DOUBLE sum = _mm_cvtsd_f64(_mm_add_sd(acc0, _mm_unpackhi_pd(acc0, acc0)));
  if (i < n) {
    sum += a[i] * b[i];
  }
  return sum;
}

#else  // portable scalar fallback

inline void axpy(IVP_DOUBLE* dst, const IVP_DOUBLE* src, IVP_DOUBLE f, int n) {
  for (int i = 0; i < n; i++) {
    dst[i] += src[i] * f;
  }
}

inline void scale(IVP_DOUBLE* dst, IVP_DOUBLE f, int n) {
  for (int i = 0; i < n; i++) {
    dst[i] *= f;
  }
}

inline void swap_rows(IVP_DOUBLE* a, IVP_DOUBLE* b, int n) {
  for (int i = 0; i < n; i++) {
    std::swap(a[i], b[i]);
  }
}

inline IVP_DOUBLE dot(const IVP_DOUBLE* a, const IVP_DOUBLE* b, int n) {
  IVP_DOUBLE sum = 0.0f;
  for (int i = 0; i < n; i++) {
    sum += a[i] * b[i];
  }
  return sum;
}

#endif

}  // namespace

// dimhotepus: The 'adress_aligned' argument is ignored: every kernel processes
// exactly [0, size) with unaligned loads. The old code instead rounded the
// start address down / the size up to the vector block, i.e. it read and wrote
// elements outside the requested range (and with the PLATFORM_64BITS
// MEMSHIFT=2 setting it miscounted doubles and overran by one block).

inline void IVP_VecFPU::fpu_add_multiple_row(IVP_DOUBLE *target_adress,
                                             const IVP_DOUBLE *source_adress,
                                             IVP_DOUBLE factor, int size,
                                             IVP_BOOL) {
  axpy(target_adress, source_adress, factor, size);
}

inline IVP_DOUBLE IVP_VecFPU::fpu_large_dot_product(const IVP_DOUBLE *base_a,
                                                    const IVP_DOUBLE *base_b,
                                                    int size, IVP_BOOL) {
  return dot(base_a, base_b, size);
}

inline void IVP_VecFPU::fpu_multiply_row(IVP_DOUBLE *target_adress,
                                         IVP_DOUBLE factor, int size,
                                         IVP_BOOL) {
  scale(target_adress, factor, size);
}

inline void IVP_VecFPU::fpu_exchange_rows(IVP_DOUBLE *target_adress1,
                                          IVP_DOUBLE *target_adress2, int size,
                                          IVP_BOOL) {
  swap_rows(target_adress1, target_adress2, size);
}

inline void IVP_VecFPU::fpu_copy_rows(IVP_DOUBLE *target_adress,
                                      const IVP_DOUBLE *source_adress, int size,
                                      IVP_BOOL) {
  if (size > 0) {
    std::memcpy(target_adress, source_adress,
                size_t(size) * sizeof(IVP_DOUBLE));
  }
}

inline void IVP_VecFPU::fpu_set_row_to_zero(IVP_DOUBLE *target_adress, int size,
                                            IVP_BOOL) {
  if (size > 0) {
    std::fill_n(target_adress, size, IVP_DOUBLE(0));
  }
}

// tests a line to be greater equal result value
IVP_RETURN_TYPE IVP_Great_Matrix_Many_Zero::matrix_check_unequation_line(
    int linenum) {
  const IVP_DOUBLE *dline = &matrix_values[aligned_row_len * linenum];
  IVP_DOUBLE left_side = IVP_VecFPU::fpu_large_dot_product(dline, result_vector,
                                                           columns, IVP_TRUE);
  IVP_DOUBLE eps = IVP_Inline_Math::fabsd(desired_vector[linenum] *
                                          IVP_STATIC_FRICTION_TEST_EPS);
  if (left_side + eps >= desired_vector[linenum]) {
    return IVP_OK;
  } else {
    // ivp_message("gauss_unequ %f %f\n",left_side,desired_vector[linenum]);
    return IVP_FAULT;
  }
}

void IVP_Great_Matrix_Many_Zero::matrix_test_unequation() {
  for (int i = 0; i < columns; i++) {
    IVP_DOUBLE left = IVP_VecFPU::fpu_large_dot_product(
        &matrix_values[i * aligned_row_len], result_vector, columns, IVP_TRUE);
    IVP_IF(1) {
      ivp_message("unequation_test left %f right %f\n", left,
                  desired_vector[i]);
    }
  }
}

void IVP_Great_Matrix_Many_Zero::matrix_out_before_gauss() {
  IVP_IF(1) {
    for (int i = 0; i < columns; i++) {
      for (int j = 0; j < columns; j++) {
        ivp_message("%.5f  ", matrix_values[i * aligned_row_len + j]);
      }
      ivp_message("=  %.5f\n", desired_vector[i]);
    }
  }
}

// add factor * line number j to line number i (j=first i=second)
// the matrix-entries left of j in both lines are zero
void IVP_Great_Matrix_Many_Zero::add_multiple_line(int first, int second,
                                                   IVP_DOUBLE factor) {
  IVP_DOUBLE *first_line =
      &matrix_values[first * this->aligned_row_len + first];
  IVP_DOUBLE *second_line =
      &matrix_values[second * this->aligned_row_len + first];

  IVP_VecFPU::fpu_add_multiple_row(second_line, first_line, factor,
                                   columns - first, IVP_FALSE);
  desired_vector[second] += desired_vector[first] * factor;
}

void IVP_Great_Matrix_Many_Zero::debug_fill_zero() {
  // prevent SUN from getting read from uninitialized errors
  for (int i = 0; i < columns; i++) {
    IVP_VecFPU::fpu_set_row_to_zero(&matrix_values[i * aligned_row_len],
                                    aligned_row_len, IVP_TRUE);
  }
}

IVP_RETURN_TYPE IVP_Great_Matrix_Many_Zero::solve_great_matrix_many_zero() {
#ifdef DEBUG
  IVP_IF(0) { this->plausible_input_check(); }
#endif
  transform_to_lower_null_triangle();
  IVP_RETURN_TYPE rt = solve_lower_null_matrix();
  return rt;
}

void IVP_Great_Matrix_Many_Zero::copy_matrix(
    IVP_Great_Matrix_Many_Zero *orig_mat) {
  for (int i = 0; i < columns; i++) {
    for (int j = 0; j < columns; j++) {
      this->matrix_values[i * columns + j] =
          orig_mat->matrix_values[i * columns + j];
    }
    this->desired_vector[i] = orig_mat->desired_vector[i];
  }
}

void IVP_Great_Matrix_Many_Zero::align_matrix_values() {
  hk_intp adress, result_adress;
  adress = (hk_intp)matrix_values;
  int maximal_shift = (IVP_VECFPU_SIZE - 1) << IVP_VECFPU_MEMSHIFT;
  result_adress = (adress + maximal_shift) & IVP_VECFPU_MEM_MASK;
  matrix_values = (IVP_DOUBLE *)result_adress;

  IVP_IF(1) {
    for (int i = 0; i < columns; i++) {
      IVP_DOUBLE *x = &matrix_values[i * aligned_row_len];
      for (int j = columns; j < aligned_row_len; j++) {
        x[j] = 0.0f;
      }
    }
    for (int j = columns; j < aligned_row_len; j++) {
      desired_vector[j] = 0.0f;
    }
  }
}

void IVP_Great_Matrix_Many_Zero::copy_matrix(IVP_DOUBLE *values,
                                             IVP_DOUBLE *desired) {
  IVP_VecFPU::fpu_copy_rows(values, matrix_values, columns * columns, IVP_TRUE);
  IVP_VecFPU::fpu_copy_rows(desired, desired_vector, columns, IVP_TRUE);
}

void IVP_Great_Matrix_Many_Zero::find_pivot_in_column(int col) {
  IVP_DOUBLE best_val =
      IVP_Inline_Math::fabsd(matrix_values[col * aligned_row_len + col]);
  int pos = -1;
  for (int i = columns - 1; i > col; i--) {
    IVP_DOUBLE h = matrix_values[i * aligned_row_len + col];
    if (IVP_Inline_Math::fabsd(h) > best_val) {
      best_val = IVP_Inline_Math::fabsd(h);
      pos = i;
    }
  }
  if (pos >= 0) {
    exchange_rows(col, pos);
  }
}

// optimization: when called from transform_to_lower_null_triangle, begin by 'a'
// (first values are 0.0f)
void IVP_Great_Matrix_Many_Zero::exchange_rows(int a, int b) {
  IVP_VecFPU::fpu_exchange_rows(&matrix_values[b * aligned_row_len],
                                &matrix_values[a * aligned_row_len], columns,
                                IVP_TRUE);

  std::swap(desired_vector[a], desired_vector[b]);
}

// solve with Gauss-Algo. : upper triangle matrix with nulls in lower part
void IVP_Great_Matrix_Many_Zero::transform_to_lower_null_triangle() {
  // matrix_out_before_gauss();
  int i, j;

  // j is counting columns, i is counting lines

  // walk through columns
  for (j = 0; j < columns; j++) {
    IVP_DOUBLE diagonal_elem, negativ_inv_diagonal;
    // always pivot search
    find_pivot_in_column(j);
    diagonal_elem = matrix_values[j * aligned_row_len + j];
    if (IVP_Inline_Math::fabsd(diagonal_elem) < MATRIX_EPS) {
      goto column_done;
    }

    negativ_inv_diagonal = -1.0f / diagonal_elem;
    // walk through lines
    for (i = j + 1; i < columns; i++) {
      IVP_DOUBLE col_down_elem = matrix_values[i * aligned_row_len + j];
      if (IVP_Inline_Math::fabsd(col_down_elem) > MATRIX_EPS) {
        add_multiple_line(j, i, col_down_elem * negativ_inv_diagonal);
      }
    }
  column_done:;
  }

  // now matrix has zeros in lower-left part
  // solve columns from bottom to top
}

#ifdef DEBUG
void IVP_Great_Matrix_Many_Zero::plausible_input_check() {
  int i, j;
  for (i = 0; i < columns; i++) {
    for (j = 0; j < columns; j++) {
      IVP_DOUBLE a = matrix_values[i * aligned_row_len + j];
      IVP_ASSERT(a > -P_DOUBLE_MAX);
      IVP_ASSERT(a < P_DOUBLE_MAX);
    }
    IVP_DOUBLE b = desired_vector[i];
    IVP_ASSERT(b > -P_DOUBLE_MAX);
    IVP_ASSERT(b < P_DOUBLE_MAX);
  }
}
#endif

// matrix values are in upper triangle form, solve from bottom to top
IVP_RETURN_TYPE IVP_Great_Matrix_Many_Zero::solve_lower_null_matrix() {
  int i;
  for (i = columns - 1; i >= 0; i--) {
    IVP_DOUBLE *pointer_in_line =
        &matrix_values[i * aligned_row_len + columns - 1];

    IVP_DOUBLE right_side = desired_vector[i];
    for (int j = columns - 1; j > i; j--) {
      right_side -= desired_vector[j] * *pointer_in_line;
      pointer_in_line--;
    }
    IVP_DOUBLE left_side = *pointer_in_line;
    if (IVP_Inline_Math::fabsd(left_side) < MATRIX_EPS) {
      if (IVP_Inline_Math::fabsd(right_side) < MATRIX_EPS * 1000.0f) {
        // rang(matrix) is not maximal and result space is greater 1
        // choose an arbitrary result element (0.0f)
        // ivp_message("got_gauss_null %f %f\n",left_side,right_side);
        desired_vector[i] = 0.0f;
        IVP_IF(0) { ivp_message("gauss_singular_matrix\n"); }
      } else {
        // no result possible
        IVP_VecFPU::fpu_set_row_to_zero(result_vector, columns, IVP_TRUE);
        return IVP_FAULT;
      }
    } else {
      desired_vector[i] =
          right_side /
          left_side;  // division left_side is not needed, it was done already
                      // above (make a vector of inverses)
      if (desired_vector[i] > 10E11f) {
        IVP_IF(1) { ivp_message("\ngauss_failure\n\n"); }
      }
    }
  }

  IVP_VecFPU::fpu_copy_rows(result_vector, desired_vector, columns, IVP_TRUE);

  return IVP_OK;
}

void IVP_Great_Matrix_Many_Zero::matrix_multiplication(IVP_DOUBLE *first,
                                                       IVP_DOUBLE *second) {
  int i, j, k;
  for (i = 0; i < columns; i++) {
    for (j = 0; j < columns; j++) {
      IVP_DOUBLE sum = 0.0f;
      for (k = 0; k < columns; k++) {
        IVP_DOUBLE val = first[i * columns + k] * second[k * columns + j];
        sum += val;
      }
      matrix_values[i * columns + j] = sum;
    }
  }
}

// for debug only
int IVP_Great_Matrix_Many_Zero::test_result(IVP_DOUBLE *old_matrix,
                                            IVP_DOUBLE *old_desired) {
  // desired_vector and matrix is changed during calculation, old_matrix and
  // old_desired has values needed for test

  IVP_IF(1) {
    int i, j;
    for (j = 0; j < columns; j++) {
      // walk through lines
      IVP_DOUBLE left_side = 0.0f;
      for (i = 0; i < columns; i++) {
        left_side += old_matrix[j * columns + i] * result_vector[i];
        ivp_message("multip %.5f %.5f  ", old_matrix[j * columns + i],
                    result_vector[i]);
      }
      ivp_message("\nwanted_res was %.5f now %.5f\n", old_desired[j],
                  left_side);
    }
  }
  return 0;
}

IVP_Great_Matrix_Many_Zero::IVP_Great_Matrix_Many_Zero(int n) {
  IVP_ASSERT(0);  // does not work any longer: because of Vector-FPU adress has
                  // to be aligned normal malloc + free is no good

  MATRIX_EPS = 1.0f / 10E8f;
  columns = n;
  calc_aligned_row_len();
  matrix_values =
      (IVP_DOUBLE *)p_malloc(n * aligned_row_len * sizeof(IVP_DOUBLE));
  desired_vector = (IVP_DOUBLE *)p_malloc(aligned_row_len * sizeof(IVP_DOUBLE));
  result_vector = (IVP_DOUBLE *)p_malloc(aligned_row_len * sizeof(IVP_DOUBLE));
}

IVP_Great_Matrix_Many_Zero::IVP_Great_Matrix_Many_Zero() {
  MATRIX_EPS = 1.0f / 10E8f;
  columns = 0;
  aligned_row_len = 0;
  matrix_values = NULL;
  desired_vector = NULL;
  result_vector = NULL;
}

// both matrices are aligned
// my matrix is smaller than big_matrix, copy only those lines/columns that are
// wanted which are wanted is written in array original_pos
void IVP_Great_Matrix_Many_Zero::fill_from_bigger_matrix(
    IVP_Great_Matrix_Many_Zero *big_matrix, int *original_pos, int n_column) {
  if (n_column == 0) return;
  if (original_pos[n_column - 1] == n_column - 1) {  // no reorder
    IVP_IF(1) {
      for (int i = 0; i < n_column; i++) {
        IVP_ASSERT(original_pos[i] == i);
      }
    }
    for (int i = 0; i < n_column; i++) {
      IVP_DOUBLE *source =
          &big_matrix
               ->matrix_values[original_pos[i] * big_matrix->aligned_row_len];
      IVP_DOUBLE *dest = &matrix_values[i * aligned_row_len];
      IVP_VecFPU::fpu_copy_rows(dest, source, aligned_row_len, IVP_TRUE);
      this->desired_vector[i] = big_matrix->desired_vector[original_pos[i]];
    }
  } else {
    for (int i = 0; i < n_column; i++) {
      IVP_DOUBLE *dest = &matrix_values[i * aligned_row_len];
      IVP_IF((original_pos[i] != i)) {
        ivp_message("original_pos_diff %d %d\n", i, original_pos[i]);
      }

      IVP_DOUBLE *source =
          &big_matrix
               ->matrix_values[original_pos[i] * big_matrix->aligned_row_len];
      for (int j = 0; j < n_column; j++) {
        dest[j] = source[original_pos[j]];
      }
      this->desired_vector[i] = big_matrix->desired_vector[original_pos[i]];
    }
  }
}

// copies a column from original matrix to sub_matrix
void IVP_Great_Matrix_Many_Zero::copy_to_sub_matrix(
    IVP_DOUBLE *values_big_matrix, IVP_Great_Matrix_Many_Zero *sub_matrix,
    int *original_pos) {
  for (int i = 0; i < sub_matrix->columns; i++) {
    for (int j = 0; j < sub_matrix->columns; j++) {
      sub_matrix->matrix_values[i * sub_matrix->columns + j] =
          values_big_matrix[original_pos[i] * columns + original_pos[j]];
    }
  }
}

void IVP_Great_Matrix_Many_Zero::set_value(IVP_DOUBLE val, int col, int row) {
  // ATT: slow through mults
  IVP_ASSERT(col >= 0 && col < columns);
  IVP_ASSERT(row >= 0 && row < columns);

  // dimhotepus: note, uses 'columns' as row stride while get_value() uses
  // 'aligned_row_len'. Kept as-is (mult()/invert() use the 'columns' layout).
  *(this->matrix_values + col + columns * row) = val;
}

IVP_DOUBLE IVP_Great_Matrix_Many_Zero::get_value(int col, int row) const {
  // ATT: slow through mults
  IVP_ASSERT(col >= 0 && col < columns);
  IVP_ASSERT(row >= 0 && row < columns);

  return *(this->matrix_values + col + aligned_row_len * row);
}

int IVP_Great_Matrix_Many_Zero::print_great_matrix(const char *comment) const {
  ivp_message("Matrix: %s\n", comment);
  int i;
  for (i = 0; i < columns; i++) {  // rows
    int j;
    for (j = 0; j < columns; j++) {  // columns
      IVP_DOUBLE val = this->get_value(j, i);
      if (IVP_Inline_Math::fabsd(val) < 1e-20f) {
        ivp_message("    0  ");
      } else {
        ivp_message("%2.6g  ", val);
      }
    }
    ivp_message("\n");
  }
  ivp_message("desired ");
  for (i = 0; i < columns; i++) {
    ivp_message("%.6f ", desired_vector[i]);
  }
  ivp_message("\n");
  return 0;  // for dbx
}

// input: desired_vector, output: result_vector, rows have aligned_row_len
void IVP_Great_Matrix_Many_Zero::mult_aligned() {
  for (int j = 0; j < columns; j++) {
    result_vector[j] = IVP_VecFPU::fpu_large_dot_product(
        &matrix_values[j * aligned_row_len], desired_vector, columns, IVP_TRUE);
  }
}

// from desired_vector to result_vector, rows have 'columns' length
void IVP_Great_Matrix_Many_Zero::mult() {
  for (int j = 0; j < columns; j++) {
    result_vector[j] = IVP_VecFPU::fpu_large_dot_product(
        &matrix_values[j * columns], desired_vector, columns, IVP_FALSE);
  }
}

// can not be vector optimized because of indexed access
void IVP_Linear_Constraint_Solver::mult_active_x_for_accel() {
  int i, j;
  for (i = r_actives; i < n_variables; i++) {
    IVP_DOUBLE sum = 0.0f;
    int row_of_A = actives_inactives_ignored[i];
    IVP_DOUBLE *row_vals =
        &full_solver_mat.matrix_values[row_of_A * aligned_size];
    int real_pos;
    for (j = 0; j < r_actives; j++) {
      real_pos = actives_inactives_ignored[j];
      sum += delta_f[real_pos] * row_vals[real_pos];
    }
    real_pos = actives_inactives_ignored[ignored_pos];
    sum += row_vals[real_pos] * 1.0f;
    delta_accel[row_of_A] = sum;
  }
}

void IVP_Linear_Constraint_Solver::mult_x_with_full_A_minus_b() {
  for (int i = 0; i < n_variables; i++) {
    IVP_DOUBLE *base_a = &full_A[i * aligned_size];
    IVP_DOUBLE sum = IVP_VecFPU::fpu_large_dot_product(base_a, full_x,
                                                       n_variables, IVP_TRUE);
    temp[i] = sum - full_b[i];
  }
}

IVP_BOOL IVP_Linear_Constraint_Solver::numerical_stability_ok() {
  mult_x_with_full_A_minus_b();
  int k;
  IVP_DOUBLE val;
  IVP_DOUBLE worst_error = 0.0f;
  for (k = 0; k < r_actives; k++) {
    val = IVP_Inline_Math::fabsd(temp[actives_inactives_ignored[k]]);
    IVP_IF(1) {
      if (worst_error < val) {
        worst_error = val;
      }
    }
    if (val > TEST_EPS) {
      IVP_IF(debug_lcs) {
        ivp_message("numerical_unstable %d %.10f should be zero\n", ignored_pos,
                    val);
      }
      return IVP_FALSE;
    }
  }
  for (k = r_actives; k < n_variables; k++) {
    val = IVP_Inline_Math::fabsd(temp[actives_inactives_ignored[k]] -
                                 accel[actives_inactives_ignored[k]]);
    IVP_IF(1) {
      if (worst_error < val) {
        worst_error = val;
      }
    }
    if (val > TEST_EPS) {
      IVP_IF(debug_lcs) {
        ivp_message("numerical_unstable %d %.10f should be %.10f\n",
                    ignored_pos, temp[actives_inactives_ignored[k]],
                    accel[actives_inactives_ignored[k]]);
      }
      return IVP_FALSE;
    }
  }
  IVP_IF(0) { ivp_message("numerical_worst %.5e\n", worst_error); }
  return IVP_TRUE;
}

void IVP_Linear_Constraint_Solver::alloc_memory(IVP_U_Memory *my_mem) {
  actives_inactives_ignored = my_mem->get_mem<int>(aligned_size);
  variable_is_found_at = my_mem->get_mem<int>(aligned_size);

  accel = my_mem->get_mem<IVP_DOUBLE>(aligned_size);
  delta_accel = my_mem->get_mem<IVP_DOUBLE>(aligned_size);
  delta_f = my_mem->get_mem<IVP_DOUBLE>(aligned_size);
  reset_x = my_mem->get_mem<IVP_DOUBLE>(aligned_size);
  reset_accel = my_mem->get_mem<IVP_DOUBLE>(aligned_size);

  lu_sub_solver.L_matrix =
      my_mem->get_mem<IVP_DOUBLE>(aligned_size * n_variables);
  lu_sub_solver.U_matrix =
      my_mem->get_mem<IVP_DOUBLE>(aligned_size * n_variables);
  lu_sub_solver.input_vec = my_mem->get_mem<IVP_DOUBLE>(aligned_size);
  lu_sub_solver.out_vec = my_mem->get_mem<IVP_DOUBLE>(aligned_size);
  lu_sub_solver.temp_vec = my_mem->get_mem<IVP_DOUBLE>(aligned_size);
  lu_sub_solver.mult_vec = my_mem->get_mem<IVP_DOUBLE>(aligned_size);

  sub_solver_mat.matrix_values =
      my_mem->get_mem<IVP_DOUBLE>(aligned_size * n_variables);
  sub_solver_mat.desired_vector = my_mem->get_mem<IVP_DOUBLE>(aligned_size);
  sub_solver_mat.result_vector = my_mem->get_mem<IVP_DOUBLE>(aligned_size);

  inv_mat.matrix_values =
      my_mem->get_mem<IVP_DOUBLE>(aligned_size * n_variables);
  inv_mat.desired_vector = my_mem->get_mem<IVP_DOUBLE>(aligned_size);
  inv_mat.result_vector = my_mem->get_mem<IVP_DOUBLE>(aligned_size);

  IVP_IF(1) {  // fill LU for fast align access
    const int total = aligned_size * n_variables;
    IVP_VecFPU::fpu_set_row_to_zero(lu_sub_solver.L_matrix, total, IVP_TRUE);
    IVP_VecFPU::fpu_set_row_to_zero(lu_sub_solver.U_matrix, total, IVP_TRUE);
    IVP_VecFPU::fpu_set_row_to_zero(sub_solver_mat.matrix_values, total,
                                    IVP_TRUE);
    IVP_VecFPU::fpu_set_row_to_zero(inv_mat.matrix_values, total, IVP_TRUE);
    for (int k = n_variables; k < aligned_size; k++) {
      reset_accel[k] = 0.0f;
    }
  }
}

IVP_RETURN_TYPE IVP_Linear_Constraint_Solver::init_and_solve_lc(
    IVP_DOUBLE *A_in, IVP_DOUBLE *b_in, IVP_DOUBLE *result_vec_out, int var_num,
    int actives_at_begin, IVP_U_Memory *my_mem) {
  n_variables = var_num;
  aligned_size = (n_variables + IVP_VECFPU_SIZE - 1) & IVP_VECFPU_MASK;

  alloc_memory(my_mem);

  r_actives = 0;
  ignored_pos = 0;
  SOLVER_EPS = 1E-7f;  // do not make this one bigger
  GAUSS_EPS = SOLVER_EPS;
  TEST_EPS = SOLVER_EPS * 1000.0;
  sub_solver_status = 0;
  MAX_STEP_LEN = 10000;

  lu_sub_solver.aligned_row_len = aligned_size;
  lu_sub_solver.n_sub = 0;
  lu_sub_solver.MATRIX_EPS =
      SOLVER_EPS * 10;  // makes incrementel lu more often to fail but prevents
                        // from getting numerical inaccurate

  temp = lu_sub_solver.temp_vec;

  sub_solver_mat.MATRIX_EPS =
      GAUSS_EPS;  // for gauss a more liberal EPS is enough (otherwise it may
                  // happen the unilateral solver to fail completely) sorry, but
                  // for fdirection its not acceptable to have very large values

  inv_mat.MATRIX_EPS = SOLVER_EPS;

  IVP_IF(1) {
    debug_mat.matrix_values =
        my_mem->get_mem<IVP_DOUBLE>(aligned_size * var_num);
    debug_mat.desired_vector = my_mem->get_mem<IVP_DOUBLE>(aligned_size);
    debug_mat.result_vector = my_mem->get_mem<IVP_DOUBLE>(aligned_size);
    debug_mat.MATRIX_EPS = SOLVER_EPS;
  }

  full_solver_mat.columns = var_num;
  full_solver_mat.aligned_row_len = aligned_size;
  full_solver_mat.matrix_values = A_in;
  full_solver_mat.desired_vector = delta_f;
  full_solver_mat.result_vector = delta_accel;
  full_solver_mat.MATRIX_EPS = SOLVER_EPS;

  int i;
  for (i = 0; i < aligned_size; i++) {
    actives_inactives_ignored[i] = i;
    variable_is_found_at[i] = i;
    result_vec_out[i] = 0.0;
    accel[i] = -b_in[i];
  }

  full_A = A_in;
  full_b = b_in;
  full_x = result_vec_out;

  first_permute_index = 0;
  second_permute_index = 0;
  first_permute_ignored = 0;
  second_permute_ignored = 0;

  debug_no_lu_count = 0;
  debug_lcs = 0;

  IVP_IF(debug_lcs) {
    ivp_message(
        "\n***********************************************************\n");
    start_debug_lcs();
  }

  startup_setup(actives_at_begin);
  IVP_RETURN_TYPE ret_val = solve_lc();

  IVP_IF((debug_no_lu_count > 0) && (debug_lcs)) {
    ivp_message("sum_lu_failed %d\n", debug_no_lu_count);
  }

  IVP_IF(ret_val == IVP_OK) {
    full_solver_mat.desired_vector = full_x;
    full_solver_mat.mult_aligned();

    constexpr IVP_DOUBLE eps{0.0001};

    for (int j = 0; j < n_variables; j++) {
      if (full_solver_mat.result_vector[j] + eps < full_b[j]) {
        ivp_message("linear_constraint_failed #%d %f < %f (eps %.4f)\n", j,
                    full_solver_mat.result_vector[j], full_b[j], eps);
      }
    }
    for (int j = 0; j < r_actives; j++) {
      int index_full = actives_inactives_ignored[j];
      IVP_DOUBLE diff = IVP_Inline_Math::fabsd(
          full_solver_mat.result_vector[index_full] - full_b[index_full]);
      if (diff > eps) {
        ivp_message("linear_constraint_failed #%d %f == %f (diff %f > %.4f)\n",
                    index_full, full_solver_mat.result_vector[index_full],
                    full_b[index_full], diff, eps);
      }
    }
  }

  full_solver_mat.result_vector = NULL;
  full_solver_mat.desired_vector = NULL;
  full_solver_mat.matrix_values = NULL;
  sub_solver_mat.result_vector = NULL;
  sub_solver_mat.desired_vector = NULL;
  sub_solver_mat.matrix_values = NULL;
  inv_mat.result_vector = NULL;
  inv_mat.desired_vector = NULL;
  inv_mat.matrix_values = NULL;
  IVP_IF(1) {
    debug_mat.result_vector = NULL;
    debug_mat.desired_vector = NULL;
    debug_mat.matrix_values = NULL;
  }

  return ret_val;
}

void IVP_Linear_Constraint_Solver::decrement_sub_solver(int sub_pos) {
  if (sub_solver_status == 0) {
    IVP_ASSERT(lu_sub_solver.n_sub == r_actives + 1);
    if (lu_sub_solver.decrement_l_u(sub_pos) != IVP_OK) {
      IVP_IF(1) { ivp_message("\n\ndecrement_lu_failed_need_setup\n\n"); }
      sub_solver_status = 2;
    }
  } else {
    lu_sub_solver.n_sub--;
  }
}

void IVP_Linear_Constraint_Solver::increment_sub_solver() {
  int k;
  int new_var_orig = actives_inactives_ignored[r_actives - 1];
  if (sub_solver_status == 0) {
    IVP_ASSERT(lu_sub_solver.n_sub == r_actives - 1);
    for (k = 0; k < r_actives; k++) {
      lu_sub_solver
          .U_matrix[lu_sub_solver.n_sub * lu_sub_solver.aligned_row_len + k] =
          full_A[new_var_orig * aligned_size + actives_inactives_ignored[k]];
    }
    for (k = 0; k < r_actives - 1; k++) {
      lu_sub_solver.input_vec[k] =
          full_A[aligned_size * actives_inactives_ignored[k] + new_var_orig];
    }
    if (lu_sub_solver.increment_l_u() != IVP_OK) {
      IVP_IF(debug_lcs) {
        ivp_message("\n\nincrement_lu_failed_need_setup\n\n");
      }
      sub_solver_status = 2;
    }
  } else {
    lu_sub_solver.n_sub++;
  }
}

void IVP_Linear_Constraint_Solver::do_a_little_random_permutation() {
  // in case numeric gets unstable, neverending loops can occur
  // do some permutions to get fresh values ( in gauss the last variable gets
  // zero, this "wrong" values is distributed alternatively )

  if (r_actives < 2) {
    return;
  }

  first_permute_index += 1;
  second_permute_index += 2;

  while (first_permute_index >= r_actives) {
    first_permute_index -= r_actives;
  }
  while (second_permute_index >= r_actives) {
    second_permute_index -= r_actives;
  }
  exchange_lcs_variables(first_permute_index, second_permute_index);

  first_permute_ignored += 1;
  second_permute_ignored += 2;

  int free_space = n_variables - ignored_pos - 1;
  if (free_space < 2) {
    return;
  }
  while (first_permute_ignored >= free_space) {
    first_permute_ignored -= free_space;
  }
  while (second_permute_ignored >= free_space) {
    second_permute_ignored -= free_space;
  }
  exchange_lcs_variables(first_permute_ignored + ignored_pos + 1,
                         second_permute_ignored + ignored_pos + 1);
}

void IVP_Linear_Constraint_Solver::move_not_necessary_actives_to_inactives() {
  int i;
  for (i = 0; i < r_actives; i++) {
    if (full_x[actives_inactives_ignored[i]] < SOLVER_EPS) {
      // ivp_message("juhu_removed_not_necessary_active %d full
      // %d\n",i,actives_inactives_ignored[i]);
      full_x[actives_inactives_ignored[i]] = 0.0f;
      exchange_lcs_variables(i, r_actives - 1);
      r_actives--;
      decrement_sub_solver(i);
      i--;
    }
  }
}

void IVP_Linear_Constraint_Solver::increase_step_count(int *step_count) {
  (*step_count)++;
}

// actives_inactives_ignored : array to transform index from small variable
// space (that is growing) to full variable space first are active variables:
// their accel - val is 0.0f and x val is > 0.0f then inactives: their x val is
// 0.0f and accel val is > 0.0f last ignored: x and accel unknown, first of
// ignored is processed

// doctrin:
// when step size and variables are near to 0.0f than try to move corresponding
// variable into inactive variables (makes algorithm faster (sub matrix small)
// and robust (sub matrix not singular)) but moving variables with x=0.0f into
// active cannot be avoided: sometimes it is necessary when moving first ignored
// into actives (as second step)
IVP_RETURN_TYPE IVP_Linear_Constraint_Solver::solve_lc() {
  int index;  // var to go from sub space to full space
  int step_count = 0;
  int nr_zero_steps = 0;  // to avoid endless loops
  int did_real_step = 0;  // real step means that active variables have changed
  int next_numeric_stability_test = IVP_NUMERIC_TEST_EVERY_N;

  // ivp_message("\n\n\n\n\ndoing_lcs_ %d\n",n_variables);

  IVP_DOUBLE solver_eps = SOLVER_EPS;

  while (1) {
    if (did_real_step) {
      increase_step_count(&step_count);
    }
    if (step_count > IVP_UNILATERAL_SOLVER_MAX_STEP) {
      IVP_IF(1) {
        ivp_message("\n\ngiveup_stepcount_linear_constraint_solver\n\n\n");
      }
      return IVP_FAULT;
    }
    int limiting_var;

    if (next_numeric_stability_test == 0) {
      if ((numerical_stability_ok() == IVP_FALSE)) {
      perform_full_setup:
        increase_step_count(&step_count);
        if (step_count > IVP_UNILATERAL_SOLVER_MAX_STEP) {
          IVP_IF(1) {
            ivp_message("\n\ngiveup_stepcount_linear_constraint_solver\n\n\n");
          }
          return IVP_FAULT;
        }
        nr_zero_steps = 0;
        if (full_setup() == IVP_FAULT) {
          return IVP_FAULT;
        }
      }
      next_numeric_stability_test = IVP_NUMERIC_TEST_EVERY_N;
    } else {
      next_numeric_stability_test--;
    }

    if (ignored_pos >= n_variables) {
      return IVP_OK;
    }

    int ignored_full_index = actives_inactives_ignored[ignored_pos];

    IVP_IF(debug_lcs) {
      int k;
      for (k = 0; k < r_actives; k++) {
        int idx = actives_inactives_ignored[k];
        IVP_IF(IVP_Inline_Math::fabsd(accel[idx]) > 0.01f) {
          ivp_message("incorrect_accel %f at %d  should be zero\n", accel[idx],
                      idx);
        }
        IVP_IF(IVP_Inline_Math::fabsd(full_x[idx]) < solver_eps) {
          if (IVP_Inline_Math::fabsd(full_x[ignored_full_index]) < solver_eps) {
            ivp_message("active_var %d shouldnt be active but inactive\n", idx);
          }
        }
        if (full_x[idx] < 0.0f) {
          IVP_IF(1) {
            ivp_message("active_var %d should be positive %f\n", idx,
                        full_x[idx]);
          }
        }
      }
    }
    IVP_IF(1) {
      int k;
      for (k = r_actives; k < ignored_pos; k++) {
        int idx = actives_inactives_ignored[k];
        IVP_IF(IVP_Inline_Math::fabsd(full_x[idx]) > 0.1f) {
          ivp_message("incorrect_x %f at %d  should be zero\n", accel[idx],
                      idx);
        }
        if (full_x[idx] < -0.001f) {
          ivp_message("incorrect_negative_x %f at %d  should be zero\n",
                      accel[idx], idx);
        }
        if (accel[idx] < 0.0f) {
          ivp_message("warning_inactive_neg accel %d %f\n", idx, accel[idx]);
        }
      }
    }

    IVP_IF(debug_lcs) { debug_out_lcs(); }

    IVP_IF(IVP_Inline_Math::fabsd(full_x[ignored_full_index]) < solver_eps) {
      debug_test_all_values();
    }

    if ((sub_solver_status > 0)) {
      if ((sub_solver_status == 1) && (did_real_step)) {
        do_a_little_random_permutation();
        if (setup_l_u_solver() == IVP_OK) {
          sub_solver_status = 0;
        } else {
          IVP_IF(debug_lcs) { ivp_message("setup_lu_notok\n"); }
        }
      } else {
        sub_solver_status = 1;  // ok then next time
      }
    }

    if (IVP_Inline_Math::fabsd(accel[ignored_full_index]) < solver_eps) {
      // my processed variable has no acceleration and so could be an inactive
      // var except the force x is greater 0.0f than we must move it to actives
      limiting_var = ignored_pos;
      if (IVP_Inline_Math::fabsd(full_x[ignored_full_index]) < solver_eps) {
        goto move_ignored_to_inactive;
      } else {
        goto move_ignored_to_active;
      }
    }

    if (accel[ignored_full_index] < 0.0f) {
      IVP_DOUBLE smallest_step;

      // first r vars are clamped
      // compute x with Acc x = b
      if (get_fdirection() == IVP_FAULT) {
        IVP_IF(debug_lcs) { ivp_message("lcs_fdirection_total_fail\n"); }
        goto perform_full_setup;
      }
      delta_f[ignored_full_index] = 1.0f;

      // delta_f is now filled in original var space

      IVP_IF(0) {
        ivp_message("fullmat:\n");
        full_solver_mat.print_great_matrix("");
        ivp_message("\n");
      }

      // full_solver_mat.mult();  //delta_accel = A * delta_f
      // delta_accel is now filled in original var space
      mult_active_x_for_accel();

      int i;
      for (i = 0; i < r_actives; i++) {
        int index_full = actives_inactives_ignored[i];
        delta_accel[index_full] = 0.0f;  // just to overwrite rounding-errors
      }

      IVP_IF(0) {
        for (int j = 0; j < r_actives; j++) {
          int full_index = actives_inactives_ignored[j];
          ivp_message("should be zero %f\n", delta_accel[full_index]);
        }
      }

      // now find smallest steps

      // step to make clamped active out of ignored_pos
      IVP_IF(accel[ignored_full_index] > 0.0f) {
        ivp_message("errror_accel_ignored_negativ\n");
      }
      if (-accel[ignored_full_index] <
          delta_accel[ignored_full_index] * MAX_STEP_LEN) {
        smallest_step =
            -accel[ignored_full_index] / delta_accel[ignored_full_index];
        IVP_ASSERT(smallest_step > 0.0f);
        limiting_var = ignored_pos;
      } else {
        limiting_var = -1;
        smallest_step = P_DOUBLE_MAX;
      }

      // smallest step of clamped
      for (i = 0; i < r_actives; i++) {
        index = actives_inactives_ignored[i];
        IVP_DOUBLE del_f = delta_f[index];
        if (del_f < -solver_eps) {
          IVP_DOUBLE local_step = -full_x[index] / del_f;
          if ((IVP_Inline_Math::fabsd(local_step) < solver_eps) &&
              (full_x[index] < solver_eps)) {
            limiting_var = i;
            smallest_step = local_step;
            goto limiting_var_found;
          }
          if (local_step <
              smallest_step + solver_eps) {  // make it easier to move from
                                             // active to inactive
            smallest_step = local_step;
            limiting_var = i;
          }
        }
      }

      // smallest step of unclamped
      for (i = r_actives; i < ignored_pos; i++) {
        index = actives_inactives_ignored[i];
        IVP_DOUBLE del_a = delta_accel[index];
        if (del_a < -SOLVER_EPS) {
          IVP_DOUBLE local_step = -accel[index] / del_a;
          if (local_step <
              smallest_step - solver_eps) {  // make it harder to move from
                                             // inactive to active
            IVP_IF(debug_lcs) {
              ivp_message("violate_inactive: accel %.4f  delta %.4f\n",
                          accel[index], del_a);
            }
            // we prefer making inactives out of actives instead other way round
            // if both is possible ( both steps are nearly zero ) uuups this is
            // all wrong
            if ((IVP_Inline_Math::fabsd(local_step) > solver_eps) ||
                (limiting_var == ignored_pos) || 1) {
              smallest_step = local_step;
              limiting_var = i;
            }
          }
        }
      }

      if (smallest_step < 0.0f) {
        IVP_IF(1) {
          ivp_message("error_stepsize_negative %f\n", smallest_step);
        }
        smallest_step = 0.0f;
      }

      if (limiting_var < 0) {
        return IVP_FAULT;
      }

      IVP_IF(debug_lcs) { ivp_message("smallest_step_is %f\n", smallest_step); }

    limiting_var_found:
      if (smallest_step > MAX_STEP_LEN) {
        goto perform_full_setup;
      }
      if (smallest_step < SOLVER_EPS) {
        nr_zero_steps++;
        if (nr_zero_steps > (n_variables >> 1) + 2) {  // heuristic
          goto perform_full_setup;  // does a little random permutation, we hope
                                    // next time no endless loop
        }
      } else {
        nr_zero_steps = 0;
      }
      did_real_step = 1;
      update_step_vars(smallest_step);

      if (limiting_var < r_actives) {
        // one active var becomes inactive

        IVP_IF(debug_lcs) {
          ivp_message("\nactive %d becomes inactive (relp %d)\n\n",
                      actives_inactives_ignored[limiting_var], limiting_var);
        }

        int indx = actives_inactives_ignored[limiting_var];
        IVP_ASSERT((IVP_Inline_Math::fabsd(full_x[indx]) < 0.001f) ||
                   (IVP_Inline_Math::fabsd(smallest_step) < solver_eps));
        full_x[indx] = 0.0f;
        r_actives--;
        // exchange_activ_inactiv(limiting_var);
        exchange_lcs_variables(r_actives, limiting_var);
        decrement_sub_solver(limiting_var);
      } else {
        if (limiting_var < ignored_pos) {
          // one inactive var becomes active
          if (smallest_step > SOLVER_EPS) {
            move_not_necessary_actives_to_inactives();
          }

          IVP_IF(debug_lcs) {
            ivp_message("\ninactive %d becomes active (relp %d)\n\n",
                        actives_inactives_ignored[limiting_var], limiting_var);
          }
          int indx = actives_inactives_ignored[limiting_var];
          IVP_ASSERT(IVP_Inline_Math::fabsd(accel[indx]) < 0.001f);
          accel[indx] = 0.0f;
          // exchange_activ_inactiv(limiting_var);
          exchange_lcs_variables(r_actives, limiting_var);
          r_actives++;
          increment_sub_solver();
        } else {
        move_ignored_to_active:
          // ignored_pos comes to the active variables
          move_not_necessary_actives_to_inactives();
          IVP_IF(debug_lcs) {
            ivp_message("\nignored %d becomes active (relp %d)\n\n",
                        actives_inactives_ignored[ignored_pos], ignored_pos);
          }
          int indx = actives_inactives_ignored[limiting_var];
          IVP_ASSERT(IVP_Inline_Math::fabsd(accel[indx]) < 0.001f);
          accel[indx] = 0.0f;
          if (full_x[indx] < 0.0f) {  // really necessary ?
            full_x[indx] = 0.0f;
          }
          // exchange_activ_inactiv(limiting_var);
          exchange_lcs_variables(r_actives, limiting_var);
          ignored_pos++;
          r_actives++;
          if (ignored_pos < n_variables) {
            increment_sub_solver();
          } else {
            next_numeric_stability_test = 0;
          }
          IVP_IF(0) { debug_test_all_values(); }
        }
      }
    } else {
    move_ignored_to_inactive:
      // ignored_pos comes to the inactives variables
      did_real_step = 0;  // active variables are unchanged
      IVP_IF(debug_lcs) {
        ivp_message("ignored %d becomes inactive (relp %d)\n\n",
                    actives_inactives_ignored[ignored_pos], ignored_pos);
      }
      int indx = actives_inactives_ignored[ignored_pos];
      IVP_IF(!(IVP_Inline_Math::fabsd(full_x[indx]) < 0.001f)) {
        ivp_message("error_move_ignored_to_inactive_x_not_zero\n");
      }

      full_x[indx] = 0.0f;
      if (accel[indx] < 0.0f) {  // really necessary ?
        accel[indx] = 0.0f;
      }
      ignored_pos++;
      if (ignored_pos >= n_variables) {
        next_numeric_stability_test = 0;
      }
      // reset_values();
    }
  }

  IVP_IF(debug_lcs) { debug_out_lcs(); }

  return IVP_OK;
}

// both numbers are in sub-space
void IVP_Linear_Constraint_Solver::exchange_lcs_variables(int first_nr,
                                                          int second_nr) {
  IVP_IF(1) {
    IVP_ASSERT(variable_is_found_at[actives_inactives_ignored[second_nr]] ==
               second_nr);
    IVP_ASSERT(variable_is_found_at[actives_inactives_ignored[first_nr]] ==
               first_nr);
  }
  IVP_ASSERT(first_nr >= 0);
  IVP_ASSERT(second_nr >= 0);
  IVP_ASSERT(first_nr < n_variables);
  IVP_ASSERT(second_nr < n_variables);
  int full_space_0, full_space_1;
  full_space_0 = actives_inactives_ignored[second_nr];
  full_space_1 = actives_inactives_ignored[first_nr];
  actives_inactives_ignored[first_nr] = full_space_0;
  actives_inactives_ignored[second_nr] = full_space_1;
  variable_is_found_at[full_space_0] = first_nr;
  variable_is_found_at[full_space_1] = second_nr;
}
// please merge these two functions !!!!
void IVP_Linear_Constraint_Solver::exchange_activ_inactiv(int change_var) {
  IVP_IF(1) {
    IVP_ASSERT(variable_is_found_at[actives_inactives_ignored[change_var]] ==
               change_var);
    IVP_ASSERT(variable_is_found_at[actives_inactives_ignored[r_actives]] ==
               r_actives);
  }
  int first_inactiv = r_actives;
  int full_space_0, full_space_1;
  full_space_0 = actives_inactives_ignored[change_var];
  full_space_1 = actives_inactives_ignored[first_inactiv];
  actives_inactives_ignored[first_inactiv] = full_space_0;
  actives_inactives_ignored[change_var] = full_space_1;
  variable_is_found_at[full_space_0] = first_inactiv;
  variable_is_found_at[full_space_1] = change_var;
}

void IVP_Linear_Constraint_Solver::update_step_vars(IVP_DOUBLE step_size) {
  int i;
  IVP_IF(debug_lcs) {
    ivp_message("deltaa  ");
    for (i = 0; i < n_variables; i++) {
      ivp_message("%f ", delta_accel[i]);
    }
    ivp_message("\n");
  }
  // accel += step * delta_accel; x += step * delta_f
  // (same per-element mul+add as before, so results are bit-identical)
  IVP_VecFPU::fpu_add_multiple_row(accel, delta_accel, step_size, n_variables,
                                   IVP_TRUE);
  IVP_VecFPU::fpu_add_multiple_row(full_x, delta_f, step_size, n_variables,
                                   IVP_TRUE);

  // in some cases (e.g. overriding an inactive) acceleration of inactives gets
  // negative
  for (i = r_actives; i < ignored_pos; i++) {
    if (accel[actives_inactives_ignored[i]] < 0.0f) {
      accel[actives_inactives_ignored[i]] = 0.0f;
    }
  }
  // same with actives
  for (i = 0; i < r_actives; i++) {
    if (full_x[actives_inactives_ignored[i]] < 0.0f) {
      full_x[actives_inactives_ignored[i]] = 0.0f;
    }
  }
}

IVP_RETURN_TYPE IVP_Linear_Constraint_Solver::get_fdirection() {
  // clear all
  int i, k;
  IVP_VecFPU::fpu_set_row_to_zero(delta_f, aligned_size, IVP_TRUE);

  if (r_actives == 0) {
    return IVP_OK;
  }

  IVP_RETURN_TYPE ret_val = IVP_FAULT;

  if (sub_solver_status == 0) {
    int ignored_index = actives_inactives_ignored[ignored_pos];

    for (k = 0; k < r_actives; k++) {
      int index = actives_inactives_ignored[k];
      lu_sub_solver.input_vec[k] =
          -this->full_A[index * aligned_size + ignored_index];
    }
    lu_sub_solver.solve_lin_equ();
    ret_val = IVP_OK;

    IVP_IF(debug_lcs) {
      sub_solver_mat.columns = r_actives;
      sub_solver_mat.calc_aligned_row_len();
      sub_solver_mat.MATRIX_EPS =
          GAUSS_EPS *
          0.001f;  // we know our matrix is stable, so it is save to raise EPS
      inv_mat.columns = r_actives;
      debug_mat.columns = r_actives;

      for (i = 0; i < r_actives; i++) {
        int index = actives_inactives_ignored[i];
        sub_solver_mat.desired_vector[i] =
            -this->full_A[index * aligned_size + ignored_index];
        debug_mat.desired_vector[i] =
            -this->full_A[index * aligned_size + ignored_index];
        int j;
        for (j = 0; j < r_actives; j++) {
          int index2 = actives_inactives_ignored[j];
          sub_solver_mat.matrix_values[i * sub_solver_mat.aligned_row_len + j] =
              this->full_A[index * aligned_size + index2];
          inv_mat.matrix_values[i * r_actives + j] =
              this->full_A[index * aligned_size + index2];
        }
      }
      IVP_RETURN_TYPE ret_val2 = sub_solver_mat.solve_great_matrix_many_zero();
      IVP_RETURN_TYPE ret_val3 = inv_mat.invert(&debug_mat);
      if (ret_val2 == IVP_OK) {
        for (i = 0; i < r_actives; i++) {
          IVP_DOUBLE diff =
              lu_sub_solver.out_vec[i] - sub_solver_mat.result_vector[i];
          if (IVP_Inline_Math::fabsd(diff) > 0.001f) {
            IVP_IF(debug_lcs) {
              ivp_message("gauss_lu_test_fail %f %f should be equal\n",
                          lu_sub_solver.out_vec[i],
                          sub_solver_mat.result_vector[i]);
            }
          }
        }
      } else {
        IVP_IF(debug_lcs) { ivp_message("gauss_lu_test_gauss_invalid\n"); }
      }
      if (ret_val3 == IVP_OK) {
        debug_mat.mult();
        for (i = 0; i < r_actives; i++) {
          IVP_DOUBLE diff =
              lu_sub_solver.out_vec[i] - debug_mat.result_vector[i];
          if (IVP_Inline_Math::fabsd(diff) > 0.001f) {
            IVP_IF(debug_lcs) {
              ivp_message("inv_lu_test_fail %f %f should be equal\n",
                          lu_sub_solver.out_vec[i], debug_mat.result_vector[i]);
            }
          }
        }
      }
      sub_solver_mat.MATRIX_EPS = GAUSS_EPS;
    }
  } else {
    IVP_IF((debug_lcs)) { ivp_message("sub_lu_is_off %d\n", r_actives); }
    debug_no_lu_count++;
    sub_solver_mat.columns = r_actives;
    sub_solver_mat.calc_aligned_row_len();
    int ignored_index = actives_inactives_ignored[ignored_pos];

    for (i = 0; i < r_actives; i++) {
      int index = actives_inactives_ignored[i];
      sub_solver_mat.desired_vector[i] =
          -this->full_A[index * aligned_size + ignored_index];

      int j;
      for (j = 0; j < r_actives; j++) {
        int index2 = actives_inactives_ignored[j];
        sub_solver_mat.matrix_values[i * sub_solver_mat.aligned_row_len + j] =
            this->full_A[index * aligned_size + index2];
      }
    }
    ret_val = sub_solver_mat.solve_great_matrix_many_zero();
    IVP_VecFPU::fpu_copy_rows(lu_sub_solver.out_vec,
                              sub_solver_mat.result_vector, r_actives,
                              IVP_TRUE);
  }

  // copy clamped
  for (i = 0; i < r_actives; i++) {
    this->delta_f[actives_inactives_ignored[i]] = lu_sub_solver.out_vec[i];
  }

  this->delta_f[actives_inactives_ignored[ignored_pos]] = 1.0f;

  return ret_val;
}

void IVP_Linear_Constraint_Solver::start_debug_lcs() {
  ivp_message("b_vals ");
  int i;
  int j;
  for (i = 0; i < n_variables; i++) {
    ivp_message("%.4f ", full_b[i]);
  }
  ivp_message("\n");

  ivp_message("matrix:\n");
  for (i = 0; i < n_variables; i++) {
    for (j = 0; j < n_variables; j++) {
      ivp_message("%.4f ", full_A[i * aligned_size + j]);
    }
    ivp_message("\n");
  }
  ivp_message("\n");
}

void IVP_Linear_Constraint_Solver::debug_out_lcs() {
  ivp_message("r_actives %d  ignored_nr %d\n", r_actives, ignored_pos);
  int i;

  if (ignored_pos > n_variables) {
    ignored_pos = n_variables;
  }

  ivp_message("actives_inactives  ");
  for (i = 0; i < r_actives; i++) {
    ivp_message("%d ", actives_inactives_ignored[i]);
  }
  ivp_message("  ");
  for (i = r_actives; i < ignored_pos; i++) {
    ivp_message("%d ", actives_inactives_ignored[i]);
  }
  ivp_message("  ");
  for (i = ignored_pos; i < n_variables; i++) {
    ivp_message("%d ", actives_inactives_ignored[i]);
  }
  ivp_message("\n");

  ivp_message("variable_is_found  ");
  for (i = 0; i < r_actives; i++) {
    ivp_message("%d ", variable_is_found_at[i]);
  }
  ivp_message("  ");
  for (i = r_actives; i < ignored_pos; i++) {
    ivp_message("%d ", variable_is_found_at[i]);
  }
  ivp_message("  ");
  for (i = ignored_pos; i < n_variables; i++) {
    ivp_message("%d ", variable_is_found_at[i]);
  }
  ivp_message("\n");

  ivp_message("x-vals ");
  for (i = 0; i < n_variables; i++) {
    ivp_message("%.4f ", full_x[i]);
  }
  ivp_message("\n");

  full_solver_mat.desired_vector = full_x;
  full_solver_mat.mult_aligned();  // temporary missuse
  full_solver_mat.desired_vector = delta_f;

  ivp_message("accel1 ");
  for (i = 0; i < n_variables; i++) {
    ivp_message("%.4f ", full_solver_mat.result_vector[i] - full_b[i]);
  }
  ivp_message("\n");

  ivp_message("incr_accel ");
  for (i = 0; i < n_variables; i++) {
    ivp_message("%.4f ", accel[i]);
  }
  ivp_message("\n\n");
}

// cannot be vector optimized because of indexed access
IVP_RETURN_TYPE IVP_Linear_Constraint_Solver::setup_l_u_solver() {
  int i;
  IVP_IF(0) { ivp_message("setting_up_lu\n"); }

  for (i = 0; i < r_actives; i++) {
    int index = actives_inactives_ignored[i];
    // sub_solver_mat.desired_vector[i]= b[index];
    lu_sub_solver.input_vec[i] = full_b[index];
    IVP_DOUBLE *source = &this->full_A[index * aligned_size];
    IVP_DOUBLE *dest =
        &lu_sub_solver.U_matrix[i * lu_sub_solver.aligned_row_len];
    for (int j = 0; j < r_actives; j++) {
      int index2 = actives_inactives_ignored[j];
      dest[j] = source[index2];
    }
  }

  return lu_sub_solver.l_u_decomposition_with_pivoting();
}

void IVP_Linear_Constraint_Solver::lcs_bubble_sort_x_vals() {
  int r1 = 0;
  int r2;
  while (1) {
    r2 = r1 + 1;
    if (r2 >= r_actives) {
      break;
    }
    if (full_x[actives_inactives_ignored[r1]] <
        full_x[actives_inactives_ignored[r2]]) {
      exchange_lcs_variables(r1, r2);
      int r_t = r1;
      while ((r_t > 0) && (full_x[actives_inactives_ignored[r_t - 1]] <
                           full_x[actives_inactives_ignored[r_t]])) {
        exchange_lcs_variables(r_t, r_t - 1);
        r_t--;
      }
    }
    r1 = r2;
  }

  IVP_IF(1) {
    int i;
    for (i = 1; i < r_actives; i++) {
      if (full_x[actives_inactives_ignored[i - 1]] <
          full_x[actives_inactives_ignored[i]]) {
        ivp_message("lcs_bubble_failed\n");
      }
    }
  }
}

void IVP_Linear_Constraint_Solver::get_values_when_setup() {
  full_solver_mat.result_vector = reset_accel;
  full_solver_mat.desired_vector = reset_x;  // temporary misuse
  full_solver_mat.mult_aligned();
  full_solver_mat.result_vector = delta_accel;
  full_solver_mat.desired_vector = delta_f;

  int i;
  // reset_accel -= b
  IVP_VecFPU::fpu_add_multiple_row(reset_accel, full_b, -1.0f, n_variables,
                                   IVP_TRUE);

  for (i = 0; i < r_actives; i++) {
    reset_accel[actives_inactives_ignored[i]] =
        0.0f;  // just to remove rounding errors
  }

  IVP_VecFPU::fpu_copy_rows(accel, reset_accel, n_variables, IVP_TRUE);
  IVP_VecFPU::fpu_copy_rows(full_x, reset_x, n_variables, IVP_TRUE);
}

void IVP_Linear_Constraint_Solver::startup_setup(int try_actives) {
  r_actives = try_actives;
  aligned_sub_size = (r_actives + IVP_VECFPU_SIZE - 1) & IVP_VECFPU_MASK;
  ignored_pos = try_actives;
  lu_sub_solver.n_sub = r_actives;

  int i;
  IVP_VecFPU::fpu_set_row_to_zero(reset_x, aligned_size, IVP_TRUE);
  if (setup_l_u_solver() == IVP_OK) {
    sub_solver_status = 0;
    // input vec for lu_sub_solver is filled within setup_l_u_solver

    while (1) {
    begin_of_while:
      // solution_not_found=0;
      lu_sub_solver.solve_lin_equ();
      for (i = 0; i < r_actives; i++) {
        int index = actives_inactives_ignored[i];
        reset_x[index] = lu_sub_solver.out_vec[i];
      }
      get_values_when_setup();  // fills x[i] and accel[i]

      for (i = r_actives - 1; i >= 0; i--) {
        int index = actives_inactives_ignored[i];
        if (full_x[index] < 0.0f) {
          full_x[index] = 0.0f;
          reset_x[index] = 0.0f;
          r_actives--;
          ignored_pos--;
          exchange_lcs_variables(r_actives, i);
          decrement_sub_solver(i);
          if (sub_solver_status > 0) {
            IVP_VecFPU::fpu_set_row_to_zero(accel, n_variables, IVP_TRUE);
            IVP_VecFPU::fpu_set_row_to_zero(full_x, n_variables, IVP_TRUE);
            goto fast_setup_failed;
          } else {
            for (i = 0; i < r_actives; i++) {
              index = actives_inactives_ignored[i];
              lu_sub_solver.input_vec[i] = full_b[index];
            }
            goto begin_of_while;
          }
        }
      }
      break;
    }
  } else {
  fast_setup_failed:
    // sorry fast setup failed
    r_actives = 0;
    ignored_pos = 0;
    lu_sub_solver.n_sub = 0;
  }
}

IVP_RETURN_TYPE IVP_Linear_Constraint_Solver::full_setup() {
  IVP_IF(0) { ivp_message("doing_fulll_setup\n"); }

  int i;
  lu_sub_solver.n_sub = r_actives;

  IVP_VecFPU::fpu_set_row_to_zero(reset_x, aligned_size, IVP_TRUE);

  do_a_little_random_permutation();
  if (setup_l_u_solver() == IVP_OK) {
    sub_solver_status = 0;
    // input vec for lu_sub_solver is filled within setup_l_u_solver
    lu_sub_solver.solve_lin_equ();
    for (i = 0; i < r_actives; i++) {
      int index = actives_inactives_ignored[i];
      reset_x[index] = lu_sub_solver.out_vec[i];
    }
  } else {
    sub_solver_status = 2;
    IVP_IF(debug_lcs) { ivp_message("setup_lu_failed\n"); }

    lcs_bubble_sort_x_vals();  // move actives with lowest x to the end (matrix
                               // is singular and last vars get a zero) but
                               // giving the lowest x's the zeros is just a
                               // heuristic
    do_a_little_random_permutation();

    sub_solver_mat.columns = r_actives;
    sub_solver_mat.calc_aligned_row_len();

    for (i = 0; i < r_actives; i++) {
      int index = actives_inactives_ignored[i];
      sub_solver_mat.desired_vector[i] = full_b[index];

      int j;
      for (j = 0; j < r_actives; j++) {
        int index2 = actives_inactives_ignored[j];
        sub_solver_mat.matrix_values[i * sub_solver_mat.aligned_row_len + j] =
            this->full_A[index * aligned_size + index2];
      }
    }
    IVP_RETURN_TYPE ret_val = sub_solver_mat.solve_great_matrix_many_zero();

    if (ret_val != IVP_OK) {
      IVP_IF(1) { ivp_message("error_setup_gauss_failed_too\n"); }
      return ret_val;
    }

    for (i = 0; i < r_actives; i++) {
      int index = actives_inactives_ignored[i];
      reset_x[index] = sub_solver_mat.result_vector[i];
    }
  }

  get_values_when_setup();

  if (full_setup_test_ranges() > 0) {
    IVP_IF(debug_lcs) { ivp_message("\ncleared_some_illegal_vars\n\n"); }
    return full_setup();
  } else {
  }

  return IVP_OK;
  // optimization:
  // fill directly in data, not versus reset_*
}

void IVP_Linear_Constraint_Solver::move_variable_to_end(int var_nr) {
  int i;
  for (i = var_nr + 1; i < n_variables; i++) {
    exchange_lcs_variables(i - 1, i);
  }
}

// returns number of removed actives
int IVP_Linear_Constraint_Solver::full_setup_test_ranges() {
  int illegal_vars = 0;
  int i;
  for (i = 0; i < r_actives; i++) {
    if (full_x[actives_inactives_ignored[i]] < SOLVER_EPS) {
      if (full_x[actives_inactives_ignored[i]] > -SOLVER_EPS) {
        full_x[actives_inactives_ignored[i]] = 0.0f;
        exchange_lcs_variables(i, r_actives - 1);
      } else {
        move_variable_to_end(i);
        illegal_vars++;
        ignored_pos--;
      }
      i--;
      r_actives--;
      lu_sub_solver.n_sub--;
      sub_solver_status = 1;
    }
  }
  for (i = r_actives; i < ignored_pos; i++) {
    if (accel[actives_inactives_ignored[i]] < 0.0f) {
      move_variable_to_end(i);
      i--;
      ignored_pos--;
    }
  }
  return illegal_vars;
}

void IVP_Linear_Constraint_Solver::debug_test_all_values() {
  sub_solver_mat.columns = r_actives;
  int i;

  IVP_VecFPU::fpu_copy_rows(reset_x, full_x, aligned_size, IVP_TRUE);

  full_solver_mat.result_vector = reset_accel;
  full_solver_mat.desired_vector = reset_x;  // temporary misuse
  full_solver_mat.mult_aligned();
  full_solver_mat.result_vector = delta_accel;
  full_solver_mat.desired_vector = delta_f;

  IVP_VecFPU::fpu_add_multiple_row(reset_accel, full_b, -1.0f, aligned_size,
                                   IVP_TRUE);

  IVP_IF(debug_lcs) {
    for (i = 0; i < n_variables; i++) {
      IVP_DOUBLE diff = IVP_Inline_Math::fabsd(reset_accel[i] - accel[i]);
      if (diff > 0.001f) {
        ivp_message("accel_violated at %d %f should be %f\n", i, reset_accel[i],
                    accel[i]);
      }
      diff = IVP_Inline_Math::fabsd(full_x[i] - reset_x[i]);
      if (diff > 0.001f) {
        ivp_message("x_violated at %d\n", i);
      }
    }
  }
}

IVP_RETURN_TYPE IVP_Great_Matrix_Many_Zero::lu_crout(int *index_vec,
                                                     IVP_DOUBLE *sign) {
  /* use desired_vector to store scalings */

  int i, imax = 0, j, k, zeros = 0;
  IVP_DOUBLE sum;
  IVP_DOUBLE big, temp;

  int n_columns = this->columns;

  *sign = 1.0f;

  // Find implicit scaling factors

  for (i = 0; i < n_columns; i++) {
    big = 0.0f;
    for (j = 0; j < n_columns; j++) {
      temp =
          IVP_Inline_Math::fabsd((IVP_DOUBLE)matrix_values[i * n_columns + j]);
      if (temp > big) big = temp;
    }
    desired_vector[i] = (big == 0.0f ? 0.0f : 1.0f / big);
  }

  for (j = 0; j < n_columns; j++) {
    /*************************************
     Run down jth column from top to diag,
     to form the elements of U.
    **************************************/
    for (i = 0; i < j; i++) {
      sum = matrix_values[i * n_columns + j];
      for (k = 0; k < i; k++) {
        sum -=
            matrix_values[i * n_columns + k] * matrix_values[k * n_columns + j];
      }
      matrix_values[i * n_columns + j] = sum;
    }

    /******************************************
     Run down jth subdiag to form the residuals
     after the elimination of the first j-1
     subdiags.  These residuals divided by the
     appropriate diagonal term will become the
     multipliers in the elimination of the jth.
     subdiag. Find index of largest scaled term
     in imax.
    *******************************************/
    big = 0.0f;
    for (i = j; i < n_columns; i++) {
      sum = matrix_values[i * n_columns + j];
      for (k = 0; k < j; k++)
        sum -=
            matrix_values[i * n_columns + k] * matrix_values[k * n_columns + j];
      matrix_values[i * n_columns + j] = sum;
      temp = desired_vector[i] * (IVP_Inline_Math::fabsd(sum));
      if (temp >= big) {
        big = temp;
        imax = i;
      }
    }

    // Permute current row with imax

    if (j != imax) {
      IVP_VecFPU::fpu_exchange_rows(&matrix_values[imax * n_columns],
                                    &matrix_values[j * n_columns], n_columns,
                                    IVP_FALSE);
      *sign = -*sign;
      desired_vector[imax] = desired_vector[j];
    }
    index_vec[j] = imax;

    // If diag term is not zero divide subdiag to form multipliers.

    if (IVP_Inline_Math::fabsd(matrix_values[j * n_columns + j]) < MATRIX_EPS)
      zeros++;
    else if (j != n_columns - 1) {
      temp = 1.0f / matrix_values[j * n_columns + j];
      for (i = j + 1; i < n_columns; i++)
        matrix_values[i * n_columns + j] *= temp;
    }
  }
  if (zeros) {
    return IVP_FAULT;
  }
  return IVP_OK;
}

IVP_RETURN_TYPE IVP_Great_Matrix_Many_Zero::lu_inverse(
    IVP_Great_Matrix_Many_Zero *matrix_out, int *index_vec) {
  // solve the Identity matrix column for column to get inverse
  int i, j;
  for (i = 0; i < columns; i++) {
    IVP_VecFPU::fpu_set_row_to_zero(desired_vector, columns, IVP_TRUE);
    desired_vector[i] = 1.0f;

    if (this->lu_solve(index_vec) == IVP_OK) {
      for (j = 0; j < columns; j++) {
        matrix_out->matrix_values[j * columns + i] = this->result_vector[j];
      }
    } else {
      return IVP_FAULT;
    }
  }
  return IVP_OK;
}

IVP_RETURN_TYPE IVP_Great_Matrix_Many_Zero::lu_solve(int *index_vec) {
  int i, nonzero = -1, iperm, j;
  IVP_DOUBLE sum;

  int n_columns = this->columns;

  // check for zero diagonals

  for (i = 0; i < n_columns; i++) {
    if (IVP_Inline_Math::fabsd(matrix_values[i * n_columns + i]) < MATRIX_EPS) {
      return IVP_FAULT;
    }
  }

  // transform b allowing for leading zeros

  for (i = 0; i < n_columns; i++) {
    iperm = index_vec[i];
    sum = desired_vector[iperm];
    desired_vector[iperm] = desired_vector[i];
    if (nonzero >= 0)
      for (j = nonzero - 1; j <= i - 1; j++) {
        sum -= matrix_values[i * n_columns + j] * desired_vector[j];
      }
    else if (!hk_Math::almost_zero(sum))
      nonzero = i + 1;
    desired_vector[i] = sum;
  }

  // backsubstitution

  for (i = n_columns - 1; i >= 0; i--) {
    sum = desired_vector[i] - IVP_VecFPU::fpu_large_dot_product(
                                  &matrix_values[i * n_columns + i + 1],
                                  &desired_vector[i + 1], n_columns - i - 1,
                                  IVP_FALSE);
    desired_vector[i] = (sum / matrix_values[i * n_columns + i]);
    result_vector[i] =
        desired_vector[i];  // in place of desired_vector is possible!
  }

  return IVP_OK;
}

// destroys matrix this
// also uses and destroys this->desired_vector and this->result_vector
IVP_RETURN_TYPE IVP_Great_Matrix_Many_Zero::invert(
    IVP_Great_Matrix_Many_Zero *dest) {
  // NOTE: dest memory already has to be allocated

  IVP_ASSERT(dest->columns == this->columns);

#if defined(IVP_NO_ALLOCA)
  int index_vec[IVP_MAX_GREAT_MATRIX_SIZE];
  IVP_ASSERT(columns < IVP_MAX_GREAT_MATRIX_SIZE);
#else
  int *index_vec = (int *)alloca(columns * sizeof(int));
#endif

  IVP_DOUBLE sign;

  if (lu_crout(index_vec, &sign) == IVP_FAULT) {
    return IVP_FAULT;
  }

  return lu_inverse(dest, index_vec);
}

IVP_RETURN_TYPE IVP_Incr_L_U_Matrix::normize_row_L(int row_nr) {
  IVP_DOUBLE *base = &L_matrix[row_nr * aligned_row_len];
  IVP_DOUBLE val = base[row_nr];
  if (IVP_Inline_Math::fabsd(val) < MATRIX_EPS) {
    return IVP_FAULT;
  }
  IVP_DOUBLE inv_val = 1.0f / val;

  IVP_VecFPU::fpu_multiply_row(&base[0], inv_val, n_sub, IVP_TRUE);

  base[row_nr] = 1.0f;
  return IVP_OK;
}

IVP_RETURN_TYPE IVP_Incr_L_U_Matrix::normize_row(int row_nr) {
  int row_base = row_nr * aligned_row_len;
  IVP_DOUBLE val = U_matrix[row_base + row_nr];
  if (IVP_Inline_Math::fabsd(val) < MATRIX_EPS) {
    return IVP_FAULT;
  }
  IVP_DOUBLE inv_val = 1.0f / val;
  IVP_VecFPU::fpu_multiply_row(&L_matrix[row_base], inv_val, n_sub, IVP_TRUE);
  IVP_VecFPU::fpu_multiply_row(&U_matrix[row_base + row_nr + 1], inv_val,
                               n_sub - row_nr - 1, IVP_FALSE);

  U_matrix[row_base + row_nr] = 1.0f;
  return IVP_OK;
}

void IVP_Incr_L_U_Matrix::exchange_rows_l_u(int pivot_col, int exchange) {
  int base_a = pivot_col * aligned_row_len;
  int base_b = exchange * aligned_row_len;

  IVP_VecFPU::fpu_exchange_rows(&U_matrix[base_a + pivot_col],
                                &U_matrix[base_b + pivot_col],
                                n_sub - pivot_col, IVP_FALSE);
  IVP_VecFPU::fpu_exchange_rows(&L_matrix[base_a], &L_matrix[base_b], n_sub,
                                IVP_TRUE);
}

// search from col_nr|colnr down to n_sub|colnr
void IVP_Incr_L_U_Matrix::pivot_search_l_u(int col_nr) {
  IVP_DOUBLE biggest =
      IVP_Inline_Math::fabsd(U_matrix[col_nr * aligned_row_len + col_nr]);
  int pivot_row = col_nr;

  for (int i = n_sub - 1; i > col_nr; i--) {
    IVP_DOUBLE new_val =
        IVP_Inline_Math::fabsd(U_matrix[i * aligned_row_len + col_nr]);
    if (new_val > biggest) {
      pivot_row = i;
      biggest = new_val;
    }
  }

  if (pivot_row != col_nr) {
    exchange_rows_l_u(col_nr, pivot_row);
  }
}

// add row to a row more upwards
void IVP_Incr_L_U_Matrix::add_neg_row_upwards_l_u(int row_down, int dest_row,
                                                  IVP_DOUBLE factor) {
  IVP_DOUBLE *base_a = &U_matrix[row_down * aligned_row_len];
  IVP_DOUBLE *base_b = &U_matrix[dest_row * aligned_row_len];
  IVP_VecFPU::fpu_add_multiple_row(&base_b[row_down + 1], &base_a[row_down + 1],
                                   -factor, n_sub - row_down - 1, IVP_FALSE);

  base_a = &L_matrix[row_down * aligned_row_len];
  base_b = &L_matrix[dest_row * aligned_row_len];
  IVP_VecFPU::fpu_add_multiple_row(&base_b[0], &base_a[0], -factor, n_sub,
                                   IVP_TRUE);
  base_b[row_down] = 0.0f;
}

// add row to a row more downwards
void IVP_Incr_L_U_Matrix::add_neg_row_to_row_l_u(int pivot_row, int dest_row,
                                                 IVP_DOUBLE factor) {
  int base_a = pivot_row * aligned_row_len;
  int base_b = dest_row * aligned_row_len;

  IVP_VecFPU::fpu_add_multiple_row(&U_matrix[base_b + pivot_row + 1],
                                   &U_matrix[base_a + pivot_row + 1], -factor,
                                   n_sub - pivot_row - 1, IVP_FALSE);
  IVP_VecFPU::fpu_add_multiple_row(&L_matrix[base_b], &L_matrix[base_a],
                                   -factor, n_sub, IVP_TRUE);
  U_matrix[base_b + pivot_row] = 0.0f;
}

void IVP_Incr_L_U_Matrix::subtract_row_L(int from, int dest,
                                         IVP_DOUBLE factor) {
  IVP_DOUBLE *base_a = &L_matrix[from * aligned_row_len];
  IVP_DOUBLE *base_b = &L_matrix[dest * aligned_row_len];
  IVP_VecFPU::fpu_add_multiple_row(&base_b[0], &base_a[0], -factor, n_sub,
                                   IVP_TRUE);

  base_b[from] = 0.0f;
}

IVP_RETURN_TYPE IVP_Incr_L_U_Matrix::l_u_decomposition_with_pivoting() {
  if (n_sub == 0) {
    return IVP_OK;
  }

  int i;
  for (i = n_sub - 1; i >= 0; i--) {
    IVP_DOUBLE *base = &L_matrix[i * aligned_row_len];
    IVP_VecFPU::fpu_set_row_to_zero(&base[0], n_sub, IVP_TRUE);
    base[i] = 1.0f;
  }

  int rows_to_do;
  for (rows_to_do = 1; rows_to_do < n_sub; rows_to_do++) {
    // debug_print_l_u();
    int pivot_elem = rows_to_do - 1;
    pivot_search_l_u(pivot_elem);
    // L_matrix[pivot_elem * n_max + pivot_elem] = 1.0f;
    if (normize_row(pivot_elem) == IVP_FAULT) {
      return IVP_FAULT;
    }

    for (int rows_become_zero = n_sub - 1; rows_become_zero >= rows_to_do;
         rows_become_zero--) {
      IVP_DOUBLE factor =
          U_matrix[rows_become_zero * aligned_row_len + pivot_elem];
      if (factor != 0.0f) {  // original matrix may be filled with many zeros
        add_neg_row_to_row_l_u(pivot_elem, rows_become_zero, factor);
      }
    }
  }
  // L_matrix[ (n_sub-1)*n_max + n_sub-1 ] = 1.0f;
  if (normize_row(n_sub - 1) == IVP_FAULT) {
    return IVP_FAULT;
  }
  // debug_print_l_u();
  return IVP_OK;
}

// incremental l_u_decomposition: matrix is correctly decomposed to n_sub*n_sub
// last row of new U was already added, row has length n_sub+1
// last column of new U is stored in input_vec, column has length n_sub
IVP_RETURN_TYPE IVP_Incr_L_U_Matrix::increment_l_u() {
  int i;
  IVP_VecFPU::fpu_copy_rows(mult_vec, input_vec, n_sub, IVP_TRUE);

  mult_vec_with_L();

  for (i = n_sub - 1; i >= 0; i--) {
    U_matrix[i * aligned_row_len + n_sub] = temp_vec[i];
  }

  // clear row and column of L
  for (i = n_sub - 1; i >= 0; i--) {
    L_matrix[i * aligned_row_len + n_sub] = 0.0f;
  }
  IVP_VecFPU::fpu_set_row_to_zero(&L_matrix[n_sub * aligned_row_len], n_sub,
                                  IVP_TRUE);

  L_matrix[n_sub * aligned_row_len + n_sub] = 1.0f;

  // inv_index_pos_contains[ n_sub ] = n_sub;
  // index_pos_contains[ n_sub ] = n_sub;

  int base_row = n_sub * aligned_row_len;

  n_sub++;
  for (i = 0; i < n_sub - 1; i++) {
    IVP_DOUBLE factor = U_matrix[base_row + i];
    add_neg_row_to_row_l_u(i, n_sub - 1, factor);
  }

  IVP_RETURN_TYPE ret = normize_row(n_sub - 1);
  // debug_print_l_u();
  return ret;
}

// variable with nr del_nr is removed from linear equation systems
// first U is transformed in a way only 0 0 1 0 0 is found in the row
// then L is transformed in a way only 0 0 1 0 0 is found in the column
// then column and row is deleted from L and U
IVP_RETURN_TYPE IVP_Incr_L_U_Matrix::decrement_l_u(int del_nr) {
  int i;
  // exchange_rows_l_u(del_nr,n_sub-1);
  exchange_columns_L(del_nr, n_sub - 1);
  exchange_columns_U(del_nr, n_sub - 1);

  for (i = n_sub - 2; i > del_nr; i--) {
    IVP_DOUBLE factor = U_matrix[i * aligned_row_len + del_nr];
    if (factor != 0.0f) {
      add_neg_row_L(n_sub - 1, i, factor);
    }
    U_matrix[i * aligned_row_len + del_nr] = 0.0f;
  }
  if (normize_row(del_nr) != IVP_OK) {
    // ivp_message("special_case_decrement\n");
    add_neg_row_L(n_sub - 1, del_nr, -1.0f);
    U_matrix[del_nr * aligned_row_len + del_nr] = 1.0f;
  }

  for (i = del_nr; i < n_sub - 1; i++) {
    IVP_DOUBLE factor = U_matrix[(n_sub - 1) * aligned_row_len + i];
    if (factor != 0.0f) {
      add_neg_row_to_row_l_u(i, n_sub - 1, factor);
    }
  }
  // normize_row(n_sub-1);

  if (normize_row_L(n_sub - 1) != IVP_OK) {
    n_sub--;
    return IVP_FAULT;
  }

  del_nr = n_sub - 1;
  for (i = del_nr - 1; i >= 0; i--) {
    IVP_DOUBLE factor = L_matrix[i * aligned_row_len + del_nr];
    if (factor != 0.0f) {
      subtract_row_L(del_nr, i, factor);
    }
  }

  n_sub--;
  return IVP_OK;
}

void IVP_Incr_L_U_Matrix::add_neg_col_L(int col0, int col_dest,
                                        IVP_DOUBLE factor) {
  // Vector FPU not possible (strided access)
  for (int i = n_sub - 1; i >= 0; i--) {
    L_matrix[i * aligned_row_len + col_dest] -=
        factor * L_matrix[i * aligned_row_len + col0];
  }
}

void IVP_Incr_L_U_Matrix::exchange_columns_L(int col0, int col1) {
  // Vector FPU not possible (strided access)
  for (int i = 0; i < n_sub; i++) {
    IVP_DOUBLE* row = &L_matrix[i * aligned_row_len];
    std::swap(row[col0], row[col1]);
  }
}

void IVP_Incr_L_U_Matrix::exchange_columns_U(int col0, int col1) {
  // Vector FPU not possible (strided access)
  for (int i = 0; i < n_sub; i++) {
    IVP_DOUBLE* row = &U_matrix[i * aligned_row_len];
    std::swap(row[col0], row[col1]);
  }
}

void IVP_Incr_L_U_Matrix::mult_vec_with_L() {
  int i;
  for (i = n_sub - 1; i >= 0; i--) {
    IVP_DOUBLE sum = IVP_VecFPU::fpu_large_dot_product(
        &L_matrix[i * aligned_row_len], &mult_vec[0], n_sub, IVP_TRUE);
    temp_vec[i] = sum;
  }
}

void IVP_Incr_L_U_Matrix::solve_vec_with_U() {
  // dimhotepus: Back substitution, U has unit diagonal.
  // The old vectorised attempt here ("doesnt work, but why?") failed because
  // the aligned kernels rounded the range down/up and so multiplied stale
  // entries beyond n_sub; the exact-range dot product has no such problem.
  for (int i = n_sub - 1; i >= 0; i--) {
    IVP_DOUBLE leftsum = IVP_VecFPU::fpu_large_dot_product(
        &U_matrix[i * aligned_row_len + i + 1], &temp_vec[i + 1], n_sub - i - 1,
        IVP_FALSE);
    IVP_DOUBLE res = temp_vec[i] - leftsum;
    temp_vec[i] = res;
    out_vec[i] = res;
  }
}

void IVP_Incr_L_U_Matrix::solve_lin_equ() {
  IVP_VecFPU::fpu_copy_rows(mult_vec, input_vec, n_sub, IVP_TRUE);
  mult_vec_with_L();
  solve_vec_with_U();
}

void IVP_Incr_L_U_Matrix::add_neg_row_L(int source_row, int dest_row,
                                        IVP_DOUBLE factor) {
  int base_a = source_row * aligned_row_len;
  int base_b = dest_row * aligned_row_len;

  IVP_VecFPU::fpu_add_multiple_row(&L_matrix[base_b], &L_matrix[base_a],
                                   -factor, n_sub, IVP_TRUE);
}

void IVP_Incr_L_U_Matrix::debug_print_l_u() {
  int i;
  ivp_message("  L                                      U\n");
  for (i = 0; i < n_sub; i++) {
    int j;
    for (j = 0; j < n_sub; j++) {
      ivp_message("%.5f  ", L_matrix[i * aligned_row_len + j]);
    }
    ivp_message("          ");
    for (j = 0; j < n_sub; j++) {
      ivp_message("%.5f  ", U_matrix[i * aligned_row_len + j]);
    }
    ivp_message("\n");
  }
}

void IVP_Incr_L_U_Matrix::debug_print_a() {
  IVP_Great_Matrix_Many_Zero was_l(n_sub);
  IVP_Great_Matrix_Many_Zero was_u(n_sub);

  for (int i = 0; i < n_sub; i++) {
    for (int j = 0; j < n_sub; j++) {
      was_l.matrix_values[i * n_sub + j] = L_matrix[i * aligned_row_len + j];
      was_u.matrix_values[i * n_sub + j] = U_matrix[i * aligned_row_len + j];
    }
  }

  IVP_Great_Matrix_Many_Zero inv_l(n_sub);
  was_l.invert(&inv_l);

  IVP_Great_Matrix_Many_Zero new_a(n_sub);
  new_a.matrix_multiplication(inv_l.matrix_values, was_u.matrix_values);

  new_a.print_great_matrix("orig_A");
}

// when matrix is singular give dependences of a variable
void IVP_Linear_Constraint_Solver::debug_dep_var(int full_var_nr) {
  int col_nr = 0;
  int sub_var_nr = variable_is_found_at[full_var_nr];
  if (sub_var_nr >= r_actives) {
    ivp_message("works_only_with_active\n");
    return;
  }
  int i, j;
  sub_solver_mat.columns = r_actives;
  sub_solver_mat.calc_aligned_row_len();

  for (i = 0; i < sub_var_nr; i++) {
    for (j = 0; j < r_actives; j++) {
      int full_i, full_j;
      full_i = actives_inactives_ignored[i];
      full_j = actives_inactives_ignored[j];
      sub_solver_mat
          .matrix_values[j * sub_solver_mat.aligned_row_len + col_nr] =
          full_solver_mat.matrix_values[full_j * aligned_size + full_i];
    }
    col_nr++;
  }
  for (i = sub_var_nr + 1; i < r_actives; i++) {
    for (j = 0; j < r_actives; j++) {
      int full_i, full_j;
      full_i = actives_inactives_ignored[i];
      full_j = actives_inactives_ignored[j];
      sub_solver_mat
          .matrix_values[j * sub_solver_mat.aligned_row_len + col_nr] =
          full_solver_mat.matrix_values[full_j * aligned_size + full_i];
    }
    col_nr++;
  }
  for (j = 0; j < r_actives; j++) {
    sub_solver_mat.matrix_values[j * r_actives + r_actives - 1] = 0.0f;
    int full_i, full_j;
    full_i = actives_inactives_ignored[sub_var_nr];
    full_j = actives_inactives_ignored[j];
    sub_solver_mat.desired_vector[j] =
        full_solver_mat.matrix_values[full_j * aligned_size + full_i];
  }

  if (sub_solver_mat.solve_great_matrix_many_zero() == IVP_OK) {
    ivp_message("dependency %d is ", full_var_nr);
    col_nr = 0;
    for (i = 0; i < sub_var_nr; i++) {
      ivp_message("%d:%f ", actives_inactives_ignored[i],
                  sub_solver_mat.result_vector[col_nr]);
      col_nr++;
    }
    for (i = sub_var_nr + 1; i < r_actives; i++) {
      ivp_message("%d:%f ", actives_inactives_ignored[i],
                  sub_solver_mat.result_vector[col_nr]);
      col_nr++;
    }
    ivp_message("\n");
  } else {
    ivp_message("variable %d is independent\n", full_var_nr);
  }
}