const std = @import("std");

pub fn build(b: *std.Build) !void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const neon = b.option(bool, "neon", "Select usage of ARM NEON (default: detect)");
    const endianness = b.option(
        std.builtin.Endian,
        "endian",
        "Select native endianness (default: detect)",
    ) orelse .little;

    var sources: std.ArrayListUnmanaged([]const u8) = .empty;
    defer sources.deinit(b.allocator);
    try sources.appendSlice(b.allocator, default_sources);
    const add_neon = neon orelse (target.result.cpu.arch == .aarch64);
    if (add_neon) {
        try sources.appendSlice(b.allocator, neon_sources);
    }
    var flags: std.ArrayListUnmanaged([]const u8) = .empty;
    defer flags.deinit(b.allocator);
    try flags.appendSlice(b.allocator, default_flags);
    try flags.append(b.allocator, switch (endianness) {
        .little => "-DUTF8NORM_BIG_ENDIAN=0",
        .big => "-DUTF8NORM_BIG_ENDIAN=1",
    });
    if (!add_neon) {
        try flags.append(b.allocator, "-DUTF8NORM_IMPLEMENTATION_NEON=0");
    }

    const run_amalgamate = std.Build.Step.Run.create(b, "Run amalgamate");
    run_amalgamate.addFileArg(b.path("gen/amalgamate.py"));
    for (all_sources) |source| {
        run_amalgamate.addFileArg(b.path(source));
    }
    for (all_files) |file| {
        run_amalgamate.addFileInput(b.path(file));
    }
    run_amalgamate.addArg("-o");
    const amalgamation = run_amalgamate.addOutputFileArg("utf8norm_amalgamation.c");
    const amalgamate_install_file = b.addInstallFile(amalgamation, "amalgamation.c");
    const amalgamate_step = b.step("amalgamate", "Create the utf8norm amalgamation file");
    amalgamate_step.dependOn(&amalgamate_install_file.step);

    const lib = createLibrary(
        b,
        target,
        optimize,
        sources.items,
        flags.items,
        amalgamation,
    );
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

    const compare_exe = b.addExecutable(.{
        .name = "compare",
        .root_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
        }),
    });
    compare_exe.addIncludePath(b.path(""));
    compare_exe.linkSystemLibrary2("icu-uc", .{ .preferred_link_mode = .dynamic });
    compare_exe.addCSourceFile(.{ .file = amalgamation, .flags = flags.items });
    compare_exe.addCSourceFile(.{ .file = b.path("test/fuzz.c") });
    b.installArtifact(compare_exe);

    const benchmark_exe = b.addExecutable(.{
        .name = "benchmark",
        .root_module = b.createModule(.{
            .root_source_file = b.path("benchmarks/benchmark.zig"),
            .target = target,
            .optimize = if (optimize == .Debug)
                .ReleaseFast
            else
                optimize,
        }),
    });
    const optimized_lib = createLibrary(
        b,
        target,
        // Do not run benchmarks on a debug compilation
        if (optimize == .Debug)
            .ReleaseFast
        else
            optimize,
        sources.items,
        flags.items,
        amalgamation,
    );
    benchmark_exe.linkLibrary(optimized_lib);
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

fn createLibrary(
    b: *std.Build,
    target: std.Build.ResolvedTarget,
    optimize: std.builtin.OptimizeMode,
    sources: []const []const u8,
    flags: []const []const u8,
    amalgamation_path: std.Build.LazyPath,
) *std.Build.Step.Compile {
    const lib = b.addLibrary(.{
        .name = "utf8norm",
        .root_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
            .sanitize_c = false,
        }),
        .linkage = .static,
    });
    lib.addIncludePath(b.path(""));
    if (optimize == .Debug) {
        lib.addCSourceFiles(.{
            .root = b.path(""),
            .files = sources,
            .flags = flags,
        });
    } else {
        // Use single header for release builds. This gives worse compiler errors, but
        // better performance.
        lib.addCSourceFile(.{ .file = amalgamation_path, .flags = flags });
    }
    lib.installHeader(b.path("utf8norm.h"), "utf8norm.h");

    return lib;
}

const default_flags: []const []const u8 = &.{
    "-Wall",
    "-Wextra",
    "-Wcast-qual",
    "-Wcast-align",
    "-Wstrict-aliasing",
    "-Wpointer-arith",
    "-Wshadow",
    "-Winit-self",
    "-Wswitch-enum",
    "-Wstrict-prototypes",
    "-Wmissing-prototypes",
    "-Wredundant-decls",
    "-Wfloat-equal",
    "-Wundef",
    "-Wvla",
    "-Wformat=2",
    "-Wc++-compat",
    "-Werror",
};

const all_files: []const []const u8 = &.{
    "utf8norm.h",
    "utf8norm.c",
    "normdata.h",
    "normdata.c",
    "impl/scalar.h",
    "impl/scalar/scalar_utf8.c",
    "impl/scalar/scalar_utf8.h",
    "impl/scalar/scalar_utf16.c",
    "impl/scalar/scalar_utf16.h",
    "impl/scalar/scalar_common.c",
    "impl/scalar/scalar_common.h",
    "impl/neon.h",
    "impl/neon/neon_common.c",
    "impl/neon/neon_common.h",
    "impl/neon/neon_utf8.c",
    "impl/neon/neon_utf8.h",
    "impl/neon/neon_utf16.c",
    "impl/neon/neon_utf16.h",
};

const default_sources: []const []const u8 = &.{
    "utf8norm.c",
    "normdata.c",
    "impl/scalar/scalar_utf8.c",
    "impl/scalar/scalar_utf16.c",
    "impl/scalar/scalar_common.c",
};

const neon_sources: []const []const u8 = &.{
    "impl/neon/neon_common.c",
    "impl/neon/neon_utf8.c",
    "impl/neon/neon_utf16.c",
};

const all_sources: []const []const u8 = default_sources ++ neon_sources;
