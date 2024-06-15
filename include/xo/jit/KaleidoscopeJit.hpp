//===- KaleidoscopeJIT.h - A simple JIT for Kaleidoscope --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Contains a simple JIT definition for use in the kaleidoscope tutorials.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_KALEIDOSCOPEJIT_H
#define LLVM_EXECUTIONENGINE_ORC_KALEIDOSCOPEJIT_H

#include "llvm/ADT/StringRef.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/Orc/CompileUtils.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"
#include "llvm/ExecutionEngine/Orc/ExecutorProcessControl.h"
#include "llvm/ExecutionEngine/Orc/IRCompileLayer.h"
#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"
#include "llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h"
#include "llvm/ExecutionEngine/Orc/Shared/ExecutorSymbolDef.h"
#include "llvm/ExecutionEngine/SectionMemoryManager.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/LLVMContext.h"
#include <memory>

namespace xo {
    namespace jit {

        class KaleidoscopeJIT {
        private:
            using StringRef = llvm::StringRef;
            using SectionMemoryManager = llvm::SectionMemoryManager;
            using DynamicLibrarySearchGenerator = llvm::orc::DynamicLibrarySearchGenerator;
            using ConcurrentIRCompiler = llvm::orc::ConcurrentIRCompiler;
            using ExecutionSession = llvm::orc::ExecutionSession;
            using DataLayout = llvm::DataLayout;
            using MangleAndInterner = llvm::orc::MangleAndInterner;
            using RTDyldObjectLinkingLayer = llvm::orc::RTDyldObjectLinkingLayer;
            using IRCompileLayer = llvm::orc::IRCompileLayer;
            using JITDylib = llvm::orc::JITDylib;
            using JITTargetMachineBuilder = llvm::orc::JITTargetMachineBuilder;
            using ThreadSafeModule = llvm::orc::ThreadSafeModule;
            using ResourceTrackerSP = llvm::orc::ResourceTrackerSP;
            using ExecutorSymbolDef = llvm::orc::ExecutorSymbolDef;
            using SelfExecutorProcessControl = llvm::orc::SelfExecutorProcessControl;

        private:
            /* execution session - represents a currenlty-running jit program */
            std::unique_ptr<ExecutionSession> ES;

            DataLayout DL;
            MangleAndInterner Mangle;

            RTDyldObjectLinkingLayer ObjectLayer;
            IRCompileLayer CompileLayer;

            JITDylib &MainJD;

        public:
            KaleidoscopeJIT(std::unique_ptr<ExecutionSession> ES,
                            JITTargetMachineBuilder JTMB,
                            DataLayout DL)
                : ES(std::move(ES)),
                  DL(std::move(DL)),
                  Mangle(*this->ES, this->DL),
                  ObjectLayer(*this->ES,
                              []() { return std::make_unique<SectionMemoryManager>(); }),
                  CompileLayer(*this->ES, ObjectLayer,
                               std::make_unique<ConcurrentIRCompiler>(std::move(JTMB))),
                  MainJD(this->ES->createBareJITDylib("<main>"))
                {
                    MainJD.addGenerator(
                        cantFail(DynamicLibrarySearchGenerator::GetForCurrentProcess(
                                     DL.getGlobalPrefix())));
                    if (JTMB.getTargetTriple().isOSBinFormatCOFF()) {
                        ObjectLayer.setOverrideObjectFlagsWithResponsibilityFlags(true);
                        ObjectLayer.setAutoClaimResponsibilityForObjectSymbols(true);
                    }
                }

            ~KaleidoscopeJIT() {
                if (auto Err = ES->endSession())
                    ES->reportError(std::move(Err));
            }

            static llvm::Expected<std::unique_ptr<KaleidoscopeJIT>> Create() {
                auto EPC = SelfExecutorProcessControl::Create();
                if (!EPC)
                    return EPC.takeError();

                auto ES = std::make_unique<ExecutionSession>(std::move(*EPC));

                JITTargetMachineBuilder JTMB(
                    ES->getExecutorProcessControl().getTargetTriple());

                auto DL = JTMB.getDefaultDataLayoutForTarget();
                if (!DL)
                    return DL.takeError();

                return std::make_unique<KaleidoscopeJIT>(std::move(ES), std::move(JTMB),
                                                         std::move(*DL));
            }

            const DataLayout &getDataLayout() const { return DL; }

            JITDylib &getMainJITDylib() { return MainJD; }

            llvm::Error addModule(ThreadSafeModule TSM, ResourceTrackerSP RT = nullptr) {
                if (!RT)
                    RT = MainJD.getDefaultResourceTracker();
                return CompileLayer.add(RT, std::move(TSM));
            }

            llvm::Expected<ExecutorSymbolDef> lookup(StringRef Name) {
                return ES->lookup({&MainJD}, Mangle(Name.str()));
            }

            /* dump */
            void dump_execution_session() {
                ES->dump(llvm::errs());
            }
        };

    } // end namespace jit
} // end namespace xo

#endif // LLVM_EXECUTIONENGINE_ORC_KALEIDOSCOPEJIT_H
