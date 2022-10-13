load("@rules_cc//cc:defs.bzl", "cc_binary")

cc_binary(
    name = "bgp",
    srcs = ["main.c"],
    deps = [
       "//utils/logger",
       "//utils/mem",
       "//protocol/bgp",
       "//routing/bgp",
       "//utils/ifaddrs",
    ],
)
