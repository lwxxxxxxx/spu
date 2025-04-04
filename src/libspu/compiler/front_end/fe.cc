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

#include "libspu/compiler/front_end/fe.h"

#include "fmt/ranges.h"
#include "magic_enum.hpp"
#include "mlir/Dialect/Func/Extensions/InlinerExtension.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Parser/Parser.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Transforms/Passes.h"
#include "stablehlo/dialect/StablehloOps.h"
#include "xla/mlir_hlo/mhlo/IR/hlo_ops.h"
#include "xla/mlir_hlo/mhlo/transforms/passes.h"
#include "xla/translate/mhlo_to_hlo/translate.h"

#include "libspu/compiler/common/compilation_context.h"
#include "libspu/compiler/front_end/hlo_importer.h"
#include "libspu/compiler/utils/utils.h"
#include "libspu/core/prelude.h"
#include "libspu/dialect/pphlo/IR/dialect.h"
#include "libspu/dialect/pphlo/transforms/passes.h"
namespace spu::compiler {

FE::FE(CompilationContext *ctx) : ctx_(ctx) {
  ctx_->getMLIRContext()
      ->loadDialect<mlir::spu::pphlo::PPHloDialect, mlir::mhlo::MhloDialect,
                    mlir::stablehlo::StablehloDialect,
                    mlir::func::FuncDialect>();
  mlir::DialectRegistry registry;
  mlir::func::registerInlinerExtension(registry);
  ctx_->getMLIRContext()->appendDialectRegistry(registry);
}

mlir::OwningOpRef<mlir::ModuleOp> FE::doit(const CompilationSource &source) {
  HloImporter importer(ctx_);
  mlir::OwningOpRef<mlir::ModuleOp> module;

  if (source.ir_type == spu::SourceIRType::STABLEHLO) {
    module = mlir::parseSourceString<mlir::ModuleOp>(source.ir_txt,
                                                     ctx_->getMLIRContext());

    SPU_ENFORCE(module, "MLIR parser failure");

    // Convert stablehlo to mhlo first
    mlir::PassManager pm(ctx_->getMLIRContext());
    pm.addPass(mlir::mhlo::createStablehloLegalizeToHloPass());
    if (pm.run(module.get()).failed()) {
      SPU_THROW("Failed to legalized stablehlo to mhlo");
    }

    // Convert back to XLA, SPU still relies on XLA to eliminate ops like
    // batch-normal-inference
    std::string xla_text;
    llvm::raw_string_ostream out(xla_text);
    if (!mlir::failed(xla::MlirHloToHloTranslateFunction(module.get(), out,
                                                         true, true))) {
      out.flush();
      module = importer.parseXlaModuleFromString(xla_text);
    }
  } else if (source.ir_type == spu::SourceIRType::XLA) {
    module = importer.parseXlaModuleFromString(source.ir_txt);
  } else {
    SPU_THROW("Unhandled IR type = {}", source.ir_type);
  }

  std::string input_vis_str;
  {
    std::vector<std::string> input_vis;
    for (const auto &v : source.input_visibility) {
      input_vis.emplace_back(magic_enum::enum_name(v));
    }
    input_vis_str = fmt::format("input_vis_list={}", fmt::join(input_vis, ","));
  }

  // Run pipeline
  mlir::PassManager pm(ctx_->getMLIRContext());
  buildFrontEndPipeline(&pm, input_vis_str);

  ctx_->setupPrettyPrintConfigurations(&pm);

  auto ret = pm.run(module.get());

  if (ret.failed()) {
    SPU_THROW("Run front end pipeline failed");
  }

  return module;
}

void FE::buildFrontEndPipeline(mlir::PassManager *pm, const std::string &args) {

  // mhlo side
  {
    pm->addPass(mlir::createInlinerPass());
    pm->addPass(mlir::mhlo::createExpandHloTuplesPass());

    auto &optPM = pm->nest<mlir::func::FuncOp>();
    optPM.addPass(mlir::mhlo::createLowerComplexPass());
    optPM.addPass(mlir::mhlo::createLegalizeEinsumToDotGeneralPass());
    optPM.addPass(mlir::mhlo::createLegalizeGeneralDotPass());
    optPM.addPass(mlir::mhlo::createSinkConstantsToControlFlowPass());
    optPM.addPass(mlir::mhlo::createLowerComplexPass());
    optPM.addPass(mlir::mhlo::createFlattenTuplePass());
    optPM.addPass(mlir::mhlo::createBroadcastPropagationPass());

    // Convert to stablehlo
    pm->addPass(mlir::mhlo::createHloLegalizeToStablehloPass());
  }

  // stablehlo now
  // Dialect conversion
  {
    auto l = mlir::spu::pphlo::createLegalizeToPPHloPass();
    if (!args.empty()) {
      SPU_ENFORCE(l->initializeOptions(args, mlir::spu::argparser_error_handler)
                      .succeeded());
    }
    pm->addPass(std::move(l));
  }
  auto &optPM = pm->nest<mlir::func::FuncOp>();
  optPM.addPass(mlir::spu::pphlo::createLowerConversionCastPass());
}

} // namespace spu::compiler
