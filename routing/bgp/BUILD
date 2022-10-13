load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "bgp",
    srcs = ["routing.c"],
    hdrs = ["routing.h"],
    deps = [
        "//utils/logger",
    ],
    visibility = ["//visibility:public"],
)
