#include "gtest/gtest.h"

#include "Models/StateSpace/Filters/SparseMatrix.hpp"

#include "test_utils/test_utils.hpp"

namespace {

  using namespace BOOM;
  using std::endl;

  class SparseMatrixTest : public ::testing::Test {
   protected:
    SparseMatrixTest() {
      GlobalRng::rng.seed(8675309);
    }
  };
  
  void CheckSparseMatrixBlock(
      const Ptr<SparseMatrixBlock> &sparse,
      const Matrix &dense) {
    EXPECT_TRUE(MatrixEquals(sparse->dense(), dense));
    
    EXPECT_EQ(sparse->nrow(), dense.nrow())
        << endl << sparse->dense() << endl << dense;
    EXPECT_EQ(sparse->ncol(), dense.ncol())
        << endl << sparse->dense() << endl << dense;

    Vector rhs_vector(dense.ncol());
    rhs_vector.randomize();
    Vector lhs_vector(dense.nrow());
    sparse->multiply(VectorView(lhs_vector), rhs_vector);
    EXPECT_TRUE(VectorEquals(lhs_vector, dense * rhs_vector))
        << endl << sparse->dense() << endl << dense << endl
        << "rhs = " << rhs_vector << endl
        << "sparse * rhs = " << lhs_vector << endl
        << "dense * rhs = " << dense * rhs_vector << endl;

    Vector original_lhs = lhs_vector;
    lhs_vector.randomize();
    rhs_vector.randomize();
    sparse->multiply_and_add(VectorView(lhs_vector), rhs_vector);
    EXPECT_TRUE(VectorEquals(lhs_vector, original_lhs + dense * rhs_vector))
        << endl << sparse->dense() << endl << dense << endl
        << "rhs = " << rhs_vector << endl
        << "lhs = " << original_lhs << endl
        << "lhs + sparse * rhs = " << lhs_vector << endl
        << "lhs + dense * rhs = " << lhs_vector + dense * rhs_vector << endl;

    Vector rhs_tmult_vector(dense.nrow());
    Vector lhs_tmult_vector(dense.ncol());
    sparse->Tmult(VectorView(lhs_tmult_vector), rhs_tmult_vector);
    EXPECT_TRUE(VectorEquals(lhs_tmult_vector,
                             rhs_tmult_vector * dense));
    
    // Only check multiply_inplace and friends if the matrix is square.
    if (dense.nrow() == dense.ncol()) {
      Vector original_rhs = rhs_vector;
      sparse->multiply_inplace(VectorView(rhs_vector));
      EXPECT_TRUE(VectorEquals(rhs_vector, dense * original_rhs))
        << endl << sparse->dense() << endl << dense << endl
        << "rhs = " << original_rhs << endl
        << "sparse->multiply_inplace(rhs) = " << rhs_vector << endl
        << "dense * rhs = " << dense * original_rhs << endl;

      Matrix rhs_matrix(dense.ncol(), dense.ncol());
      rhs_matrix.randomize();
      Matrix original_rhs_matrix = rhs_matrix;
      sparse->matrix_multiply_inplace(SubMatrix(rhs_matrix));
      EXPECT_TRUE(MatrixEquals(rhs_matrix, dense * original_rhs_matrix))
        << endl << sparse->dense() << endl << dense << endl
        << "rhs = " << original_rhs_matrix << endl
        << "sparse->matrix_multiply_inplace(rhs) = " << rhs_matrix << endl
        << "dense * rhs = " << dense * original_rhs_matrix << endl;

      original_rhs_matrix = rhs_matrix;
      sparse->matrix_transpose_premultiply_inplace(SubMatrix(rhs_matrix));
      EXPECT_TRUE(MatrixEquals(rhs_matrix,
                               dense.transpose() * original_rhs_matrix))
        << endl << sparse->dense() << endl << dense << endl
        << "rhs = " << original_rhs_matrix << endl
        << "sparse->matrix_transpose_multiply_inplace(rhs) = " << rhs_matrix << endl
        << "dense * rhs = " << dense.transpose() * original_rhs_matrix << endl;
    }

    Matrix summand(dense.nrow(), dense.ncol());
    summand.randomize();
    Matrix original_summand(summand);
    sparse->add_to(SubMatrix(summand));
    EXPECT_TRUE(MatrixEquals(summand, dense + original_summand))
        << endl << sparse->dense() << endl << dense << endl
        << "B = " << original_summand << endl
        << "sparse->add_to(B) = " << summand << endl
        << "dense + B = " << dense + original_summand << endl;
  }
  
  void CheckLeftInverse(const Ptr<SparseMatrixBlock> &block,
                        const Vector &rhs) {
    BlockDiagonalMatrix mat;
    mat.add_block(block);
    
    Vector lhs = mat.left_inverse(rhs);
    Vector rhs_new = mat * lhs;

    EXPECT_TRUE(VectorEquals(rhs, rhs_new))
        << "Vectors were not equal." << endl
        << rhs << endl
        << rhs_new;
  }
  
  TEST_F(SparseMatrixTest, LeftInverseIdentity) {
    NEW(IdentityMatrix, mat)(3);
    Vector x(3);
    x.randomize();
    CheckLeftInverse(mat, x);
  }

  TEST_F(SparseMatrixTest, LeftInverseSkinnyColumn) {
    NEW(FirstElementSingleColumnMatrix, column)(12);
    Vector errors(1);
    errors.randomize();
    Vector x(12);
    x[0] = errors[0];
    CheckLeftInverse(column, x);
  }

  TEST_F(SparseMatrixTest, IdentityMatrix) {
    NEW(IdentityMatrix, I3)(3);
    SpdMatrix I3_dense(3, 1.0);
    CheckSparseMatrixBlock(I3, I3_dense);

    NEW(IdentityMatrix, I1)(1);
    SpdMatrix I1_dense(1, 1.0);
    CheckSparseMatrixBlock(I1, I1_dense);
  }

  TEST_F(SparseMatrixTest, LocalTrend) {
    NEW(LocalLinearTrendMatrix, T)();
    Matrix Tdense = T->dense();
    EXPECT_TRUE(VectorEquals(Tdense.row(0), Vector{1, 1}));
    EXPECT_TRUE(VectorEquals(Tdense.row(1), Vector{0, 1}));
    CheckSparseMatrixBlock(T, Tdense);
  }

  TEST_F(SparseMatrixTest, DenseMatrixTest) {
    Matrix square(4, 4);
    square.randomize();
    NEW(DenseMatrix, square_kalman)(square);
    CheckSparseMatrixBlock(square_kalman, square);

    Matrix rectangle(3, 4);
    rectangle.randomize();
    NEW(DenseMatrix, rectangle_kalman)(rectangle);
    CheckSparseMatrixBlock(rectangle_kalman, rectangle);
  }

  TEST_F(SparseMatrixTest, SpdTest) {
    SpdMatrix spd(3);
    spd.randomize();

    NEW(DenseSpd, spd_kalman)(spd);
    CheckSparseMatrixBlock(spd_kalman, spd);

    NEW(SpdParams, sparams)(spd);
    NEW(DenseSpdParamView, spd_view)(sparams);
    CheckSparseMatrixBlock(spd_view, spd);
  }

  TEST_F(SparseMatrixTest, Diagonal) {
    Vector values(4);
    values.randomize();
    
    NEW(DiagonalMatrixBlock, diag)(values);
    Matrix D(4, 4, 0.0);
    D.set_diag(values);

    CheckSparseMatrixBlock(diag, D);

    NEW(VectorParams, vprm)(values);
    NEW(DiagonalMatrixBlockVectorParamView, diag_view)(vprm);
    CheckSparseMatrixBlock(diag_view, D);
  }

  TEST_F(SparseMatrixTest, Seasonal) {
    NEW(SeasonalStateSpaceMatrix, seasonal)(4);
    Matrix seasonal_dense(4, 4, 0.0);
    seasonal_dense.row(0) = -1;
    seasonal_dense.subdiag(1) = 1.0;

    CheckSparseMatrixBlock(seasonal, seasonal_dense);
  }

  TEST_F(SparseMatrixTest, AutoRegression) {
    Vector elements(4);
    elements.randomize();
    NEW(GlmCoefs, rho)(elements);
    NEW(AutoRegressionTransitionMatrix, rho_kalman)(rho);
    Matrix rho_dense(4, 4);
    rho_dense.row(0) = elements;
    rho_dense.subdiag(1) = 1.0;

    CheckSparseMatrixBlock(rho_kalman, rho_dense);
  }

  TEST_F(SparseMatrixTest, EmptyTest) {
    Matrix empty;
    NEW(EmptyMatrix, empty_kalman)();
    CheckSparseMatrixBlock(empty_kalman, empty);
  }

  TEST_F(SparseMatrixTest, ConstantTest) {
    SpdMatrix dense(4, 8.7);
    NEW(ConstantMatrix, sparse)(4, 8.7);
    CheckSparseMatrixBlock(sparse, dense);

    NEW(UnivParams, prm)(8.7);
    NEW(ConstantMatrixParamView, sparse_view)(4, prm);
    CheckSparseMatrixBlock(sparse_view, dense);
  }

  TEST_F(SparseMatrixTest, ZeroTest) {
    NEW(ZeroMatrix, sparse)(7);
    Matrix dense(7, 7, 0.0);
    CheckSparseMatrixBlock(sparse, dense);
  }

  TEST_F(SparseMatrixTest, ULC) {
    NEW(UpperLeftCornerMatrix, sparse)(5, 19.2);
    Matrix dense(5, 5, 0.0);
    dense(0, 0) = 19.2;
    CheckSparseMatrixBlock(sparse, dense);

    NEW(UnivParams, prm)(19.2);
    NEW(UpperLeftCornerMatrixParamView, sparse_view)(5, prm);
    CheckSparseMatrixBlock(sparse_view, dense);
  }

  TEST_F(SparseMatrixTest, FirstElementSingleColumnMatrixTest) {
    NEW(FirstElementSingleColumnMatrix, sparse)(7);
    Matrix dense(7, 1, 0.0);
    dense(0, 0) = 1.0;
    CheckSparseMatrixBlock(sparse, dense);
  }

  TEST_F(SparseMatrixTest, ZeroPaddedIdTest) {
    NEW(ZeroPaddedIdentityMatrix, sparse)(20, 4);
    Matrix dense(20, 4);
    dense.set_diag(1.0);
    CheckSparseMatrixBlock(sparse, dense);
  }

  TEST_F(SparseMatrixTest, SingleSparseDiagonalElementMatrixTest) {
    NEW(SingleSparseDiagonalElementMatrix, sparse)(12, 18.7, 5);
    Matrix dense(12, 12, 0.0);
    dense(5, 5) = 18.7;
    CheckSparseMatrixBlock(sparse, dense);

    NEW(UnivParams, prm)(18.7);
    NEW(SingleSparseDiagonalElementMatrixParamView, sparse_view)(12, prm, 5);
    CheckSparseMatrixBlock(sparse_view, dense);
  }

  TEST_F(SparseMatrixTest, SingleElementInFirstRowTest) {
    NEW(SingleElementInFirstRow, sparse_square)(5, 5, 3, 12.9);
    Matrix dense(5, 5, 0.0);
    dense(0, 3) = 12.9;
    CheckSparseMatrixBlock(sparse_square, dense);

    NEW(SingleElementInFirstRow, sparse_rectangle)(5, 8, 0, 99.99);
    Matrix wide(5, 8, 0.0);
    wide(0, 0) = 99.99;
    CheckSparseMatrixBlock(sparse_rectangle, wide);

    NEW(SingleElementInFirstRow, sparse_tall)(20, 4, 2, 13.7);
    Matrix tall(20, 4, 0.0);
    tall(0, 2) = 13.7;
    CheckSparseMatrixBlock(sparse_tall, tall);
  }

  TEST_F(SparseMatrixTest, UpperLeftDiagonalTest) {
    std::vector<Ptr<UnivParams>> params;
    params.push_back(new UnivParams(3.2));
    params.push_back(new UnivParams(1.7));
    params.push_back(new UnivParams(-19.8));

    NEW(UpperLeftDiagonalMatrix, sparse)(params, 17);
    Matrix dense(17, 17, 0.0);
    for (int i = 0; i < params.size(); ++i) {
      dense(i, i) = params[i]->value();
    }
    CheckSparseMatrixBlock(sparse, dense);

    Vector scale_factor(3);
    scale_factor.randomize();
    dense.diag() *= scale_factor;
    NEW(UpperLeftDiagonalMatrix, sparse2)(params, 17, scale_factor);
    CheckSparseMatrixBlock(sparse2, dense);
  }

  TEST_F(SparseMatrixTest, IdenticalRowsMatrixTest) {
    SparseVector row(20);
    row[0] = 8;
    row[17] = 6;
    row[12] = 7;
    row[9] = 5;
    row[3] = 3;
    row[1] = 0;
    row[2] = 9;
    NEW(IdenticalRowsMatrix, sparse)(row, 20);
    Matrix dense(20, 20, 0.0);
    dense.col(0) = 8;
    dense.col(17) = 6;
    dense.col(12) = 7;
    dense.col(9) = 5;
    dense.col(3) = 3;
    dense.col(1) = 0;
    dense.col(2) = 9;
    CheckSparseMatrixBlock(sparse, dense);
  }

  Matrix ConstraintMatrix(int dim) {
    Matrix ans(dim, dim, 0.0);
    ans.set_diag(1.0);
    return ans - Matrix(dim, dim, 1.0 / dim);
  }
  
  TEST_F(SparseMatrixTest, EffectConstrainedMatrixBlockTest) {
    NEW(SeasonalStateSpaceMatrix, seasonal)(12);
    NEW(EffectConstrainedMatrixBlock, constrained_seasonal)(
        seasonal);
    CheckSparseMatrixBlock(constrained_seasonal,
                           seasonal->dense() * ConstraintMatrix(11));

  }

  TEST_F(SparseMatrixTest, GenericSparseMatrixBlockTest) {
    NEW(GenericSparseMatrixBlock, sparse)(12, 18);
    (*sparse)(3, 7) = 19;
    (*sparse)(5, 2) = -4;

    Matrix dense(12, 18, 0.0);
    dense(3, 7) = 19;
    dense(5, 2) = -4;
    CheckSparseMatrixBlock(sparse, dense);
  }
  
}  // namespace
