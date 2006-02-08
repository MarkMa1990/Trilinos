// @HEADER
// ***********************************************************************
// 
//    Thyra: Interfaces and Support for Abstract Numerical Algorithms
//                 Copyright (2004) Sandia Corporation
// 
// Under terms of Contract DE-AC04-94AL85000, there is a non-exclusive
// license for use of this work by or on behalf of the U.S. Government.
// 
// This library is free software; you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as
// published by the Free Software Foundation; either version 2.1 of the
// License, or (at your option) any later version.
//  
// This library is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//  
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
// USA
// Questions? Contact Michael A. Heroux (maherou@sandia.gov) 
// 
// ***********************************************************************
// @HEADER

#ifndef THYRA_DIAGONAL_LINEAR_OP_WITH_SOLVE_DECL_HPP
#define THYRA_DIAGONAL_LINEAR_OP_WITH_SOLVE_DECL_HPP

#include "Thyra_DiagonalLinearOpDecl.hpp"
#include "Thyra_LinearOpWithSolveBaseDecl.hpp"
#include "Thyra_SingleRhsLinearOpWithSolveBaseDecl.hpp"

namespace Thyra {

/** \brief Concrete <tt>LinearOpWithSolveBase</tt> subclass for diagonal linear
 * operators.
 *
 * This class represents a diagonal linear operator <tt>M</tt> of the form:
 \verbatim

 M = gamma*diag(diag)
 \endverbatim
 *
 * where <tt>diag</tt> is a <tt>VectorBase</tt> object and <tt>gamma</tt>
 * is a <tt>Scalar</tt>.
 *
 * The defined operator implements <tt>this->apply()</tt> as follows:
 *
 \verbatim

 y = alpha*op(M)*x + beta*y
 
 =>

 y(i) = (alpha*gamma)*diag(i)*x(i) + beta*y(i), for i = 1 ... n
 \endverbatim
 *
 * where <tt>n = this->domain()->dim()</tt>.
 *
 * The defined operator implements <tt>this->solve()</tt> as follows:
 *
 \verbatim

 x = inv(op(M))*b
 
 =>

 x(i) = (1/gamma)*b(i)/diag(i), for i = 1 ... n
 \endverbatim
 *
 * where <tt>n = this->domain()->dim()</tt>.
 *
 * That is all there is to this subclass.
 */
template<class Scalar>
class DiagonalLinearOpWithSolve
  : virtual public DiagonalLinearOp<Scalar>                    // Public interface
  , virtual public LinearOpWithSolveBase<Scalar>               // Public interface
  , virtual protected SingleRhsLinearOpWithSolveBase<Scalar>   // Implementation detail
{
public:

  /** @name Constructors/initializers/accessors */
  //@{

  /** \brief Constructs to uninitialized.
   *
   * Postconditions:<ul>
   * <li><tt>this->getDiag().get()==NULL</tt>
   * </ul>
   */
  DiagonalLinearOpWithSolve();

  /// Calls <tt>initialize()</tt>
  DiagonalLinearOpWithSolve(
    const Teuchos::RefCountPtr<const VectorBase<Scalar> >   &diag
    ,const Scalar                                           &gamma = Teuchos::ScalarTraits<Scalar>::one()
    );

protected:

  /** @name Overridden from SingleScalarLinearOpWithSolveBase */
  //@{
  /** \brief . */
  bool solveSupportsTrans(ETransp M_trans) const;
  /** \brief . */
  bool solveSupportsSolveTolType(ETransp M_trans, ESolveTolType solveTolType) const;
  //@}

  /** @name Overridden from SingleRhsLinearOpWithSolveBase */
  //@{
  /** \brief . */
  SolveStatus<Scalar> solve(
    const ETransp                         M_trans
    ,const VectorBase<Scalar>             &b
    ,VectorBase<Scalar>                   *x
    ,const SolveCriteria<Scalar>          *solveCriteria
    ) const;
  //@}

};

}	// end namespace Thyra

#endif	// THYRA_DIAGONAL_LINEAR_OP_WITH_SOLVE_DECL_HPP
