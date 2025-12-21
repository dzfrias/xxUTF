const std = @import("std");
const c = @cImport({
    @cInclude("xxutf.h");
    @cInclude("utf8proc.h");
    @cInclude("xxutf_shim.h");
});
const assert = std.debug.assert;
const Allocator = std.mem.Allocator;

const print_alignment = 30;
const full_line_length = print_alignment + 16;

const ImplementationFunc = fn ([]const u8) void;
const Encoding = enum { utf8, utf16le, utf16be };

const implementations: []const struct { []const u8, ImplementationFunc, Encoding } = &.{
    .{ "xxutf_utf8_nfd", xxutfNormalizeUtf8NFD, .utf8 },
    .{ "utf8proc_utf8_nfd", utf8procNormalizeNFD, .utf8 },
    .{ "icu_utf8_nfd", IcuNormalizerUtf8(c.shim_unorm2_getNFDInstance).implementation, .utf8 },
    .{ "xxutf_utf8_nfkd", xxutfNormalizeUtf8NFKD, .utf8 },
    .{ "utf8proc_utf8_nfkd", utf8procNormalizeNFKD, .utf8 },
    .{ "icu_utf8_nfkd", IcuNormalizerUtf8(c.shim_unorm2_getNFKDInstance).implementation, .utf8 },
    .{ "xxutf_utf8_nfc", xxutfNormalizeUtf8NFC, .utf8 },
    .{ "utf8proc_utf8_nfc", utf8procNormalizeNFC, .utf8 },
    .{ "icu_utf8_nfc", IcuNormalizerUtf8(c.shim_unorm2_getNFCInstance).implementation, .utf8 },
    .{ "xxutf_utf8_nfkc", xxutfNormalizeUtf8NFKC, .utf8 },
    .{ "utf8proc_utf8_nfkc", utf8procNormalizeNFKC, .utf8 },
    .{ "icu_utf8_nfkc", IcuNormalizerUtf8(c.shim_unorm2_getNFKCInstance).implementation, .utf8 },
    .{ "xxutf_utf16le_nfd", xxutfNormalizeUtf16leNFD, .utf16le },
    .{ "xxutf_utf16be_nfd", xxutfNormalizeUtf16beNFD, .utf16be },
    .{ "xxutf_utf16le_nfkd", xxutfNormalizeUtf16leNFKD, .utf16le },
    .{ "xxutf_utf16be_nfkd", xxutfNormalizeUtf16beNFKD, .utf16be },
    .{ "xxutf_utf16le_nfc", xxutfNormalizeUtf16leNFC, .utf16le },
    .{ "xxutf_utf16be_nfc", xxutfNormalizeUtf16beNFC, .utf16be },
    .{ "xxutf_utf16le_nfkc", xxutfNormalizeUtf16leNFKC, .utf16le },
    .{ "xxutf_utf16be_nfkc", xxutfNormalizeUtf16beNFKC, .utf16be },
};

pub fn main() !void {
    var dbg_allocator: std.heap.DebugAllocator(.{}) = .init;
    defer assert(dbg_allocator.deinit() == .ok);
    const allocator = dbg_allocator.allocator();

    var args_it = std.process.args();
    _ = args_it.next().?;
    const input_dir_path = args_it.next() orelse return error.ArgumentError;
    const arguments = try parseArgs(allocator, &args_it);
    var input_dir = try std.fs.cwd().openDir(input_dir_path, .{ .iterate = true });
    defer input_dir.close();
    var out_dir = if (arguments.output_dir) |path|
        try std.fs.cwd().makeOpenPath(path, .{})
    else
        null;
    defer if (out_dir) |*dir| dir.close();
    var stdout_buffer: [1024]u8 = undefined;
    var stdout_writer = std.fs.File.stdout().writer(&stdout_buffer);
    const stdout = &stdout_writer.interface;

    inline for (implementations) |impl| {
        if (arguments.patterns == null or match: {
            for (arguments.patterns.?) |pattern| {
                if (std.mem.indexOf(u8, impl[0], pattern) != null) {
                    break :match true;
                }
            }
            break :match false;
        }) {
            try benchmarkImplementation(
                impl[0],
                stdout,
                input_dir,
                out_dir,
                arguments.specific_test,
                impl[2],
                impl[1],
            );
            try stdout.writeByte('\n');
            try stdout.flush();
        }
    }
}

const Arguments = struct {
    output_dir: ?[]const u8 = null,
    specific_test: ?[]const u8 = null,
    patterns: ?[]const []const u8 = null,
};

fn parseArgs(allocator: Allocator, args: *std.process.ArgIterator) !Arguments {
    var arguments: Arguments = .{};
    while (args.next()) |arg| {
        const eq_pos = std.mem.indexOfScalar(u8, arg, '=') orelse return error.ArgumentError;
        const arg_name = arg[0..eq_pos];
        const value = arg[eq_pos + 1 ..];

        if (std.mem.eql(u8, arg_name, "-o")) {
            arguments.output_dir = value;
        } else if (std.mem.eql(u8, arg_name, "-p")) {
            var patterns: std.ArrayListUnmanaged([]const u8) = .empty;
            defer patterns.deinit(allocator);
            const first_sep_pos = std.mem.indexOfScalar(u8, value, ',') orelse value.len;
            try patterns.append(allocator, value[0..first_sep_pos]);
            var p = first_sep_pos + 1;
            while (p < value.len) {
                const sep_pos = std.mem.indexOfScalar(u8, value[p..], ',') orelse value[p..].len;
                try patterns.append(allocator, value[p .. p + sep_pos]);
                p += sep_pos + 1;
            }
            arguments.patterns = try patterns.toOwnedSlice(allocator);
        } else if (std.mem.eql(u8, arg_name, "-t")) {
            arguments.specific_test = value;
        } else {
            return error.ArgumentError;
        }
    }

    return arguments;
}

fn writeHeader(out: *std.Io.Writer, title: []const u8) !void {
    try out.writeAll(title);
    try out.splatByteAll('-', full_line_length - title.len);
    try out.writeByte('\n');
}

fn benchmarkImplementation(
    name: []const u8,
    out: *std.Io.Writer,
    inputs: std.fs.Dir,
    data_out: ?std.fs.Dir,
    specific_test: ?[]const u8,
    encoding: Encoding,
    comptime impl: fn ([]const u8) void,
) !void {
    try writeHeader(out, name);

    var out_dir = if (data_out) |dir| try dir.makeOpenPath(name, .{}) else null;
    defer if (out_dir) |*dir| dir.close();

    var inputs_it = inputs.iterate();
    while (try inputs_it.next()) |entry| {
        if (entry.kind != .file) {
            continue;
        }
        if (specific_test) |s| {
            if (!std.ascii.eqlIgnoreCase(entry.name, s)) {
                continue;
            }
        }

        const file = try inputs.openFile(entry.name, .{});
        defer file.close();
        const result = try runBenchmark(file, encoding, impl);
        try out.print(
            std.fmt.comptimePrint("{{s: >{}}}: {{d:.3}}±{{d:.3}}ms\n", .{print_alignment}),
            .{ entry.name, result.mean_ms, result.sd_ms },
        );
        try out.flush();

        if (out_dir) |dir| {
            const out_file = try dir.createFile(entry.name, .{});
            defer out_file.close();
            var out_file_buffer: [64]u8 = undefined;
            var out_file_writer = out_file.writer(&out_file_buffer);
            for (result.data) |x| {
                try out_file_writer.interface.print("{d:.3}\n", .{x});
                try out_file_writer.interface.flush();
            }
        }
    }
}

fn xxutfNormalizeUtf8NFD(src: []const u8) void {
    var out: [100_000]u8 = undefined;
    _ = c.xxutf_normalize_utf8_nfd(src.ptr, src.len, &out);
}

fn utf8procNormalizeNFD(src: []const u8) void {
    var out: [*c]c_char = undefined;
    _ = c.utf8proc_map(
        src.ptr,
        @intCast(src.len),
        &out,
        c.UTF8PROC_STABLE | c.UTF8PROC_DECOMPOSE,
    );
    c.free(out);
}

fn xxutfNormalizeUtf8NFKD(src: []const u8) void {
    var out: [100_000]u8 = undefined;
    _ = c.xxutf_normalize_utf8_nfkd(src.ptr, src.len, &out);
}

fn utf8procNormalizeNFKD(src: []const u8) void {
    var out: [*c]c_char = undefined;
    _ = c.utf8proc_map(
        src.ptr,
        @intCast(src.len),
        &out,
        c.UTF8PROC_STABLE | c.UTF8PROC_DECOMPOSE | c.UTF8PROC_COMPAT,
    );
    c.free(out);
}

fn xxutfNormalizeUtf8NFC(src: []const u8) void {
    var out: [100_000]u8 = undefined;
    _ = c.xxutf_normalize_utf8_nfc(src.ptr, src.len, &out);
}

fn utf8procNormalizeNFC(src: []const u8) void {
    var out: [*c]c_char = undefined;
    _ = c.utf8proc_map(
        src.ptr,
        @intCast(src.len),
        &out,
        c.UTF8PROC_STABLE | c.UTF8PROC_COMPOSE,
    );
    c.free(out);
}

fn xxutfNormalizeUtf8NFKC(src: []const u8) void {
    var out: [100_000]u8 = undefined;
    _ = c.xxutf_normalize_utf8_nfkc(src.ptr, src.len, &out);
}

fn utf8procNormalizeNFKC(src: []const u8) void {
    var out: [*c]c_char = undefined;
    _ = c.utf8proc_map(
        src.ptr,
        @intCast(src.len),
        &out,
        c.UTF8PROC_STABLE | c.UTF8PROC_COMPOSE | c.UTF8PROC_COMPAT,
    );
    c.free(out);
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

fn IcuNormalizerUtf8(comptime getNormalizerFunc: fn (*c.UErrorCode) callconv(.c) ?*const c.UNormalizer2) type {
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

fn icuNormalizeUtf8NFD(src: []const u8) void {
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
    const normalizer = c.shim_unorm2_getNFDInstance(&status);
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

const BenchResult = struct {
    mean_ms: f64,
    sd_ms: f64,
    data: [n_iters]f64,
};

const n_iters = 500;

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
    file: std.fs.File,
    encoding: Encoding,
    comptime impl: fn ([]const u8) void,
) !BenchResult {
    var read_buf: [4096]u8 = undefined;
    var encoded_buf: [4096]u16 = undefined;

    var sum: f64 = 0;
    var results: [n_iters]f64 = undefined;
    var timer = try std.time.Timer.start();
    for (0..n_iters) |i| {
        var nread = try file.read(&read_buf);
        var carry_buffer: [4]u8 = undefined;
        var carry = std.ArrayListUnmanaged(u8).initBuffer(&carry_buffer);
        var elapsed: f64 = 0;
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
            timer.reset();
            impl(encoded);
            elapsed += @floatFromInt(timer.read());
            carry.clearRetainingCapacity();
            carry.appendSliceBounded(buf[trimmed.len..]) catch unreachable;
            @memcpy(read_buf[0..carry.items.len], carry.items);
            nread = try file.read(read_buf[carry.items.len..]);
        }
        const elapsed_ms: f64 = elapsed / std.time.ns_per_ms;
        sum += elapsed_ms;
        results[i] = elapsed_ms;
        try file.seekTo(0);
    }
    const mean = sum / n_iters;

    var total_dists: f64 = 0;
    for (0..n_iters) |i| {
        const x = results[i];
        total_dists += std.math.pow(f64, x - mean, 2);
    }
    const variance = total_dists / (n_iters - 1);

    return .{ .mean_ms = mean, .sd_ms = std.math.sqrt(variance), .data = results };
}
