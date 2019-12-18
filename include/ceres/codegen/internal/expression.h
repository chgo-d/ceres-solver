// Ceres Solver - A fast non-linear least squares minimizer
// Copyright 2019 Google Inc. All rights reserved.
// http://code.google.com/p/ceres-solver/
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice,
//   this list of conditions and the following disclaimer.
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
// * Neither the name of Google Inc. nor the names of its contributors may be
//   used to endorse or promote products derived from this software without
//   specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// Author: darius.rueckert@fau.de (Darius Rueckert)
//
// During code generation, your cost functor is converted into a list of
// expressions stored in an expression graph. For each operator (+,-,=,...),
// function call (sin,cos,...), and special keyword (if,else,...) the
// appropriate ExpressionType is selected. On a high level all ExpressionTypes
// are grouped into two different classes: Arithmetic expressions and control
// expressions.
//
// Part 1: Arithmetic Expressions
//
// Arithmetic expression are the most basic and common types. They are all of
// the following form:
//
// <lhs> = <rhs>
//
// <lhs> is the variable name on the left hand side of the assignment. <rhs> can
// be different depending on the ExpressionType. It must evaluate to a single
// scalar value though. Here are a few examples of arithmetic expressions (the
// ExpressionType is given on the right):
//
// v_0 = 3.1415;        // COMPILE_TIME_CONSTANT
// v_1 = v_0;           // ASSIGNMENT
// v_2 = v_0 + v_1;     // PLUS
// v_3 = v_2 / v_0;     // DIVISION
// v_4 = sin(v_3);      // FUNCTION_CALL
// v_5 = v_4 < v_3;     // BINARY_COMPARISON
//
// As you can see, the right hand side of each expression contains exactly one
// operator/value/function call. If you write long expressions like
//
// T c = a + b - T(3) * a;
//
// it will broken up into the individual expressions like so:
//
// v_0 = a + b;
// v_1 = 3;
// v_2 = v_1 * a;
// c   = v_0 - v_2;
//
// All arithmetic expressions are generated by operator and function
// overloading. These overloads are defined in expression_ref.h.
//
//
//
// Part 2: Control Expressions
//
// Control expressions include special instructions that handle the control flow
// of a program. So far, only if/else is supported, but while/for might come in
// the future.
//
// Generating code for conditional jumps (if/else) is more complicated than
// for arithmetic expressions. Let's look at a small example to see the
// problems. After that we explain how these problems are solved in Ceres.
//
// 1    T a = parameters[0][0];
// 2    T b = 1.0;
// 3    if (a < b) {
// 4      b = 3.0;
// 5    } else {
// 6      b = 4.0;
// 7    }
// 8    b += 1.0;
// 9    residuals[0] = b;
//
// Problem 1.
// We need to generate code for both branches. In C++ there is no way to execute
// both branches of an if, but we need to execute them to generate the code.
//
// Problem 2.
// The comparison a < b in line 3 is not convertible to bool. Since the value of
// a is not known during code generation, the expression a < b can not be
// evaluated. In fact, a < b will return an expression of type
// BINARY_COMPARISON.
//
// Problem 3.
// There is no way to record that an if was executed. "if" is a special operator
// which cannot be overloaded. Therefore we can't generate code that contains
// "if.
//
// Problem 4.
// We have no information about "blocks" or "scopes" during code generation.
// Even if we could overload the if-operator, there is now way to capture which
// expression was executed in which branches of the if. For example, we generate
// code for the else branch. How can we know that the else branch is finished?
// Is line 8 inside the else-block or already outside?
//
// Solution.
// Instead of using the keywords if/else we insert the macros
// CERES_IF, CERES_ELSE and CERES_ENDIF. These macros just map to a function,
// which inserts an expression into the graph. Here is how the example from
// above looks like with the expanded macros:
//
// 1    T a = parameters[0][0];
// 2    T b = 1.0;
// 3    CreateIf(a < b); {
// 4      b = 3.0;
// 5    } CreateElse(); {
// 6      b = 4.0;
// 7    } CreateEndif();
// 8    b += 1.0;
// 9    residuals[0] = b;
//
// Problem 1 solved.
// There are no branches during code generation, therefore both blocks are
// evaluated.
//
// Problem 2 solved.
// The function CreateIf(_) does not take a bool as argument, but an
// ComparisonExpression. Later during code generation an actual "if" is created
// with the condition as argument.
//
// Problem 3 solved.
// We replaced "if" by a function call so we can record it now.
//
// Problem 4 solved.
// Expressions are added into the graph in the correct order. That means, after
// seeing a CreateIf() we know that all following expressions until CreateElse()
// belong to the true-branch. Similar, all expression from CreateElse() to
// CreateEndif() belong to the false-branch. This also works recursively with
// nested ifs.
//
// If you want to use the AutoDiff code generation for your cost functors, you
// have to replace all if/else by the CERES_IF, CERES_ELSE and CERES_ENDIF
// macros. The example from above looks like this:
//
// 1    T a = parameters[0][0];
// 2    T b = 1.0;
// 3    CERES_IF (a < b) {
// 4      b = 3.0;
// 5    } CERES_ELSE {
// 6      b = 4.0;
// 7    } CERES_ENDIF;
// 8    b += 1.0;
// 9    residuals[0] = b;
//
// These macros don't have a negative impact on performance, because they only
// expand to the CreateIf/.. functions in code generation mode. Otherwise they
// expand to the if/else keywords. See expression_ref.h for the exact
// definition.
//
#ifndef CERES_PUBLIC_CODEGEN_INTERNAL_EXPRESSION_H_
#define CERES_PUBLIC_CODEGEN_INTERNAL_EXPRESSION_H_

#include <string>
#include <vector>

namespace ceres {
namespace internal {

using ExpressionId = int;
static constexpr ExpressionId kInvalidExpressionId = -1;

enum class ExpressionType {
  // v_0 = 3.1415;
  COMPILE_TIME_CONSTANT,

  // Assignment from a user-variable to a generated variable that can be used by
  // other expressions. This is used for local variables of cost functors and
  // parameters of a functions.
  // v_0 = _observed_point_x;
  // v_0 = parameters[0][0];
  INPUT_ASSIGNMENT,

  // Assignment from a generated variable to a user-variable. Used to store the
  // output of a generated cost functor.
  // residual[0] = v_51;
  OUTPUT_ASSIGNMENT,

  // Trivial assignment
  // v_3 = v_1
  ASSIGNMENT,

  // Binary Arithmetic Operations
  // v_2 = v_0 + v_1
  // The operator is stored in Expression::name_.
  BINARY_ARITHMETIC,

  // Unary Arithmetic Operation
  // v_1 = -(v_0);
  // v_2 = +(v_1);
  // The operator is stored in Expression::name_.
  UNARY_ARITHMETIC,

  // Binary Comparison. (<,>,&&,...)
  // This is the only expressions which returns a 'bool'.
  // v_2 = v_0 < v_1
  // The operator is stored in Expression::name_.
  BINARY_COMPARISON,

  // The !-operator on logical expression.
  LOGICAL_NEGATION,

  // General Function Call.
  // v_5 = f(v_0,v_1,...)
  FUNCTION_CALL,

  // Conditional control expressions if/else/endif.
  // These are special expressions, because they don't define a new variable.
  IF,
  ELSE,
  ENDIF,

  // A single comment line. Even though comments are 'unused' expression they
  // will not be optimized away.
  COMMENT,

  // No Operation. A placeholder for an 'empty' expressions which will be
  // optimized out during code generation.
  NOP
};

enum class ExpressionReturnType {
  // The expression returns a scalar value (float or double). Used for most
  // arithmetic operations and function calls.
  SCALAR,
  // The expression returns a boolean value. Used for logical expressions
  //   v_3 = v_1 < v_2
  // and functions returning a bool
  //   v_3 = isfinite(v_1);
  BOOLEAN,
  // The expressions doesn't return a value. Used for the control
  // expressions
  // and NOP.
  VOID,
};

std::string ExpressionReturnTypeToString(ExpressionReturnType type);

// This class contains all data that is required to generate one line of code.
// Each line has the following form:
//
// lhs = rhs;
//
// The left hand side is the variable name given by its own id. The right hand
// side depends on the ExpressionType. For example, a COMPILE_TIME_CONSTANT
// expressions with id 4 generates the following line:
// v_4 = 3.1415;
//
// Objects of this class are created indirectly using the static CreateXX
// methods. During creation, the Expression objects are added to the
// ExpressionGraph (see expression_graph.h).
class Expression {
 public:
  // Creates a NOP expression.
  Expression() = default;

  Expression(ExpressionType type,
             ExpressionReturnType return_type = ExpressionReturnType::VOID,
             ExpressionId lhs_id = kInvalidExpressionId,
             const std::vector<ExpressionId>& arguments = {},
             const std::string& name = "",
             double value = 0);

  // Helper 'constructors' that create an Expression with the correct type. You
  // can also use the actual constructor from above, but using the create
  // functions is less prone to errors.
  static Expression CreateCompileTimeConstant(double v);

  static Expression CreateInputAssignment(const std::string& name);
  static Expression CreateOutputAssignment(ExpressionId v,
                                           const std::string& name);
  static Expression CreateAssignment(ExpressionId dst, ExpressionId src);
  static Expression CreateBinaryArithmetic(const std::string& op,
                                           ExpressionId l,
                                           ExpressionId r);
  static Expression CreateUnaryArithmetic(const std::string& op,
                                          ExpressionId v);
  static Expression CreateBinaryCompare(const std::string& name,
                                        ExpressionId l,
                                        ExpressionId r);
  static Expression CreateLogicalNegation(ExpressionId v);
  static Expression CreateScalarFunctionCall(
      const std::string& name, const std::vector<ExpressionId>& params);
  static Expression CreateLogicalFunctionCall(
      const std::string& name, const std::vector<ExpressionId>& params);
  static Expression CreateIf(ExpressionId condition);
  static Expression CreateElse();
  static Expression CreateEndIf();
  static Expression CreateComment(const std::string& comment);

  // Returns true if this is an arithmetic expression.
  // Arithmetic expressions must have a valid left hand side.
  bool IsArithmeticExpression() const;

  // Returns true if this is a control expression.
  bool IsControlExpression() const;

  // If this expression is the compile time constant with the given value.
  // Used during optimization to collapse zero/one arithmetic operations.
  // b = a + 0;      ->    b = a;
  bool IsCompileTimeConstantAndEqualTo(double constant) const;

  // Checks if "other" is identical to "this" so that one of the epxressions can
  // be replaced by a trivial assignment. Used during common subexpression
  // elimination.
  bool IsReplaceableBy(const Expression& other) const;

  // Replace this expression by 'other'.
  // The current id will be not replaced. That means other experssions
  // referencing this one stay valid.
  void Replace(const Expression& other);

  // If this expression has 'other' as an argument.
  bool DirectlyDependsOn(ExpressionId other) const;

  // Converts this expression into a NOP
  void MakeNop();

  // Returns true if this expression has a valid lhs.
  bool HasValidLhs() const { return lhs_id_ != kInvalidExpressionId; }

  // Compares all members with the == operator. If this function succeeds,
  // IsSemanticallyEquivalentTo will also return true.
  bool operator==(const Expression& other) const;
  bool operator!=(const Expression& other) const { return !(*this == other); }

  // Semantically equivalent expressions are similar in a way, that the type(),
  // value(), name(), number of arguments is identical. The lhs_id() and the
  // argument_ids can differ. For example, the following groups of expressions
  // are semantically equivalent:
  //
  // v_0 = v_1 + v_2;
  // v_0 = v_1 + v_3;
  // v_1 = v_1 + v_2;
  //
  // v_0 = sin(v_1);
  // v_3 = sin(v_2);
  bool IsSemanticallyEquivalentTo(const Expression& other) const;

  ExpressionType type() const { return type_; }
  ExpressionReturnType return_type() const { return return_type_; }
  ExpressionId lhs_id() const { return lhs_id_; }
  double value() const { return value_; }
  const std::string& name() const { return name_; }
  const std::vector<ExpressionId>& arguments() const { return arguments_; }

  void set_lhs_id(ExpressionId new_lhs_id) { lhs_id_ = new_lhs_id; }
  std::vector<ExpressionId>* mutable_arguments() { return &arguments_; }

 private:
  ExpressionType type_ = ExpressionType::NOP;
  ExpressionReturnType return_type_ = ExpressionReturnType::VOID;

  // If lhs_id_ >= 0, then this expression is assigned to v_<lhs_id>.
  // For example:
  //    v_1 = v_0 + v_0     (Type = PLUS)
  //    v_3 = sin(v_1)      (Type = FUNCTION_CALL)
  //      ^
  //   lhs_id_
  //
  // If lhs_id_ == kInvalidExpressionId, then the expression type is not
  // arithmetic. Currently, only the following types have lhs_id = invalid:
  // IF,ELSE,ENDIF,NOP
  ExpressionId lhs_id_ = kInvalidExpressionId;

  // Expressions have different number of arguments. For example a binary "+"
  // has 2 parameters and a function call to "sin" has 1 parameter. Here, a
  // reference to these paratmers is stored. Note: The order matters!
  std::vector<ExpressionId> arguments_;

  // Depending on the type this name is one of the following:
  //  (type == FUNCTION_CALL) -> the function name
  //  (type == PARAMETER)     -> the parameter name
  //  (type == OUTPUT_ASSIGN) -> the output variable name
  //  (type == BINARY_COMPARE)-> the comparison symbol "<","&&",...
  //  else                    -> unused
  std::string name_;

  // Only valid if type == COMPILE_TIME_CONSTANT
  double value_ = 0;
};

}  // namespace internal
}  // namespace ceres

#endif  // CERES_PUBLIC_CODEGEN_INTERNAL_EXPRESSION_H_
