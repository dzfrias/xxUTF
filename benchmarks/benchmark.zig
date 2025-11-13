const std = @import("std");
const c = @cImport({
    @cInclude("utf8norm.h");
    @cInclude("utf8proc.h");
});

const print_alignment = 30;

pub fn main() !void {
    var args_it = std.process.args();
    _ = args_it.next().?;
    const input_dir_path = args_it.next() orelse return error.ArgumentError;
    const output_dir_path = args_it.next() orelse return error.ArgumentError;
    const specific_test = args_it.next();
    var input_dir = try std.fs.cwd().openDir(input_dir_path, .{ .iterate = true });
    defer input_dir.close();
    std.fs.cwd().makeDir(output_dir_path) catch {};
    var out_dir = try std.fs.cwd().openDir(output_dir_path, .{});
    defer out_dir.close();
    const stdout = std.io.getStdOut();

    try writeHeader(stdout.writer(), "UTF8NORM");
    try benchmarkImplementation(
        stdout.writer(),
        input_dir,
        out_dir,
        specific_test,
        utf8norm_normalize,
    );
    // try stdout.writer().writeByte('\n');
    // try writeHeader(stdout.writer(), "UTF8PROC");
    // try benchmarkImplementation(stdout.writer(), input_dir, out_dir, specific_test, utf8proc_normalize);
}

fn writeHeader(out: anytype, title: []const u8) !void {
    try out.writeByteNTimes('=', (print_alignment / 2) + 4);
    try out.writeAll(title);
    try out.writeByteNTimes('=', (print_alignment / 2) + 4);
    try out.writeByte('\n');
}

fn benchmarkImplementation(
    out: anytype,
    inputs: std.fs.Dir,
    data_out: std.fs.Dir,
    specific_test: ?[]const u8,
    comptime impl: fn ([]const u8) void,
) !void {
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
        const result = try runBenchmark(file, impl);
        try out.print(
            std.fmt.comptimePrint("{{s: >{}}}: {{d:.3}}±{{d:.3}}ms\n", .{print_alignment}),
            .{ entry.name, result.mean_ms, result.sd_ms },
        );

        const out_file = try data_out.createFile(entry.name, .{});
        defer out_file.close();
        for (result.data) |x| {
            try out_file.writer().print("{d:.3}\n", .{x});
        }
    }
}

fn utf8norm_normalize(src: []const u8) void {
    var out: [100_000]u8 = undefined;
    _ = c.utf8norm_normalize_utf8_nfd(src.ptr, src.len, &out);
}

fn utf8proc_normalize(src: []const u8) void {
    var out: [*c]c_char = undefined;
    _ = c.utf8proc_map(
        src.ptr,
        @intCast(src.len),
        &out,
        c.UTF8PROC_STABLE | c.UTF8PROC_DECOMPOSE,
    );
    c.free(out);
}

const BenchResult = struct {
    mean_ms: f64,
    sd_ms: f64,
    data: [n_iters]f64,
};

const n_iters = 500;

fn trim_partial_utf8(input: []const u8) []const u8 {
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

fn runBenchmark(file: std.fs.File, comptime impl: fn ([]const u8) void) !BenchResult {
    var read_buf: [4096]u8 = undefined;

    var sum: f64 = 0;
    var results: [n_iters]f64 = undefined;
    var timer = try std.time.Timer.start();
    for (0..n_iters) |i| {
        var nread = try file.read(&read_buf);
        var carry: std.BoundedArray(u8, 4) = .{};
        while (nread > 0) {
            const buf = read_buf[0..(nread + carry.len)];
            const trimmed = trim_partial_utf8(buf);
            impl(trimmed);
            carry.clear();
            carry.appendSlice(buf[trimmed.len..]) catch unreachable;
            @memcpy(read_buf[0..carry.len], carry.slice());
            nread = try file.read(read_buf[carry.len..]);
        }
        const elapsed: f64 = @floatFromInt(timer.lap());
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
