# This BUILD file was generated by //tools/base/bazel:java_import_generator, please do not edit.
load("//tools/base/bazel:maven.bzl", "maven_java_import", "maven_pom", "maven_aar")

maven_java_import(
    name = "jar",
    jars = ["netty-common-4.1.6.Final.jar"],
    pom = ":pom",
    visibility = ["//visibility:public"],
)

maven_pom(
    name = "pom",
    parent = "//prebuilts/tools/common/m2/repository/io/netty/netty-parent/4.1.6.Final:pom",
    source = "netty-common-4.1.6.Final.pom",
    visibility = ["//visibility:public"],
)
