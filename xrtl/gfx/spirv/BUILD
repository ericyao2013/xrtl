# Description:
#  SPIR-V tools.

package(default_visibility = ["//xrtl:internal"])

licenses(["notice"])  # Apache 2.0

cc_library(
    name = "shader_compiler",
    srcs = ["shader_compiler.cc"],
    hdrs = ["shader_compiler.h"],
    deps = [
        "//xrtl/base:debugging",
        "//xrtl/base:string_view",
        "//xrtl/base:tracing",
        "@com_github_KhronosGroup_glslang//:glslang",
        "@com_github_KhronosGroup_glslang//:SPIRV",
    ],
)

cc_test(
    name = "shader_compiler_test",
    srcs = ["shader_compiler_test.cc"],
    deps = [
        ":shader_compiler",
        "//xrtl/testing:gtest_main",
    ],
    size = "small",
)

cc_library(
    name = "spirv_optimizer",
    srcs = ["spirv_optimizer.cc"],
    hdrs = ["spirv_optimizer.h"],
    deps = [
        "//xrtl/base:logging",
        "//xrtl/base:tracing",
        "@spirv_tools//:spirv_tools_opt",
    ],
)

cc_test(
    name = "spirv_optimizer_test",
    srcs = ["spirv_optimizer_test.cc"],
    deps = [
        ":spirv_optimizer",
        "//xrtl/testing:gtest_main",
    ],
    size = "small",
)