load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "bgp",
    srcs = ["routing.c"],
    hdrs = ["routing.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//protocol/bgp",
        "//utils/logger",
    ],
)
