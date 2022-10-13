load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "bgp",
    srcs = ["message.c"],
    hdrs = ["message.h"],
    deps = [
        "//utils/logger",
    ],
    visibility = ["//visibility:public"],
)
