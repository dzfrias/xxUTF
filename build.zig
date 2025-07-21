const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const lib = b.addLibrary(.{
        .name = "utf8norm",
        .root_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
        }),
        .linkage = .static,
    });
    lib.addIncludePath(b.path(""));
    lib.addCSourceFiles(.{
        .root = b.path(""),
        .files = files,
        .flags = &.{ "-Wall", "-Werror" },
    });
    lib.installHeader(b.path("utf8norm.h"), "utf8norm.h");

    b.installArtifact(lib);
}

const files: []const []const u8 = &.{
    "utf8norm.c",
    "normdata.c",
    "impl/neon.c",
    "impl/scalar.c",
};
