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

    const test_exe = b.addExecutable(.{
        .name = "test_utf8norm",
        .root_module = b.createModule(.{
            .root_source_file = b.path("test/test.zig"),
            .target = target,
            .optimize = optimize,
        }),
    });
    test_exe.linkLibrary(lib);
    const run_test_exe = b.addRunArtifact(test_exe);
    run_test_exe.addFileArg(b.path("test/NormalizationTest.txt"));
    const test_step = b.step("test", "Test utf8norm using the Unicode Character Database");
    test_step.dependOn(&run_test_exe.step);
}

const files: []const []const u8 = &.{
    "utf8norm.c",
    "normdata.c",
    "impl/neon.c",
    "impl/scalar.c",
};
