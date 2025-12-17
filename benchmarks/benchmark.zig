const std = @import("std");
const c = @cImport({
    @cInclude("utf8norm.h");
    @cInclude("utf8proc.h");
});

const print_alignment = 30;
const full_line_length = print_alignment + 16;

const ImplementationFunc = fn ([]const u8) void;
const Encoding = enum { utf8, utf16le, utf16be };

const implementations: []const struct { []const u8, ImplementationFunc, Encoding } = &.{
    .{ "utf8norm_utf8_nfd", utf8normNormalizeUtf8NFD, .utf8 },
    .{ "utf8proc_utf8_nfd", utf8procNormalizeNFD, .utf8 },
    .{ "utf8norm_utf8_nfkd", utf8normNormalizeUtf8NFKD, .utf8 },
    .{ "utf8proc_utf8_nfkd", utf8procNormalizeNFKD, .utf8 },
    .{ "utf8norm_utf8_nfc", utf8normNormalizeUtf8NFC, .utf8 },
    .{ "utf8proc_utf8_nfc", utf8procNormalizeNFC, .utf8 },
    .{ "utf8norm_utf8_nfkc", utf8normNormalizeUtf8NFKC, .utf8 },
    .{ "utf8proc_utf8_nfkc", utf8procNormalizeNFKC, .utf8 },
    .{ "utf8norm_utf16le_nfd", utf8normNormalizeUtf16leNFD, .utf16le },
    .{ "utf8norm_utf16be_nfd", utf8normNormalizeUtf16beNFD, .utf16be },
    .{ "utf8norm_utf16le_nfkd", utf8normNormalizeUtf16leNFKD, .utf16le },
    .{ "utf8norm_utf16be_nfkd", utf8normNormalizeUtf16beNFKD, .utf16be },
};

pub fn main() !void {
    var args_it = std.process.args();
    _ = args_it.next().?;
    const input_dir_path = args_it.next() orelse return error.ArgumentError;
    const arguments = try parseArgs(&args_it);
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
        if (arguments.pattern == null or std.mem.indexOf(u8, impl[0], arguments.pattern.?) != null) {
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
    pattern: ?[]const u8 = null,
};

fn parseArgs(args: *std.process.ArgIterator) error{ArgumentError}!Arguments {
    var arguments: Arguments = .{};
    while (args.next()) |arg| {
        const eq_pos = std.mem.indexOfScalar(u8, arg, '=') orelse return error.ArgumentError;
        const arg_name = arg[0..eq_pos];
        const value = arg[eq_pos + 1 ..];

        if (std.mem.eql(u8, arg_name, "-o"))
            arguments.output_dir = value
        else if (std.mem.eql(u8, arg_name, "-p"))
            arguments.pattern = value
        else if (std.mem.eql(u8, arg_name, "-t"))
            arguments.specific_test = value
        else
            return error.ArgumentError;
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

fn utf8normNormalizeUtf8NFD(src: []const u8) void {
    var out: [100_000]u8 = undefined;
    _ = c.utf8norm_normalize_utf8_nfd(src.ptr, src.len, &out);
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

fn utf8normNormalizeUtf8NFKD(src: []const u8) void {
    var out: [100_000]u8 = undefined;
    _ = c.utf8norm_normalize_utf8_nfkd(src.ptr, src.len, &out);
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

fn utf8normNormalizeUtf8NFC(src: []const u8) void {
    var out: [100_000]u8 = undefined;
    _ = c.utf8norm_normalize_utf8_nfc(src.ptr, src.len, &out);
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

fn utf8normNormalizeUtf8NFKC(src: []const u8) void {
    var out: [100_000]u8 = undefined;
    _ = c.utf8norm_normalize_utf8_nfkc(src.ptr, src.len, &out);
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

fn utf8normNormalizeUtf16leNFD(src: []const u8) void {
    var out: [100_000]u8 = undefined;
    _ = c.utf8norm_normalize_utf16le_nfd(src.ptr, src.len, &out);
}

fn utf8normNormalizeUtf16beNFD(src: []const u8) void {
    var out: [100_000]u8 = undefined;
    _ = c.utf8norm_normalize_utf16be_nfd(src.ptr, src.len, &out);
}

fn utf8normNormalizeUtf16leNFKD(src: []const u8) void {
    var out: [100_000]u8 = undefined;
    _ = c.utf8norm_normalize_utf16le_nfkd(src.ptr, src.len, &out);
}

fn utf8normNormalizeUtf16beNFKD(src: []const u8) void {
    var out: [100_000]u8 = undefined;
    _ = c.utf8norm_normalize_utf16be_nfkd(src.ptr, src.len, &out);
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
