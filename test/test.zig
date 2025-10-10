const std = @import("std");
const c = @cImport({
    @cInclude("utf8norm.h");
});
const Allocator = std.mem.Allocator;
const assert = std.debug.assert;

const TestInfo = struct {
    cols: [5][]const u8,
    comment: ?[]const u8,
};

fn extractComment(input: []const u8) ?struct { []const u8, []const u8 } {
    const comment_start = std.mem.indexOfScalar(u8, input, '#') orelse return null;
    const part1 = input[0..comment_start];
    const comment = std.mem.trimLeft(u8, input[comment_start..], "# ");
    return .{ part1, comment };
}

fn parseTestInfo(allocator: Allocator, input: []const u8) !TestInfo {
    const comment_extract = extractComment(input);
    const data = if (comment_extract) |e| e[0] else input;
    const comment = if (comment_extract) |e| e[1] else null;

    var raw_cols: [5][]const u8 = undefined;
    {
        const trimmed = std.mem.trimRight(u8, data, "; ");
        var cols_it = std.mem.splitScalar(u8, trimmed, ';');
        var i: usize = 0;
        while (cols_it.next()) |col| : (i += 1) raw_cols[i] = col;
    }

    var test_info: TestInfo = undefined;
    test_info.comment = comment;
    for (raw_cols[0..5], 0..) |col, i| {
        var s: std.ArrayListUnmanaged(u8) = .empty;
        var p: usize = 0;
        var code_point_it = std.mem.splitScalar(u8, col, ' ');
        while (code_point_it.next()) |hex_cp| {
            const code_point = try std.fmt.parseInt(u21, hex_cp, 16);
            const size = try std.unicode.utf8CodepointSequenceLength(code_point);
            try s.resize(allocator, s.items.len + size);
            _ = try std.unicode.utf8Encode(code_point, s.items[p..]);
            p += size;
        }
        test_info.cols[i] = try s.toOwnedSlice(allocator);
    }

    return test_info;
}

const Failure = struct {
    expected: []const u8,
    input: []const u8,
    got: []const u8,
};

/// Global output buffer for normalized strings.
/// Testing is not thread safe.
var out: [64]u8 = undefined;

// Test if two normalization forms are equal.
fn testEqualNormalized(
    comptime impl: fn ([*c]const u8, usize, [*c]u8) callconv(.c) usize,
    expected: []const u8,
    input: []const u8,
) ?Failure {
    const nwritten = impl(input.ptr, input.len, &out);
    const normalized = out[0..nwritten];
    if (!std.mem.eql(u8, expected, normalized)) {
        return .{ .expected = expected, .input = input, .got = normalized };
    } else {
        return null;
    }
}

fn normalizeNFC(input: [*c]const u8, len: usize, buf: [*c]u8) callconv(.c) usize {
    var tmp: [64]u8 = undefined;
    const nwritten = c.utf8norm_normalize_utf8_nfd(input, len, &tmp);
    // TODO: eventually, we shouldn't have to call NFD first on the user end
    return c.utf8norm_normalize_utf8_nfc(&tmp, nwritten, buf);
}

fn testNFD(test_info: TestInfo) ?Failure {
    const expected = test_info.cols[2];

    // c3 ==  toNFD(c1) ==  toNFD(c2) ==  toNFD(c3)
    if (testEqualNormalized(c.utf8norm_normalize_utf8_nfd, expected, test_info.cols[0])) |failure|
        return failure
    else if (testEqualNormalized(c.utf8norm_normalize_utf8_nfd, expected, test_info.cols[1])) |failure|
        return failure
    else if (testEqualNormalized(c.utf8norm_normalize_utf8_nfd, expected, test_info.cols[2])) |failure|
        return failure;

    // c5 ==  toNFD(c4) ==  toNFD(c5)
    const alt_expected = test_info.cols[4];
    if (testEqualNormalized(c.utf8norm_normalize_utf8_nfd, alt_expected, test_info.cols[3])) |failure|
        return failure
    else if (testEqualNormalized(c.utf8norm_normalize_utf8_nfd, alt_expected, test_info.cols[4])) |failure|
        return failure;

    return null;
}

fn testNFC(test_info: TestInfo) ?Failure {
    const expected = test_info.cols[1];

    // c2 ==  toNFC(c1) ==  toNFC(c2) ==  toNFC(c3)
    if (testEqualNormalized(normalizeNFC, expected, test_info.cols[0])) |failure|
        return failure
    else if (testEqualNormalized(normalizeNFC, expected, test_info.cols[1])) |failure|
        return failure
    else if (testEqualNormalized(normalizeNFC, expected, test_info.cols[2])) |failure|
        return failure;

    // c4 ==  toNFC(c4) ==  toNFC(c5)
    const alt_expected = test_info.cols[3];
    if (testEqualNormalized(normalizeNFC, alt_expected, test_info.cols[3])) |failure|
        return failure
    else if (testEqualNormalized(normalizeNFC, alt_expected, test_info.cols[4])) |failure|
        return failure;

    return null;
}

fn writeHex(writer: anytype, s: []const u8) !void {
    if (std.unicode.utf8ValidateSlice(s)) {
        var code_points: std.unicode.Utf8Iterator = .{ .bytes = s, .i = 0 };
        while (code_points.nextCodepoint()) |cp| {
            try writer.print("{X:0>6} ", .{cp});
        }
    } else {
        for (s) |b| {
            try writer.print("{X:0>2} ", .{b});
        }
        try writer.writeAll("(invalid UTF-8)");
    }
    try writer.writeByte('\n');
}

pub fn main() !void {
    var arena = std.heap.ArenaAllocator.init(std.heap.page_allocator);
    defer arena.deinit();
    const allocator = arena.allocator();

    var args = std.process.args();
    _ = args.next() orelse @panic("should have initial argument");
    const arg1 = args.next() orelse return error.NeedArgument;

    const file = try std.fs.openFileAbsolute(arg1, .{ .mode = .read_only });
    defer file.close();
    const stderr = std.io.getStdErr();

    var in_buf: [1024]u8 = undefined;
    var i: usize = 0;
    var root_node = std.Progress.start(.{ .root_name = "tests" });
    var current_part: ?std.Progress.Node = null;
    while (try file.reader().readUntilDelimiterOrEof(&in_buf, '\n')) |line| : (i += 1) {
        if (std.mem.startsWith(u8, line, "#")) {
            continue;
        } else if (std.mem.startsWith(u8, line, "@Part")) {
            if (current_part) |n| n.end();
            const part_num = line["@Part".len];
            const comment = if (extractComment(line)) |e| e[1] else null;
            const part_name = try if (comment) |s|
                std.fmt.allocPrint(allocator, "part {c} ({s})", .{ part_num, s })
            else
                std.fmt.allocPrint(allocator, "part {c}", .{part_num});
            current_part = root_node.start(part_name, 0);
            continue;
        }
        var test_node = current_part.?.start(try std.fmt.allocPrint(allocator, "test (line {})", .{i + 1}), 1);
        defer test_node.end();
        const test_info = try parseTestInfo(allocator, line);
        if (testNFD(test_info)) |failure| {
            try stderr.writer().print(
                "NFD test at line {} failed: {?s}\n",
                .{ i + 1, test_info.comment },
            );
            try stderr.writeAll("input:    ");
            try writeHex(stderr.writer(), failure.input);
            try stderr.writeAll("expected: ");
            try writeHex(stderr.writer(), failure.expected);
            try stderr.writeAll("got:      ");
            try writeHex(stderr.writer(), failure.got);
            return error.Failed;
        }
        if (testNFC(test_info)) |failure| {
            try stderr.writer().print(
                "NFC test at line {} failed: {?s}\n",
                .{ i + 1, test_info.comment },
            );
            try stderr.writeAll("input:    ");
            try writeHex(stderr.writer(), failure.input);
            try stderr.writeAll("expected: ");
            try writeHex(stderr.writer(), failure.expected);
            try stderr.writeAll("got:      ");
            try writeHex(stderr.writer(), failure.got);
            return error.Failed;
        }
    }
    root_node.end();
    try stderr.writeAll("All tests passed!\n");
}
