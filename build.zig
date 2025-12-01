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
        "impl/scalar_common.c",
        "impl/scalar.c",
    });
    const add_neon = neon orelse (target.result.cpu.arch == .aarch64);
    if (add_neon) {
        try sources.append(b.allocator, "impl/neon.c");
    }
    var flags: std.ArrayListUnmanaged([]const u8) = .empty;
    defer flags.deinit(b.allocator);
    try flags.appendSlice(b.allocator, &.{
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
    });
    if (!add_neon) {
        try flags.append(b.allocator, "-DUTF8NORM_IMPLEMENTATION_NEON=0");
    }

    var generated_sources: std.ArrayListUnmanaged(std.Build.LazyPath) = .empty;
    defer generated_sources.deinit(b.allocator);

    // Handle generated files with CMake-style substitutions
    const scalar_impl = substitute(
        b,
        b.path("impl/scalar_impl.c.in"),
        "scalar_impl.c",
        &.{
            .{ .name = "DECOMP_SUFFIX", .value = "nfd" },
            .{ .name = "DECOMP_TABLE_NAME", .value = "NFD" },
            .{ .name = "COMP_SUFFIX", .value = "nfc" },
            .{ .name = "COMP_TABLE_NAME", .value = "NFC" },
        },
    );
    const scalar_impl_compat = substitute(
        b,
        b.path("impl/scalar_impl.c.in"),
        "scalar_impl_compat.c",
        &.{
            .{ .name = "DECOMP_SUFFIX", .value = "nfkd" },
            .{ .name = "DECOMP_TABLE_NAME", .value = "NFKD" },
            .{ .name = "COMP_SUFFIX", .value = "nfkc" },
            .{ .name = "COMP_TABLE_NAME", .value = "NFKC" },
        },
    );
    try generated_sources.append(b.allocator, scalar_impl);
    try generated_sources.append(b.allocator, scalar_impl_compat);

    const run_amalgamate = std.Build.Step.Run.create(b, "Run amalgamate");
    run_amalgamate.addFileArg(b.path("gen/amalgamate.py"));
    for (all_sources) |source| {
        run_amalgamate.addFileArg(b.path(source));
    }
    for (generated_sources.items) |source| {
        run_amalgamate.addFileArg(source);
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
        generated_sources.items,
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
    compare_exe.linkSystemLibrary2("utf8proc", .{ .preferred_link_mode = .dynamic });
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
        generated_sources.items,
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
    generated_sources: []const std.Build.LazyPath,
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
        for (generated_sources) |source| {
            lib.addCSourceFile(.{ .file = source, .flags = flags });
        }
    } else {
        // Use single header for release builds. This gives worse compiler errors, but
        // better performance.
        lib.addCSourceFile(.{ .file = amalgamation_path, .flags = flags });
    }
    lib.installHeader(b.path("utf8norm.h"), "utf8norm.h");

    return lib;
}

const SubstituteValue = struct {
    name: []const u8,
    value: []const u8,
};

fn substitute(
    b: *std.Build,
    input: std.Build.LazyPath,
    out_base_name: []const u8,
    values: []const SubstituteValue,
) std.Build.LazyPath {
    const run_substitute = std.Build.Step.Run.create(b, "Run substitute");
    run_substitute.addFileArg(b.path("gen/substitute.py"));
    run_substitute.addFileArg(input);
    run_substitute.addArg("-o");
    const output = run_substitute.addOutputFileArg(out_base_name);
    run_substitute.addArg("--values");
    for (values) |sub_val| {
        const arg = std.fmt.allocPrint(
            b.allocator,
            "{s}:{s}",
            .{ sub_val.name, sub_val.value },
        ) catch @panic("OOM");
        defer b.allocator.free(arg);
        run_substitute.addArg(arg);
    }
    return output;
}

const all_sources: []const []const u8 = &.{
    "utf8norm.c",
    "normdata.c",
    "impl/scalar_common.c",
    "impl/scalar.c",
    "impl/neon.c",
};

const all_files: []const []const u8 = &.{
    "utf8norm.h",
    "utf8norm.c",
    "normdata.h",
    "normdata.c",
    "impl/scalar.h",
    "impl/scalar.c",
    "impl/scalar_common.h",
    "impl/scalar_common.c",
    "impl/scalar_impl.c.in",
    "impl/neon.h",
    "impl/neon.c",
};
