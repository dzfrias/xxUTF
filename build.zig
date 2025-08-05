const std = @import("std");

pub fn build(b: *std.Build) !void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const neon = b.option(bool, "neon", "Select usage of ARM NEON (default: detect)");

    var sources: std.ArrayListUnmanaged([]const u8) = .empty;
    defer sources.deinit(b.allocator);
    try sources.appendSlice(b.allocator, &.{
        "utf8norm.c",
        "normdata.c",
        "impl/scalar.c",
    });
    const add_neon = neon orelse (target.result.cpu.arch == .aarch64);
    if (add_neon) {
        try sources.append(b.allocator, "impl/neon.c");
    }

    const lib = b.addLibrary(.{
        .name = "utf8norm",
        .root_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
        }),
        .linkage = .static,
    });
    lib.addIncludePath(b.path(""));
    var flags: std.ArrayListUnmanaged([]const u8) = .empty;
    defer flags.deinit(b.allocator);
    try flags.append(b.allocator, "-Wall");
    try flags.append(b.allocator, "-Werror");
    if (!add_neon) {
        try flags.append(b.allocator, "-DUTF8NORM_IMPLEMENTATION_NEON=0");
    }
    lib.addCSourceFiles(.{
        .root = b.path(""),
        .files = sources.items,
        .flags = flags.items,
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

    const run_amalgamate = std.Build.Step.Run.create(b, "Run amalgamate");
    run_amalgamate.addFileArg(b.path("gen/amalgamate.py"));
    for (all_sources) |source| {
        run_amalgamate.addFileArg(b.path(source));
    }
    run_amalgamate.addArg("-o");
    const amalgamation = run_amalgamate.addOutputFileArg("utf8norm_amalgamation.c");
    const amalgamate_install_file = b.addInstallFile(amalgamation, "amalgamation.c");
    const amalgamate_step = b.step("amalgamate", "Create the utf8norm amalgamation file");
    amalgamate_step.dependOn(&amalgamate_install_file.step);

    const afl_fuzz: ?[]const u8 = b.findProgram(&.{"afl-fuzz"}, &.{}) catch null;
    if (afl_fuzz) |afl_fuzz_bin| {
        const run_afl_cc = b.addSystemCommand(&.{
            try b.findProgram(&.{"afl-cc"}, &.{}),
            "-O3",
            "-flto=full",
            "-I.",
            "-o",
        });
        const fuzz_exe = run_afl_cc.addOutputFileArg("fuzz");
        run_afl_cc.addFileArg(amalgamation);
        run_afl_cc.addFileArg(b.path("test/fuzz.c"));
        run_afl_cc.addArgs(flags.items);
        run_afl_cc.addArg("-Wno-error=unused-const-variable");
        run_afl_cc.addArgs(&.{ "-licuuc", "-licudata" });
        const run_fuzz_exe = b.addSystemCommand(&.{
            afl_fuzz_bin,
            "-i",
        });
        run_fuzz_exe.addDirectoryArg(b.path("test/corpus"));
        run_fuzz_exe.addArg("-o");
        const out_dir = run_fuzz_exe.addOutputDirectoryArg("afl_out");
        run_fuzz_exe.addArg("--");
        run_fuzz_exe.addFileArg(fuzz_exe);
        const install_fuzz_results = b.addInstallDirectory(.{
            .source_dir = out_dir,
            .install_subdir = "",
            .install_dir = .{ .custom = "fuzz" },
        });
        const fuzz_step = b.step("fuzz", "Fuzz utf8norm using AFL++");
        fuzz_step.dependOn(&install_fuzz_results.step);
    }

    const benchmark_exe = b.addExecutable(.{
        .name = "benchmark",
        .root_module = b.createModule(.{
            .root_source_file = b.path("benchmarks/benchmark.zig"),
            .target = target,
            .optimize = optimize,
        }),
    });
    benchmark_exe.linkLibrary(lib);
    benchmark_exe.linkSystemLibrary2("utf8proc", .{ .preferred_link_mode = .dynamic });
    const run_benchmark_exe = b.addRunArtifact(benchmark_exe);
    run_benchmark_exe.addDirectoryArg(b.path("benchmarks/inputs"));
    for (b.args orelse &.{}) |arg| {
        run_benchmark_exe.addArg(arg);
    }
    const benchmark_step = b.step("bench", "Benchmark utf8norm");
    const benchmark_install = b.addInstallArtifact(benchmark_exe, .{});
    benchmark_step.dependOn(&run_benchmark_exe.step);
    benchmark_step.dependOn(&benchmark_install.step);
}

const all_sources: []const []const u8 = &.{
    "utf8norm.c",
    "normdata.c",
    "impl/scalar.c",
    "impl/neon.c",
};
