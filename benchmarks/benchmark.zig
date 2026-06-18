const std = @import("std");
const c = @import("c");
const flags = @import("flags");
const assert = std.debug.assert;
const Allocator = std.mem.Allocator;

const print_alignment = 30;
const full_line_length = print_alignment + 16;

const ImplementationFunc = fn ([]const u8) void;
const Encoding = enum { utf8, utf16le, utf16be };

const implementations: []const struct { []const u8, ImplementationFunc, Encoding } = &.{
    .{ "xxutf_utf8_nfd", xxutfNormalizeUtf8NFD, .utf8 },
    .{ "icu_utf8_nfd", IcuNormalizerUtf8(c.shim_unorm2_getNFDInstance).implementation, .utf8 },
    .{ "xxutf_utf8_nfkd", xxutfNormalizeUtf8NFKD, .utf8 },
    .{ "icu_utf8_nfkd", IcuNormalizerUtf8(c.shim_unorm2_getNFKDInstance).implementation, .utf8 },
    .{ "xxutf_utf8_nfc", xxutfNormalizeUtf8NFC, .utf8 },
    .{ "icu_utf8_nfc", IcuNormalizerUtf8(c.shim_unorm2_getNFCInstance).implementation, .utf8 },
    .{ "xxutf_utf8_nfkc", xxutfNormalizeUtf8NFKC, .utf8 },
    .{ "icu_utf8_nfkc", IcuNormalizerUtf8(c.shim_unorm2_getNFKCInstance).implementation, .utf8 },
    .{ "xxutf_utf16le_nfd", xxutfNormalizeUtf16leNFD, .utf16le },
    .{
        "icu_utf16le_nfd",
        IcuNormalizerAnyEncoding(c.shim_unorm2_getNFDInstance, .utf16le).implementation,
        .utf16le,
    },
    .{ "xxutf_utf16be_nfd", xxutfNormalizeUtf16beNFD, .utf16be },
    .{
        "icu_utf16be_nfd",
        IcuNormalizerAnyEncoding(c.shim_unorm2_getNFDInstance, .utf16be).implementation,
        .utf16be,
    },
    .{ "xxutf_utf16le_nfkd", xxutfNormalizeUtf16leNFKD, .utf16le },
    .{
        "icu_utf16le_nfkd",
        IcuNormalizerAnyEncoding(c.shim_unorm2_getNFKDInstance, .utf16le).implementation,
        .utf16le,
    },
    .{ "xxutf_utf16be_nfkd", xxutfNormalizeUtf16beNFKD, .utf16be },
    .{
        "icu_utf16be_nfkd",
        IcuNormalizerAnyEncoding(c.shim_unorm2_getNFKDInstance, .utf16be).implementation,
        .utf16be,
    },
    .{ "xxutf_utf16le_nfc", xxutfNormalizeUtf16leNFC, .utf16le },
    .{
        "icu_utf16le_nfc",
        IcuNormalizerAnyEncoding(c.shim_unorm2_getNFCInstance, .utf16le).implementation,
        .utf16le,
    },
    .{ "xxutf_utf16be_nfc", xxutfNormalizeUtf16beNFC, .utf16be },
    .{
        "icu_utf16be_nfc",
        IcuNormalizerAnyEncoding(c.shim_unorm2_getNFCInstance, .utf16be).implementation,
        .utf16be,
    },
    .{ "xxutf_utf16le_nfkc", xxutfNormalizeUtf16leNFKC, .utf16le },
    .{
        "icu_utf16le_nfkc",
        IcuNormalizerAnyEncoding(c.shim_unorm2_getNFKCInstance, .utf16le).implementation,
        .utf16le,
    },
    .{ "xxutf_utf16be_nfkc", xxutfNormalizeUtf16beNFKC, .utf16be },
    .{
        "icu_utf16be_nfkc",
        IcuNormalizerAnyEncoding(c.shim_unorm2_getNFKCInstance, .utf16be).implementation,
        .utf16be,
    },
    .{ "xxutf_utf8_cf", xxutfCasefoldUtf8, .utf8 },
    .{ "icu_utf8_cf", icuCasefoldUtf8, .utf8 },
    .{ "xxutf_utf16le_cf", xxutfCasefoldUtf16le, .utf16le },
    .{
        "icu_utf16le_cf",
        IcuCaseFoldAnyEncoding(.utf16le).implementation,
        .utf16le,
    },
    .{ "xxutf_utf16be_cf", xxutfCasefoldUtf16be, .utf16be },
    .{
        "icu_utf16be_cf",
        IcuCaseFoldAnyEncoding(.utf16be).implementation,
        .utf16be,
    },
};

const ImplementationResult = struct {
    name: []const u8,
    results: []const BenchResult,
};

const help =
    \\The xxUTF benchmark runner
    \\
    \\Positionals:
    \\  DIR  Set the directory that has the benchmark inputs
    \\
    \\Flags:
    \\  -p, --patterns PATTERNS     A comma-separated list of implementations (defalt: all).
    \\  -j, --json                  Output detailed JSON instead of a table graphic (default: false).
    \\  -t, --test TEST             Run a spcific test, with file extension omitted.
    \\  -h, --help                  Print this help and exit.
    \\
    \\Notes:
    \\  Patterns are of the form:
    \\    IMPL_ENCODING_ALGO
    \\  Possible IMPL values:     icu, xxutf
    \\  Possible ENCODING values: utf8, utf16le, utf16be
    \\  Possible ALGO values:     nfd, nfkd, nfc, nfkc, cf
    \\  Examples:
    \\    zig build bench -- --patterns xxutf_utf8_nfd,icu_utf8_nfd
    \\    zig build bench -- --patterns xxutf_utf8,icu_utf8
    \\
;

pub fn main(init: std.process.Init) !void {
    var args_it = init.minimal.args.iterate();
    _ = args_it.next().?;
    const result = flags.parseFromIterator(Flags, args_it, init.gpa) catch return error.ArgumentError;
    const options = switch (result) {
        .flags => |f| f,
        .err => return error.ArgumentError,
    };

    if (options.help) {
        var stderr = std.Io.File.stderr();
        var stderr_buf: [2048]u8 = undefined;
        var stderr_writer = stderr.writer(init.io, &stderr_buf);
        try stderr_writer.interface.writeAll(help);
        try stderr_writer.interface.flush();
        return;
    }

    var input_dir = try std.Io.Dir.cwd().openDir(
        init.io,
        options.input_dir_path,
        .{ .iterate = true },
    );
    defer input_dir.close(init.io);
    var stdout_buffer: [1024]u8 = undefined;
    var stdout_writer = std.Io.File.stdout().writer(init.io, &stdout_buffer);
    const stdout = &stdout_writer.interface;

    var results: ?std.ArrayList(ImplementationResult) = if (options.json)
        .empty
    else
        null;
    defer if (results) |*r| {
        for (r.items) |impl_result| {
            for (impl_result.results) |bench_res| {
                init.gpa.free(bench_res.name);
            }
            init.gpa.free(impl_result.results);
        }
        r.deinit(init.gpa);
    };
    const patterns = if (options.patterns) |s| try parseCommaList(init.gpa, s) else null;
    defer if (patterns) |p| init.gpa.free(p);
    inline for (implementations) |impl| {
        if (patterns == null or match: {
            for (patterns.?) |pattern| {
                if (std.mem.indexOf(u8, impl[0], pattern) != null) {
                    break :match true;
                }
            }
            break :match false;
        }) {
            const impl_result = try benchmarkImplementation(
                init.io,
                init.gpa,
                impl[0],
                stdout,
                options.json,
                input_dir,
                options.@"test",
                impl[2],
                impl[1],
            );
            if (results) |*r| {
                try r.append(init.gpa, impl_result);
            } else {
                try stdout.writeByte('\n');
                try stdout.flush();
                // Immediately free the allocated ImplementationResult
                init.gpa.free(impl_result.results);
            }
        }
    }

    if (results) |r| {
        try stdout.print("{f}", .{std.json.fmt(r.items, .{})});
        try stdout.flush();
    }
}

const Flags = struct {
    input_dir_path: []const u8,
    @"test": ?[]const u8,
    patterns: ?[]const u8,
    json: bool,
    help: bool,

    pub const positionals = .{
        .input_dir_path = void,
    };
    pub const shorts = .{
        .json = 'j',
        .patterns = 'p',
        .@"test" = 't',
    };
    pub const standalone = .{
        .help = void,
    };
};

fn parseCommaList(allocator: Allocator, patterns: []const u8) ![]const []const u8 {
    var parsed: std.ArrayListUnmanaged([]const u8) = .empty;
    defer parsed.deinit(allocator);
    var split_it = std.mem.splitScalar(u8, patterns, ',');
    while (split_it.next()) |pat| {
        try parsed.append(allocator, pat);
    }
    return try parsed.toOwnedSlice(allocator);
}

fn writeHeader(out: *std.Io.Writer, title: []const u8) !void {
    try out.writeAll(title);
    try out.splatByteAll('-', full_line_length - title.len);
    try out.writeByte('\n');
}

fn benchmarkImplementation(
    io: std.Io,
    allocator: Allocator,
    name: []const u8,
    out: *std.Io.Writer,
    quiet: bool,
    inputs: std.Io.Dir,
    specific_test: ?[]const u8,
    encoding: Encoding,
    comptime impl: fn ([]const u8) void,
) !ImplementationResult {
    if (!quiet) {
        try writeHeader(out, name);
    }

    var results: std.ArrayList(BenchResult) = .empty;
    var inputs_it = inputs.iterate();
    while (try inputs_it.next(io)) |entry| {
        if (entry.kind != .file) {
            continue;
        }
        if (specific_test) |s| {
            if (!std.ascii.eqlIgnoreCase(entry.name, s)) {
                continue;
            }
        }

        const file = try inputs.openFile(io, entry.name, .{});
        defer file.close(io);
        const result = try runBenchmark(
            io,
            try allocator.dupe(u8, entry.name),
            file,
            encoding,
            impl,
        );
        if (!quiet) {
            const input_size_f: f64 = @floatFromInt(result.input_size);
            const mean_ns_f: f64 = @floatFromInt(result.mean_ns);
            const sd_ns_f: f64 = @floatFromInt(result.sd_ns);
            const bytes_per_ns = input_size_f / mean_ns_f;
            const gb = 1_073_741_824.0;
            const ns_per_s: comptime_float = @floatFromInt(std.time.ns_per_s);
            const ratio = ns_per_s / gb;
            const ms = mean_ns_f / @as(f64, @floatFromInt(std.time.ns_per_ms));
            const sd_ms = sd_ns_f / @as(f64, @floatFromInt(std.time.ns_per_ms));
            // Calculate approximate GB/s standard deviation by propogating the error
            const sd_gb_s = input_size_f * (sd_ns_f / (mean_ns_f * mean_ns_f));
            try out.print(
                std.fmt.comptimePrint("{{s: >{}}}: {{d:.3}}±{{d:.3}}GB/s\n", .{print_alignment}),
                .{ entry.name, bytes_per_ns * ratio, sd_gb_s },
            );
            try out.print(
                std.fmt.comptimePrint("{{s: >{}}}: {{d:.3}}±{{d:.3}}ms\n", .{print_alignment}),
                .{ "", ms, sd_ms },
            );
            try out.flush();
        }
        try results.append(allocator, result);
    }

    return .{ .name = name, .results = try results.toOwnedSlice(allocator) };
}

fn xxutfNormalizeUtf8NFD(src: []const u8) void {
    var out: [100_000]u8 = undefined;
    _ = c.xxutf_normalize_utf8_nfd(src.ptr, src.len, &out);
}

fn xxutfNormalizeUtf8NFKD(src: []const u8) void {
    var out: [100_000]u8 = undefined;
    _ = c.xxutf_normalize_utf8_nfkd(src.ptr, src.len, &out);
}

fn xxutfNormalizeUtf8NFC(src: []const u8) void {
    var out: [100_000]u8 = undefined;
    _ = c.xxutf_normalize_utf8_nfc(src.ptr, src.len, &out);
}

fn xxutfNormalizeUtf8NFKC(src: []const u8) void {
    var out: [100_000]u8 = undefined;
    _ = c.xxutf_normalize_utf8_nfkc(src.ptr, src.len, &out);
}

fn xxutfNormalizeUtf16leNFD(src: []const u8) void {
    var out: [100_000]u8 = undefined;
    _ = c.xxutf_normalize_utf16le_nfd(src.ptr, src.len, &out);
}

fn xxutfNormalizeUtf16beNFD(src: []const u8) void {
    var out: [100_000]u8 = undefined;
    _ = c.xxutf_normalize_utf16be_nfd(src.ptr, src.len, &out);
}

fn xxutfNormalizeUtf16leNFKD(src: []const u8) void {
    var out: [100_000]u8 = undefined;
    _ = c.xxutf_normalize_utf16le_nfkd(src.ptr, src.len, &out);
}

fn xxutfNormalizeUtf16beNFKD(src: []const u8) void {
    var out: [100_000]u8 = undefined;
    _ = c.xxutf_normalize_utf16be_nfkd(src.ptr, src.len, &out);
}

fn xxutfNormalizeUtf16leNFC(src: []const u8) void {
    var out: [100_000]u8 = undefined;
    _ = c.xxutf_normalize_utf16le_nfc(src.ptr, src.len, &out);
}

fn xxutfNormalizeUtf16beNFC(src: []const u8) void {
    var out: [100_000]u8 = undefined;
    _ = c.xxutf_normalize_utf16be_nfc(src.ptr, src.len, &out);
}

fn xxutfNormalizeUtf16leNFKC(src: []const u8) void {
    var out: [100_000]u8 = undefined;
    _ = c.xxutf_normalize_utf16le_nfkc(src.ptr, src.len, &out);
}

fn xxutfNormalizeUtf16beNFKC(src: []const u8) void {
    var out: [100_000]u8 = undefined;
    _ = c.xxutf_normalize_utf16be_nfkc(src.ptr, src.len, &out);
}

fn xxutfCasefoldUtf8(src: []const u8) void {
    var out: [100_000]u8 = undefined;
    _ = c.xxutf_casefold_utf8(src.ptr, src.len, &out);
}

fn xxutfCasefoldUtf16le(src: []const u8) void {
    var out: [100_000]u8 = undefined;
    _ = c.xxutf_casefold_utf16le(src.ptr, src.len, &out);
}

fn xxutfCasefoldUtf16be(src: []const u8) void {
    var out: [100_000]u8 = undefined;
    _ = c.xxutf_casefold_utf16be(src.ptr, src.len, &out);
}

fn icuCasefoldUtf8(src: []const u8) void {
    var status: c.UErrorCode = c.U_ZERO_ERROR;
    const csm = c.shim_ucasemap_open(null, 0, &status);
    assert(status == c.U_ZERO_ERROR);
    var utf8_out: [16384]u8 = undefined;
    _ = c.shim_ucasemap_utf8FoldCase(
        csm,
        &utf8_out,
        utf8_out.len,
        src.ptr,
        @intCast(src.len),
        &status,
    );
    assert(status == c.U_ZERO_ERROR);
    c.shim_ucasemap_close(csm);
}

fn IcuCaseFoldAnyEncoding(comptime encoding: Encoding) type {
    return struct {
        fn implementation(src: []const u8) void {
            var status: c.UErrorCode = c.U_ZERO_ERROR;
            const conv = c.shim_ucnv_open(switch (encoding) {
                .utf16le => "UTF-16LE",
                .utf16be => "UTF-16BE",
                .utf8 => "UTF-8",
            }, &status);
            assert(status == c.U_ZERO_ERROR);

            var uchar_out: [16384]c.UChar = undefined;
            const uchar_length = c.shim_ucnv_toUChars(
                conv,
                &uchar_out,
                uchar_out.len,
                src.ptr,
                @intCast(src.len),
                &status,
            );
            assert(status == c.U_ZERO_ERROR);
            var folded_out: [16384]c.UChar = undefined;
            const folded_length = c.shim_u_strFoldCase(
                &uchar_out,
                uchar_length,
                &folded_out,
                folded_out.len,
                c.U_FOLD_CASE_DEFAULT,
                &status,
            );
            assert(status == c.U_ZERO_ERROR);

            var encoded_out: [16384]u8 = undefined;
            _ = c.shim_ucnv_fromUChars(
                conv,
                &encoded_out,
                encoded_out.len,
                &folded_out,
                folded_length,
                &status,
            );
            assert(status == c.U_ZERO_ERROR);
            c.shim_ucnv_close(conv);
        }
    };
}

fn IcuNormalizerUtf8(
    comptime getNormalizerFunc: fn (*c.UErrorCode) callconv(.c) ?*const c.UNormalizer2,
) type {
    return struct {
        fn implementation(src: []const u8) void {
            var uchar_out: [16384]c.UChar = undefined;
            var uchar_length: i32 = undefined;
            var status: c.UErrorCode = c.U_ZERO_ERROR;
            _ = c.shim_u_strFromUTF8(
                &uchar_out,
                uchar_out.len,
                &uchar_length,
                src.ptr,
                @intCast(src.len),
                &status,
            );
            assert(status == c.U_ZERO_ERROR);
            const normalizer = getNormalizerFunc(&status);
            assert(status == c.U_ZERO_ERROR);
            var normalized_out: [16384]c.UChar = undefined;
            const normalized_length = c.shim_unorm2_normalize(
                normalizer,
                &uchar_out,
                uchar_length,
                &normalized_out,
                normalized_out.len,
                &status,
            );
            assert(status == c.U_ZERO_ERROR);
            var utf8_out: [16384]u8 = undefined;
            var utf8_length: i32 = undefined;
            _ = c.shim_u_strToUTF8(
                &utf8_out,
                utf8_out.len,
                &utf8_length,
                &normalized_out,
                normalized_length,
                &status,
            );
            assert(status == c.U_ZERO_ERROR);
        }
    };
}

fn IcuNormalizerAnyEncoding(
    comptime getNormalizerFunc: fn (*c.UErrorCode) callconv(.c) ?*const c.UNormalizer2,
    comptime encoding: Encoding,
) type {
    return struct {
        fn implementation(src: []const u8) void {
            var status: c.UErrorCode = c.U_ZERO_ERROR;
            const conv = c.shim_ucnv_open(switch (encoding) {
                .utf16le => "UTF-16LE",
                .utf16be => "UTF-16BE",
                .utf8 => "UTF-8",
            }, &status);
            assert(status == c.U_ZERO_ERROR);

            var uchar_out: [16384]c.UChar = undefined;
            const uchar_length = c.shim_ucnv_toUChars(
                conv,
                &uchar_out,
                uchar_out.len,
                src.ptr,
                @intCast(src.len),
                &status,
            );
            assert(status == c.U_ZERO_ERROR);
            const normalizer = getNormalizerFunc(&status);
            assert(status == c.U_ZERO_ERROR);
            var normalized_out: [16384]c.UChar = undefined;
            const normalized_length = c.shim_unorm2_normalize(
                normalizer,
                &uchar_out,
                uchar_length,
                &normalized_out,
                normalized_out.len,
                &status,
            );
            assert(status == c.U_ZERO_ERROR);

            var encoded_out: [16384]u8 = undefined;
            _ = c.shim_ucnv_fromUChars(
                conv,
                &encoded_out,
                encoded_out.len,
                &normalized_out,
                normalized_length,
                &status,
            );
            assert(status == c.U_ZERO_ERROR);
            c.shim_ucnv_close(conv);
        }
    };
}

// TODO: report throughput AND time. Use ns as i64, size as u64. Interpretation of these results
//       can come later. Also count code points and display throughput as code points per second
const BenchResult = struct {
    name: []const u8,
    mean_ns: i96,
    sd_ns: i96,
    input_size: u64,
    data: [n_iters]i96,
};

const n_iters = 1_000;

fn trimPartialUTF8(input: []const u8) []const u8 {
    if (input.len > 0 and input[input.len - 1] >= 0xC0) {
        return input[0 .. input.len - 1];
    }
    if (input.len > 1 and input[input.len - 2] >= 0xE0) {
        return input[0 .. input.len - 2];
    }
    if (input.len > 2 and input[input.len - 3] >= 0xF0) {
        return input[0 .. input.len - 3];
    }
    return input;
}

fn runBenchmark(
    io: std.Io,
    name: []const u8,
    file: std.Io.File,
    encoding: Encoding,
    comptime impl: fn ([]const u8) void,
) !BenchResult {
    var read_buf: [4096]u8 = undefined;
    var encoded_buf: [4096]u16 = undefined;

    const stat = try file.stat(io);
    var reader_buf: [4096]u8 = undefined;
    var reader = file.reader(io, &reader_buf);
    var mean: i96 = 0;
    var results: [n_iters]i96 = undefined;
    for (0..n_iters) |i| {
        var nread = try reader.interface.readSliceShort(&read_buf);
        var carry_buffer: [4]u8 = undefined;
        var carry = std.ArrayListUnmanaged(u8).initBuffer(&carry_buffer);
        var elapsed: i96 = 0;
        while (nread > 0) {
            const buf = read_buf[0..(nread + carry.items.len)];
            const trimmed = trimPartialUTF8(buf);
            const encoded = switch (encoding) {
                .utf8 => trimmed,
                .utf16le => buf: {
                    const utf16_size = try std.unicode.utf8ToUtf16Le(&encoded_buf, trimmed);
                    break :buf std.mem.sliceAsBytes(encoded_buf[0..utf16_size]);
                },
                .utf16be => buf: {
                    const utf16_size = try std.unicode.utf8ToUtf16Le(&encoded_buf, trimmed);
                    std.mem.byteSwapAllElements(u16, encoded_buf[0..utf16_size]);
                    break :buf std.mem.sliceAsBytes(encoded_buf[0..utf16_size]);
                },
            };
            const timestamp = std.Io.Timestamp.now(io, .real);
            impl(encoded);
            elapsed += std.Io.Timestamp.untilNow(timestamp, io, .real).nanoseconds;
            carry.clearRetainingCapacity();
            carry.appendSliceBounded(buf[trimmed.len..]) catch unreachable;
            @memcpy(read_buf[0..carry.items.len], carry.items);
            nread = try reader.interface.readSliceShort(read_buf[carry.items.len..]);
        }
        mean += @divTrunc(elapsed, n_iters);
        results[i] = elapsed;
        try reader.seekTo(0);
    }

    var total_dists: i96 = 0;
    for (0..n_iters) |i| {
        const x = results[i];
        total_dists += std.math.pow(i96, x - mean, 2);
    }
    const variance = @divTrunc(total_dists, (n_iters - 1));

    return .{
        .name = name,
        .input_size = stat.size,
        .mean_ns = mean,
        .sd_ns = @trunc(std.math.sqrt(@as(f64, @floatFromInt(variance)))),
        .data = results,
    };
}
