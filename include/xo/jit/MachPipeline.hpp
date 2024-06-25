/** @file MachPipeline.hpp
 *
 *  Author: Roland Conybeare
 **/

#pragma once

//#include <cstdint>

#include "xo/refcnt/Refcounted.hpp"
#include "IrPipeline.hpp"
#include "LlvmContext.hpp"
#include "Jit.hpp"
#include "activation_record.hpp"

#include "xo/expression/Expression.hpp"
#include "xo/expression/ConstantInterface.hpp"
#include "xo/expression/PrimitiveInterface.hpp"
#include "xo/expression/Apply.hpp"
#include "xo/expression/Lambda.hpp"
#include "xo/expression/Variable.hpp"
#include "xo/expression/IfExpr.hpp"

/* stuff from kaleidoscope.cpp */
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/StandardInstrumentations.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Transforms/Scalar/Reassociate.h"
#include "llvm/Transforms/Scalar/SimplifyCFG.h"


namespace xo {
    namespace jit {
        /** @class MachPipeline
         *  @brief just-in-time compiler for EGAD
         *
         *  TODO: make module name a parameter?
        **/
        class MachPipeline : public ref::Refcount {
        public:
            using Expression = xo::ast::Expression;
            using Lambda = xo::ast::Lambda;
            using TypeDescr = xo::reflect::TypeDescr;
            //using ConstantInterface = xo::ast::ConstantInterface;

        public:
            /* tracking KaleidoscopeJIT::Create() here.. */
            static llvm::Expected<std::unique_ptr<MachPipeline>> make_aux();
            static xo::ref::rp<MachPipeline> make();

            // ----- module access -----

            llvm::Module * current_module() { return llvm_module_.get(); }
            ref::brw<LlvmContext> llvm_cx() { return llvm_cx_; }
            llvm::IRBuilder<> * llvm_current_ir_builder() { return llvm_toplevel_ir_builder_.get(); }

            /** target triple = string describing target host for codegen **/
            const std::string & target_triple() const;
            /** append function names defined in attached module to *p_v
             *
             *  (RC 15jun2024 - this part is working)
             **/
            std::vector<std::string> get_function_name_v();

            /** write state of execution session (all the associated dynamic libraries) **/
            void dump_execution_session();

            // ----- code generation -----

            llvm::Value * codegen_constant(ref::brw<xo::ast::ConstantInterface> expr);
            llvm::Function * codegen_primitive(ref::brw<xo::ast::PrimitiveInterface> expr);
            llvm::Value * codegen_apply(ref::brw<xo::ast::Apply> expr, llvm::IRBuilder<> & ir_builder);
            /* NOTE: codegen_lambda() needs to be reentrant too.
             *       for example can have a lambda in apply position.
             */
            llvm::Function * codegen_lambda_decl(ref::brw<xo::ast::Lambda> expr);
            llvm::Function * codegen_lambda_defn(ref::brw<xo::ast::Lambda> expr, llvm::IRBuilder<> & ir_builder);
            llvm::Value * codegen_variable(ref::brw<xo::ast::Variable> var, llvm::IRBuilder<> & ir_builder);
            llvm::Value * codegen_ifexpr(ref::brw<xo::ast::IfExpr> ifexpr, llvm::IRBuilder<> & ir_builder);

            llvm::Value * codegen(ref::brw<Expression> expr, llvm::IRBuilder<> & ir_builder);

            llvm::Value * codegen_toplevel(ref::brw<Expression> expr);

            // ----- jit online execution -----

            /** add IR code in current module to JIT,
             *  so that its available for execution
             **/
            void machgen_current_module();

            /** dump text description of module contents to console **/
            void dump_current_module();

            /** report mangle symbol **/
            std::string mangle(const std::string & x) const;

            /** lookup symbol in jit-associated output library **/
            llvm::Expected<llvm::orc::ExecutorAddr> lookup_symbol(const std::string & x);

            virtual void display(std::ostream & os) const;
            virtual std::string display_string() const;

        private:
            /** construct instance, adopting jit for compilation+execution **/
            explicit MachPipeline(std::unique_ptr<Jit> jit);

            /** iniitialize native builder (i.e. for platform we're running on) **/
            static void init_once();

            /** helper function.  find all lambda expressions in AST @p expr **/
            std::vector<ref::brw<Lambda>> find_lambdas(ref::brw<Expression> expr) const;

        public:
            /** codegen helper for a user-defined function (codegen_lambda()):
             *  create stack slot on behalf of some formal parameter to a function,
             *  so we can avoid SSA restriction on function body
             *
             *  @p var_type.  variable type
             **/
            llvm::AllocaInst * create_entry_block_alloca(llvm::Function * llvm_fn,
                                                         const std::string & var_name,
                                                         TypeDescr var_type);

        private:
            /** (re)create pipeline to turn expressions into llvm IR code **/
            void recreate_llvm_ir_pipeline();

        private:
            // ----- this part adapted from LLVM 19.0 KaleidoscopeJIT.hpp [wip] -----

            /** just-in-time compiler -- construct machine code that can
             *  be invoked from this running process
             **/
            std::unique_ptr<Jit> jit_;

            // ----- this part adapted from kaleidoscope.cpp -----

            /** everything below represents a pipeline
             *  that takes expressions, and turns them into llvm IR.
             *
             *  llvm IR can be added to running JIT by calling
             *    jit_->addModule()
             *  Note that this makes the module itself unavailable to us
             **/
            xo::ref::rp<IrPipeline> ir_pipeline_;

            /** owns + manages core "global" llvm data,
             *  including type- and constant- unique-ing tables.
             *
             *  Not threadsafe,  but ok to have multiple threads,
             *  each with its own LLVMContext
             **/
            ref::rp<LlvmContext> llvm_cx_;

            /** builder for intermediate-representation objects **/
            std::unique_ptr<llvm::IRBuilder<>> llvm_toplevel_ir_builder_;

            /** a module (1:1 with library ?) being prepared by llvm.
             *  IR-level -- does not contain machine code
             *
             *  - function names are unique within a module.
             **/
            std::unique_ptr<llvm::Module> llvm_module_;

            /** map global names to functions/variables **/
            std::map<std::string, xo::ref::rp<Expression>> global_env_;

            /** map variable names (formal parameters) to
             *  corresponding llvm IR.
             *
             *  only supports one level atm (i.e. only top-level functions)
             *
             *  All values  live on the stack, so that we can evade single-assignment
             *  restrictions.
             *
             *  rhs identifies logical stack location of a variable
             **/
            std::stack<activation_record> env_stack_;  /* <-> kaleidoscope NamedValues */
        }; /*MachPipeline*/

        inline std::ostream &
        operator<<(std::ostream & os, const MachPipeline & x) {
            x.display(os);
            return os;
        }
    } /*namespace jit*/
} /*namespace xo*/


/** end MachPipeline.hpp **/
