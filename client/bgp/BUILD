load("@rules_cc//cc:defs.bzl", "cc_binary")

cc_binary(
    name = "bgp",
    srcs = ["main.c"],
    deps = [
        "//protocol/bgp",
        "//routing/bgp",
        "//utils/ifaddrs",
        "//utils/logger",
        "//utils/mem",
    ],
)
