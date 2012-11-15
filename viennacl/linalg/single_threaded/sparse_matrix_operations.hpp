#ifndef VIENNACL_LINALG_SINGLE_THREADED_SPARSE_MATRIX_OPERATIONS_HPP_
#define VIENNACL_LINALG_SINGLE_THREADED_SPARSE_MATRIX_OPERATIONS_HPP_

/* =========================================================================
   Copyright (c) 2010-2012, Institute for Microelectronics,
                            Institute for Analysis and Scientific Computing,
                            TU Wien.

                            -----------------
                  ViennaCL - The Vienna Computing Library
                            -----------------

   Project Head:    Karl Rupp                   rupp@iue.tuwien.ac.at
               
   (A list of authors and contributors can be found in the PDF manual)

   License:         MIT (X11), see file LICENSE in the base directory
============================================================================= */

/** @file viennacl/linalg/single_threaded/sparse_matrix_operations.hpp
    @brief Implementations of operations using sparse matrices on the CPU using a single thread
*/

#include <list>

#include "viennacl/forwards.h"
#include "viennacl/scalar.hpp"
#include "viennacl/vector.hpp"
#include "viennacl/tools/tools.hpp"
#include "viennacl/linalg/single_threaded/common.hpp"

namespace viennacl
{
  namespace linalg
  {
    namespace single_threaded
    {
      //
      // Compressed matrix
      //
      
      namespace detail
      {
        template<typename ScalarType, unsigned int MAT_ALIGNMENT, unsigned int VEC_ALIGNMENT>
        void row_info(compressed_matrix<ScalarType, MAT_ALIGNMENT> const & mat,
                      vector<ScalarType, VEC_ALIGNMENT> & vec,
                      viennacl::linalg::detail::row_info_types info_selector)
        {
          ScalarType         * result_buf = detail::extract_raw_pointer<ScalarType>(vec.handle());
          ScalarType   const * elements   = detail::extract_raw_pointer<ScalarType>(mat.handle());
          unsigned int const * row_buffer = detail::extract_raw_pointer<unsigned int>(mat.handle1());
          unsigned int const * col_buffer = detail::extract_raw_pointer<unsigned int>(mat.handle2());
          
          for (std::size_t row = 0; row < mat.size1(); ++row)
          {
            ScalarType value = 0;
            unsigned int row_end = row_buffer[row+1];
            
            switch (info_selector)
            {
              case viennacl::linalg::detail::SPARSE_ROW_NORM_INF: //inf-norm
                for (unsigned int i = row_buffer[row]; i < row_end; ++i)
                  value = std::max<ScalarType>(value, std::fabs(elements[i]));
                break;
                
              case viennacl::linalg::detail::SPARSE_ROW_NORM_1: //1-norm
                for (unsigned int i = row_buffer[row]; i < row_end; ++i)
                  value += std::fabs(elements[i]);
                break;
                
              case viennacl::linalg::detail::SPARSE_ROW_NORM_2: //2-norm
                for (unsigned int i = row_buffer[row]; i < row_end; ++i)
                  value += elements[i] * elements[i];
                value = std::sqrt(value);
                break;
                
              case viennacl::linalg::detail::SPARSE_ROW_DIAGONAL: //diagonal entry
                for (unsigned int i = row_buffer[row]; i < row_end; ++i)
                {
                  if (col_buffer[i] == row)
                  {
                    value = elements[i];
                    break;
                  }
                }
                break;
                
              default:
                break;
            }
            result_buf[row] = value;
          }
        }
      }
      
      
      /** @brief Carries out matrix-vector multiplication with a compressed_matrix
      *
      * Implementation of the convenience expression result = prod(mat, vec);
      *
      * @param mat    The matrix
      * @param vec    The vector
      * @param result The result vector
      */
      template<class ScalarType, unsigned int ALIGNMENT, unsigned int VECTOR_ALIGNMENT>
      void prod_impl(const viennacl::compressed_matrix<ScalarType, ALIGNMENT> & mat, 
                     const viennacl::vector<ScalarType, VECTOR_ALIGNMENT> & vec,
                           viennacl::vector<ScalarType, VECTOR_ALIGNMENT> & result)
      {
        ScalarType         * result_buf = detail::extract_raw_pointer<ScalarType>(result.handle());
        ScalarType   const * vec_buf    = detail::extract_raw_pointer<ScalarType>(vec.handle());
        ScalarType   const * elements   = detail::extract_raw_pointer<ScalarType>(mat.handle());
        unsigned int const * row_buffer = detail::extract_raw_pointer<unsigned int>(mat.handle1());
        unsigned int const * col_buffer = detail::extract_raw_pointer<unsigned int>(mat.handle2());
        
        for (std::size_t row = 0; row < mat.size1(); ++row)
        {
          ScalarType dot_prod = 0;
          std::size_t row_end = row_buffer[row+1];
          for (std::size_t i = row_buffer[row]; i < row_end; ++i)
            dot_prod += elements[i] * vec_buf[col_buffer[i]];
          result_buf[row] = dot_prod;
        }
        
      }
      
      //
      // Triangular solve for compressed_matrix, A \ b
      //
      namespace detail
      {
        template <typename NumericT, typename ConstScalarTypeArray, typename ScalarTypeArray, typename SizeTypeArray>
        void csr_inplace_solve(SizeTypeArray const & row_buffer,
                               SizeTypeArray const & col_buffer,
                               ConstScalarTypeArray const & element_buffer,
                               ScalarTypeArray & vec_buffer,
                               std::size_t num_cols,
                               viennacl::linalg::unit_lower_tag)
        {
          std::size_t row_begin = row_buffer[1];
          for (std::size_t row = 1; row < num_cols; ++row)
          {
            NumericT vec_entry = vec_buffer[row];
            std::size_t row_end = row_buffer[row+1];
            for (std::size_t i = row_begin; i < row_end; ++i)
            {
              std::size_t col_index = col_buffer[i];
              if (col_index < row)
                vec_entry -= vec_buffer[col_index] * element_buffer[i];
            }
            vec_buffer[row] = vec_entry;
            row_begin = row_end;
          }
        }
        
        template <typename NumericT, typename ConstScalarTypeArray, typename ScalarTypeArray, typename SizeTypeArray>
        void csr_inplace_solve(SizeTypeArray const & row_buffer,
                               SizeTypeArray const & col_buffer,
                               ConstScalarTypeArray const & element_buffer,
                               ScalarTypeArray & vec_buffer,
                               std::size_t num_cols,
                               viennacl::linalg::lower_tag)
        {
          std::size_t row_begin = row_buffer[0];
          for (std::size_t row = 0; row < num_cols; ++row)
          {
            NumericT vec_entry = vec_buffer[row];
            
            // substitute and remember diagonal entry
            std::size_t row_end = row_buffer[row+1];
            NumericT diagonal_entry = 0;
            for (std::size_t i = row_begin; i < row_end; ++i)
            {
              std::size_t col_index = col_buffer[i];
              if (col_index < row)
                vec_entry -= vec_buffer[col_index] * element_buffer[i];
              else if (col_index == row)
                diagonal_entry = element_buffer[i];
            }
            
            vec_buffer[row] = vec_entry / diagonal_entry;
            row_begin = row_end;
          }
        }

        
        template <typename NumericT, typename ConstScalarTypeArray, typename ScalarTypeArray, typename SizeTypeArray>
        void csr_inplace_solve(SizeTypeArray const & row_buffer,
                               SizeTypeArray const & col_buffer,
                               ConstScalarTypeArray const & element_buffer,
                               ScalarTypeArray & vec_buffer,
                               std::size_t num_cols,
                               viennacl::linalg::unit_upper_tag)
        {
          for (std::size_t row2 = 1; row2 < num_cols; ++row2)
          {
            std::size_t row = (num_cols - row2) - 1;
            NumericT vec_entry = vec_buffer[row];
            std::size_t row_begin = row_buffer[row];
            std::size_t row_end   = row_buffer[row+1];
            for (std::size_t i = row_begin; i < row_end; ++i)
            {
              std::size_t col_index = col_buffer[i];
              if (col_index > row)
                vec_entry -= vec_buffer[col_index] * element_buffer[i];
            }
            vec_buffer[row] = vec_entry;
          }
        }
        
        template <typename NumericT, typename ConstScalarTypeArray, typename ScalarTypeArray, typename SizeTypeArray>
        void csr_inplace_solve(SizeTypeArray const & row_buffer,
                               SizeTypeArray const & col_buffer,
                               ConstScalarTypeArray const & element_buffer,
                               ScalarTypeArray & vec_buffer,
                               std::size_t num_cols,
                               viennacl::linalg::upper_tag)
        {
          for (std::size_t row2 = 0; row2 < num_cols; ++row2)
          {
            std::size_t row = (num_cols - row2) - 1;
            NumericT vec_entry = vec_buffer[row];
            
            // substitute and remember diagonal entry
            std::size_t row_begin = row_buffer[row];
            std::size_t row_end   = row_buffer[row+1];
            NumericT diagonal_entry = 0;
            for (std::size_t i = row_begin; i < row_end; ++i)
            {
              std::size_t col_index = col_buffer[i];
              if (col_index > row)
                vec_entry -= vec_buffer[col_index] * element_buffer[i];
              else if (col_index == row)
                diagonal_entry = element_buffer[i];
            }
            
            vec_buffer[row] = vec_entry / diagonal_entry;
          }
        }
        
      } //namespace detail
      
      
      
      /** @brief Inplace solution of a lower triangular compressed_matrix with unit diagonal. Typically used for LU substitutions
      *
      * @param L    The matrix
      * @param vec    The vector
      */
      template<typename ScalarType, unsigned int MAT_ALIGNMENT, unsigned int VEC_ALIGNMENT>
      void inplace_solve(compressed_matrix<ScalarType, MAT_ALIGNMENT> const & L,
                         vector<ScalarType, VEC_ALIGNMENT> & vec,
                         viennacl::linalg::unit_lower_tag tag)
      {
        ScalarType         * vec_buf    = detail::extract_raw_pointer<ScalarType>(vec.handle());
        ScalarType   const * elements   = detail::extract_raw_pointer<ScalarType>(L.handle());
        unsigned int const * row_buffer = detail::extract_raw_pointer<unsigned int>(L.handle1());
        unsigned int const * col_buffer = detail::extract_raw_pointer<unsigned int>(L.handle2());
       
        detail::csr_inplace_solve<ScalarType>(row_buffer, col_buffer, elements, vec_buf, L.size2(), tag);
      }
      
      /** @brief Inplace solution of a lower triangular compressed_matrix. Typically used for LU substitutions
      *
      * @param L    The matrix
      * @param vec    The vector
      */
      template<typename ScalarType, unsigned int MAT_ALIGNMENT, unsigned int VEC_ALIGNMENT>
      void inplace_solve(compressed_matrix<ScalarType, MAT_ALIGNMENT> const & L,
                         vector<ScalarType, VEC_ALIGNMENT> & vec,
                         viennacl::linalg::lower_tag tag)
      {
        ScalarType         * vec_buf    = detail::extract_raw_pointer<ScalarType>(vec.handle());
        ScalarType   const * elements   = detail::extract_raw_pointer<ScalarType>(L.handle());
        unsigned int const * row_buffer = detail::extract_raw_pointer<unsigned int>(L.handle1());
        unsigned int const * col_buffer = detail::extract_raw_pointer<unsigned int>(L.handle2());
        
        detail::csr_inplace_solve<ScalarType>(row_buffer, col_buffer, elements, vec_buf, L.size2(), tag);
      }
      
      
      /** @brief Inplace solution of a upper triangular compressed_matrix with unit diagonal. Typically used for LU substitutions
      *
      * @param U    The matrix
      * @param vec    The vector
      */
      template<typename ScalarType, unsigned int MAT_ALIGNMENT, unsigned int VEC_ALIGNMENT>
      void inplace_solve(compressed_matrix<ScalarType, MAT_ALIGNMENT> const & U,
                         vector<ScalarType, VEC_ALIGNMENT> & vec,
                         viennacl::linalg::unit_upper_tag tag)
      {
        ScalarType         * vec_buf    = detail::extract_raw_pointer<ScalarType>(vec.handle());
        ScalarType   const * elements   = detail::extract_raw_pointer<ScalarType>(U.handle());
        unsigned int const * row_buffer = detail::extract_raw_pointer<unsigned int>(U.handle1());
        unsigned int const * col_buffer = detail::extract_raw_pointer<unsigned int>(U.handle2());
        
        detail::csr_inplace_solve<ScalarType>(row_buffer, col_buffer, elements, vec_buf, U.size2(), tag);
      }
      
      /** @brief Inplace solution of a upper triangular compressed_matrix. Typically used for LU substitutions
      *
      * @param U    The matrix
      * @param vec    The vector
      */
      template<typename ScalarType, unsigned int MAT_ALIGNMENT, unsigned int VEC_ALIGNMENT>
      void inplace_solve(compressed_matrix<ScalarType, MAT_ALIGNMENT> const & U,
                         vector<ScalarType, VEC_ALIGNMENT> & vec,
                         viennacl::linalg::upper_tag tag)
      {
        ScalarType         * vec_buf    = detail::extract_raw_pointer<ScalarType>(vec.handle());
        ScalarType   const * elements   = detail::extract_raw_pointer<ScalarType>(U.handle());
        unsigned int const * row_buffer = detail::extract_raw_pointer<unsigned int>(U.handle1());
        unsigned int const * col_buffer = detail::extract_raw_pointer<unsigned int>(U.handle2());
        
        detail::csr_inplace_solve<ScalarType>(row_buffer, col_buffer, elements, vec_buf, U.size2(), tag);
      }
      
      
      
      
      
      
      
      //
      // Triangular solve for compressed_matrix, A^T \ b
      //
      
      namespace detail
      {
        template <typename NumericT, typename ConstScalarTypeArray, typename ScalarTypeArray, typename SizeTypeArray>
        void csr_trans_inplace_solve(SizeTypeArray const & row_buffer,
                                     SizeTypeArray const & col_buffer,
                                     ConstScalarTypeArray const & element_buffer,
                                     ScalarTypeArray & vec_buffer,
                                     std::size_t num_cols,
                                     viennacl::linalg::unit_lower_tag)
        {
          std::size_t col_begin = row_buffer[0];
          for (std::size_t col = 0; col < num_cols; ++col)
          {
            NumericT vec_entry = vec_buffer[col];
            std::size_t col_end = row_buffer[col+1];
            for (std::size_t i = col_begin; i < col_end; ++i)
            {
              unsigned int row_index = col_buffer[i];
              if (row_index > col)
                vec_buffer[row_index] -= vec_entry * element_buffer[i];
            }
            col_begin = col_end;
          }
        }

        template <typename NumericT, typename ConstScalarTypeArray, typename ScalarTypeArray, typename SizeTypeArray>
        void csr_trans_inplace_solve(SizeTypeArray const & row_buffer,
                                     SizeTypeArray const & col_buffer,
                                     ConstScalarTypeArray const & element_buffer,
                                     ScalarTypeArray & vec_buffer,
                                     std::size_t num_cols,
                                     viennacl::linalg::lower_tag)
        {
          std::size_t col_begin = row_buffer[0];
          for (std::size_t col = 0; col < num_cols; ++col)
          {
            std::size_t col_end = row_buffer[col+1];
            
            // Stage 1: Find diagonal entry:
            NumericT diagonal_entry = 0;
            for (std::size_t i = col_begin; i < col_end; ++i)
            {
              std::size_t row_index = col_buffer[i];
              if (row_index == col)
              {
                diagonal_entry = element_buffer[i];
                break;
              }
            }
            
            // Stage 2: Substitute
            NumericT vec_entry = vec_buffer[col] / diagonal_entry;
            vec_buffer[col] = vec_entry;
            for (std::size_t i = col_begin; i < col_end; ++i)
            {
              std::size_t row_index = col_buffer[i];
              if (row_index > col)
                vec_buffer[row_index] -= vec_entry * element_buffer[i];
            }
            col_begin = col_end;
          }
        }
        
        template <typename NumericT, typename ConstScalarTypeArray, typename ScalarTypeArray, typename SizeTypeArray>
        void csr_trans_inplace_solve(SizeTypeArray const & row_buffer,
                                     SizeTypeArray const & col_buffer,
                                     ConstScalarTypeArray const & element_buffer,
                                     ScalarTypeArray & vec_buffer,
                                     std::size_t num_cols,
                                     viennacl::linalg::unit_upper_tag)
        {
          for (std::size_t col2 = 0; col2 < num_cols; ++col2)
          {
            std::size_t col = (num_cols - col2) - 1;
            
            NumericT vec_entry = vec_buffer[col];
            std::size_t col_begin = row_buffer[col];
            std::size_t col_end = row_buffer[col+1];
            for (std::size_t i = col_begin; i < col_end; ++i)
            {
              std::size_t row_index = col_buffer[i];
              if (row_index < col)
                vec_buffer[row_index] -= vec_entry * element_buffer[i];
            }
            
          }
        }

        template <typename NumericT, typename ConstScalarTypeArray, typename ScalarTypeArray, typename SizeTypeArray>
        void csr_trans_inplace_solve(SizeTypeArray const & row_buffer,
                                     SizeTypeArray const & col_buffer,
                                     ConstScalarTypeArray const & element_buffer,
                                     ScalarTypeArray & vec_buffer,
                                     std::size_t num_cols,
                                     viennacl::linalg::upper_tag)
        {
          for (std::size_t col2 = 0; col2 < num_cols; ++col2)
          {
            std::size_t col = (num_cols - col2) - 1;
            std::size_t col_begin = row_buffer[col];
            std::size_t col_end = row_buffer[col+1];
            
            // Stage 1: Find diagonal entry:
            NumericT diagonal_entry = 0;
            for (std::size_t i = col_begin; i < col_end; ++i)
            {
              std::size_t row_index = col_buffer[i];
              if (row_index == col)
              {
                diagonal_entry = element_buffer[i];
                break;
              }
            }
            
            // Stage 2: Substitute
            NumericT vec_entry = vec_buffer[col] / diagonal_entry;
            vec_buffer[col] = vec_entry;
            for (std::size_t i = col_begin; i < col_end; ++i)
            {
              std::size_t row_index = col_buffer[i];
              if (row_index < col)
                vec_buffer[row_index] -= vec_entry * element_buffer[i];
            }
          }
        }
        
      } //namespace detail
      
      /** @brief Inplace solution of a lower triangular compressed_matrix with unit diagonal. Typically used for LU substitutions
      *
      * @param L    The matrix
      * @param vec    The vector
      */
      template<typename ScalarType, unsigned int MAT_ALIGNMENT, unsigned int VEC_ALIGNMENT>
      void inplace_solve(matrix_expression< const compressed_matrix<ScalarType, MAT_ALIGNMENT>,
                                            const compressed_matrix<ScalarType, MAT_ALIGNMENT>,
                                            op_trans> const & proxy,
                         vector<ScalarType, VEC_ALIGNMENT> & vec,
                         viennacl::linalg::unit_lower_tag tag)
      {
        ScalarType         * vec_buf    = detail::extract_raw_pointer<ScalarType>(vec.handle());
        ScalarType   const * elements   = detail::extract_raw_pointer<ScalarType>(proxy.lhs().handle());
        unsigned int const * row_buffer = detail::extract_raw_pointer<unsigned int>(proxy.lhs().handle1());
        unsigned int const * col_buffer = detail::extract_raw_pointer<unsigned int>(proxy.lhs().handle2());
        
        detail::csr_trans_inplace_solve<ScalarType>(row_buffer, col_buffer, elements, vec_buf, proxy.lhs().size1(), tag);
      }

      /** @brief Inplace solution of a lower triangular compressed_matrix. Typically used for LU substitutions
      *
      * @param L    The matrix
      * @param vec    The vector
      */
      template<typename ScalarType, unsigned int MAT_ALIGNMENT, unsigned int VEC_ALIGNMENT>
      void inplace_solve(matrix_expression< const compressed_matrix<ScalarType, MAT_ALIGNMENT>,
                                            const compressed_matrix<ScalarType, MAT_ALIGNMENT>,
                                            op_trans> const & proxy,
                         vector<ScalarType, VEC_ALIGNMENT> & vec,
                         viennacl::linalg::lower_tag tag)
      {
        ScalarType         * vec_buf    = detail::extract_raw_pointer<ScalarType>(vec.handle());
        ScalarType   const * elements   = detail::extract_raw_pointer<ScalarType>(proxy.lhs().handle());
        unsigned int const * row_buffer = detail::extract_raw_pointer<unsigned int>(proxy.lhs().handle1());
        unsigned int const * col_buffer = detail::extract_raw_pointer<unsigned int>(proxy.lhs().handle2());
        
        detail::csr_trans_inplace_solve<ScalarType>(row_buffer, col_buffer, elements, vec_buf, proxy.lhs().size1(), tag);
      }
      
      
      /** @brief Inplace solution of a upper triangular compressed_matrix with unit diagonal. Typically used for LU substitutions
      *
      * @param L    The matrix
      * @param vec    The vector
      */
      template<typename ScalarType, unsigned int MAT_ALIGNMENT, unsigned int VEC_ALIGNMENT>
      void inplace_solve(matrix_expression< const compressed_matrix<ScalarType, MAT_ALIGNMENT>,
                                            const compressed_matrix<ScalarType, MAT_ALIGNMENT>,
                                            op_trans> const & proxy,
                         vector<ScalarType, VEC_ALIGNMENT> & vec,
                         viennacl::linalg::unit_upper_tag tag)
      {
        ScalarType         * vec_buf    = detail::extract_raw_pointer<ScalarType>(vec.handle());
        ScalarType   const * elements   = detail::extract_raw_pointer<ScalarType>(proxy.lhs().handle());
        unsigned int const * row_buffer = detail::extract_raw_pointer<unsigned int>(proxy.lhs().handle1());
        unsigned int const * col_buffer = detail::extract_raw_pointer<unsigned int>(proxy.lhs().handle2());
        
        detail::csr_trans_inplace_solve<ScalarType>(row_buffer, col_buffer, elements, vec_buf, proxy.lhs().size1(), tag);
      }
      
      
      /** @brief Inplace solution of a upper triangular compressed_matrix with unit diagonal. Typically used for LU substitutions
      *
      * @param L    The matrix
      * @param vec    The vector
      */
      template<typename ScalarType, unsigned int MAT_ALIGNMENT, unsigned int VEC_ALIGNMENT>
      void inplace_solve(matrix_expression< const compressed_matrix<ScalarType, MAT_ALIGNMENT>,
                                            const compressed_matrix<ScalarType, MAT_ALIGNMENT>,
                                            op_trans> const & proxy,
                         vector<ScalarType, VEC_ALIGNMENT> & vec,
                         viennacl::linalg::upper_tag tag)
      {
        ScalarType         * vec_buf    = detail::extract_raw_pointer<ScalarType>(vec.handle());
        ScalarType   const * elements   = detail::extract_raw_pointer<ScalarType>(proxy.lhs().handle());
        unsigned int const * row_buffer = detail::extract_raw_pointer<unsigned int>(proxy.lhs().handle1());
        unsigned int const * col_buffer = detail::extract_raw_pointer<unsigned int>(proxy.lhs().handle2());
        
        detail::csr_trans_inplace_solve<ScalarType>(row_buffer, col_buffer, elements, vec_buf, proxy.lhs().size1(), tag);
      }
      
      
      
      
      
      

      //
      // Coordinate Matrix
      //
      
      /** @brief Carries out matrix-vector multiplication with a coordinate_matrix
      *
      * Implementation of the convenience expression result = prod(mat, vec);
      *
      * @param mat    The matrix
      * @param vec    The vector
      * @param result The result vector
      */
      template<class ScalarType, unsigned int ALIGNMENT, unsigned int VECTOR_ALIGNMENT>
      void prod_impl(const viennacl::coordinate_matrix<ScalarType, ALIGNMENT> & mat, 
                     const viennacl::vector<ScalarType, VECTOR_ALIGNMENT> & vec,
                           viennacl::vector<ScalarType, VECTOR_ALIGNMENT> & result)
      {
        ScalarType         * result_buf   = detail::extract_raw_pointer<ScalarType>(result.handle());
        ScalarType   const * vec_buf      = detail::extract_raw_pointer<ScalarType>(vec.handle());
        ScalarType   const * elements     = detail::extract_raw_pointer<ScalarType>(mat.handle());
        unsigned int const * coord_buffer = detail::extract_raw_pointer<unsigned int>(mat.handle12());
        
        for (std::size_t i = 0; i< result.size(); ++i)
          result_buf[i] = 0;
        
        for (std::size_t i = 0; i < mat.nnz(); ++i)
          result_buf[coord_buffer[2*i]] += elements[i] * vec_buf[coord_buffer[2*i+1]];
      }
      

      //
      // ELL Matrix
      //
      /** @brief Carries out matrix-vector multiplication with a ell_matrix
      *
      * Implementation of the convenience expression result = prod(mat, vec);
      *
      * @param mat    The matrix
      * @param vec    The vector
      * @param result The result vector
      */
      template<class ScalarType, unsigned int ALIGNMENT, unsigned int VECTOR_ALIGNMENT>
      void prod_impl(const viennacl::ell_matrix<ScalarType, ALIGNMENT> & mat, 
                     const viennacl::vector<ScalarType, VECTOR_ALIGNMENT> & vec,
                           viennacl::vector<ScalarType, VECTOR_ALIGNMENT> & result)
      {
        ScalarType         * result_buf   = detail::extract_raw_pointer<ScalarType>(result.handle());
        ScalarType   const * vec_buf      = detail::extract_raw_pointer<ScalarType>(vec.handle());
        ScalarType   const * elements     = detail::extract_raw_pointer<ScalarType>(mat.handle());
        unsigned int const * coords       = detail::extract_raw_pointer<unsigned int>(mat.handle2());
        

        for(std::size_t row = 0; row < mat.size1(); ++row)
        {
          ScalarType sum = 0;
            
          for(unsigned int item_id = 0; item_id < mat.internal_maxnnz(); ++item_id)
          {
            std::size_t offset = row + item_id * mat.internal_size2();
            ScalarType val = elements[offset];

            if(val != 0)
            {
              unsigned int col = coords[offset];    
              sum += (vec_buf[col] * val);
            }
          }

          result_buf[row] = sum;
        }
      }

      //
      // Hybrid Matrix
      //
      /** @brief Carries out matrix-vector multiplication with a hyb_matrix
      *
      * Implementation of the convenience expression result = prod(mat, vec);
      *
      * @param mat    The matrix
      * @param vec    The vector
      * @param result The result vector
      */
      template<class ScalarType, unsigned int ALIGNMENT, unsigned int VECTOR_ALIGNMENT>
      void prod_impl(const viennacl::hyb_matrix<ScalarType, ALIGNMENT> & mat, 
                     const viennacl::vector<ScalarType, VECTOR_ALIGNMENT> & vec,
                           viennacl::vector<ScalarType, VECTOR_ALIGNMENT> & result)
      {
        ScalarType         * result_buf     = detail::extract_raw_pointer<ScalarType>(result.handle());
        ScalarType   const * vec_buf        = detail::extract_raw_pointer<ScalarType>(vec.handle());
        ScalarType   const * elements       = detail::extract_raw_pointer<ScalarType>(mat.handle());
        unsigned int const * coords         = detail::extract_raw_pointer<unsigned int>(mat.handle2());
        ScalarType   const * csr_elements   = detail::extract_raw_pointer<ScalarType>(mat.handle5());
        unsigned int const * csr_row_buffer = detail::extract_raw_pointer<unsigned int>(mat.handle3());
        unsigned int const * csr_col_buffer = detail::extract_raw_pointer<unsigned int>(mat.handle4());
        

        for(std::size_t row = 0; row < mat.size1(); ++row)
        {
          ScalarType sum = 0;
            
          //
          // Part 1: Process ELL part
          //
          for(unsigned int item_id = 0; item_id < mat.internal_ellnnz(); ++item_id)
          {
            std::size_t offset = row + item_id * mat.internal_size2();
            ScalarType val = elements[offset];

            if(val != 0)
            {
              unsigned int col = coords[offset];    
              sum += (vec_buf[col] * val);
            }
          }

          //
          // Part 2: Process HYB part
          //
          std::size_t col_begin = csr_row_buffer[row];
          std::size_t col_end   = csr_row_buffer[row + 1];

          for(unsigned int item_id = col_begin; item_id < col_end; item_id++)
          {
              sum += (vec_buf[csr_col_buffer[item_id]] * csr_elements[item_id]);
          }
          
          result_buf[row] = sum;
        }
        
      }
      
      
    } // namespace opencl
  } //namespace linalg
} //namespace viennacl


#endif
