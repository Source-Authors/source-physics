#ifndef HK_GAUSS_ELEMINATION
#define HK_GAUSS_ELEMINATION

typedef float hk_gauss_real;

#define hk_GAUSS_SOLUTION_TOLERANCE (1000.0f)  // factor to hk_GAUSS_ELM_EPS

class hk_Gauss_Elm_Input {
 public:
  hk_gauss_real m_gauss_eps;
  hk_gauss_real
      *m_A;  // N*N Matrix, organized by rows - rows have to be memory aligned
  hk_gauss_real *m_b;  // vector, memory aligned

  int m_n_columns;
  int m_aligned_row_len;  // number of elements (not Bytes!) aligned per row
};

class hk_Gauss_Elm_Output {
 public:
  hk_gauss_real *m_result;  // vector, memory aligned
};

class hk_Gauss_Elm_Solver {
  hk_gauss_real m_gauss_eps;
  hk_gauss_real
      *m_A;  // N*N Matrix, organized by rows - rows have to be memory aligned
  hk_gauss_real *m_b;  // vector, memory aligned
  int m_n_columns;
  int m_aligned_row_len;    // number of elements (not Bytes!) aligned per row
  hk_gauss_real *m_result;  // vector, memory aligned

  void transform_to_lower_null_triangle();  // with gauss elemination algo.
  hk_result solve_lower_null_matrix();
  void add_multiple_line(int first, int second, hk_gauss_real factor);
  void exchange_rows(int a, int b);
  void find_pivot_in_column(int j);

 public:
  hk_result solve_gauss_elemination(hk_Gauss_Elm_Input &,
                                    hk_Gauss_Elm_Output &);
};

#endif  // HK_GAUSS_ELEMINATION