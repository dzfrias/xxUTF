const std = @import("std");
const c = @cImport({
    @cInclude("utf8norm.h");
});
const Allocator = std.mem.Allocator;
const assert = std.debug.assert;

const ColumnType = enum {
    utf8,
    utf16le,
    utf16be,

    fn intType(self: ColumnType) type {
        return switch (self) {
            .utf8 => u8,
            .utf16le, .utf16be => u16,
        };
    }
};

fn TestInfo(comptime col: ColumnType) type {
    return struct {
        cols: [5][]const col.intType(),
        comment: ?[]const u8,
    };
}

fn Failure(comptime col: ColumnType) type {
    return struct {
        expected: []const col.intType(),
        input: []const col.intType(),
        got: []const col.intType(),
    };
}

fn convertTest(comptime col: ColumnType, utf8_test: TestInfo(.utf8), allocator: Allocator) !TestInfo(col) {
    var new_test: TestInfo(col) = .{
        .cols = undefined,
        .comment = utf8_test.comment,
    };
    switch (col) {
        .utf8 => return utf8_test,
        .utf16le => {
            inline for (0..utf8_test.cols.len) |i| {
                const utf16_col = try std.unicode.utf8ToUtf16LeAlloc(allocator, utf8_test.cols[i]);
                new_test.cols[i] = utf16_col;
            }
            return new_test;
        },
        .utf16be => {
            inline for (0..utf8_test.cols.len) |i| {
                const utf16_col = try std.unicode.utf8ToUtf16LeAlloc(allocator, utf8_test.cols[i]);
                // Swap endianness
                for (utf16_col, 0..) |x, j| {
                    utf16_col[j] = @byteSwap(x);
                }
                new_test.cols[i] = utf16_col;
            }
            return new_test;
        },
    }
}

fn extractComment(input: []const u8) ?struct { []const u8, []const u8 } {
    const comment_start = std.mem.indexOfScalar(u8, input, '#') orelse return null;
    const part1 = input[0..comment_start];
    const comment = std.mem.trimLeft(u8, input[comment_start..], "# ");
    return .{ part1, comment };
}

fn parseTestInfo(allocator: Allocator, input: []const u8) !TestInfo(.utf8) {
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

    var test_info: TestInfo(.utf8) = undefined;
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

/// Global output buffer for normalized strings.
/// Testing is not thread safe.
var out: [64]u8 align(2) = undefined;

// Test if two normalization forms are equal.
fn testEqualNormalized(
    comptime impl: fn ([*c]const u8, usize, [*c]u8) callconv(.c) usize,
    comptime col: ColumnType,
    expected: []const col.intType(),
    input: []const col.intType(),
) ?Failure(col) {
    const input_bytes = std.mem.sliceAsBytes(input);
    const expected_bytes = std.mem.sliceAsBytes(expected);

    const nwritten = impl(input_bytes.ptr, input_bytes.len, &out);
    const normalized = out[0..nwritten];
    if (!std.mem.eql(u8, expected_bytes, normalized)) {
        const normalized_cast: []const col.intType() = @ptrCast(@alignCast(normalized));
        return .{ .expected = expected, .input = input, .got = normalized_cast };
    }
    return null;
}

const NormalizationForm = enum {
    nfd,
    nfkd,
    nfc,
    nfkc,
};

fn runTest(
    comptime form: NormalizationForm,
    comptime col: ColumnType,
    test_info: TestInfo(col),
) ?Failure(col) {
    switch (form) {
        .nfd => {
            const impl = switch (col) {
                .utf8 => c.utf8norm_normalize_utf8_nfd,
                .utf16le => c.utf8norm_normalize_utf16le_nfd,
                .utf16be => c.utf8norm_normalize_utf16be_nfd,
            };

            // c3 ==  toNFD(c1) ==  toNFD(c2) ==  toNFD(c3)
            const expected = test_info.cols[2];
            if (testEqualNormalized(impl, col, expected, test_info.cols[0])) |failure|
                return failure
            else if (testEqualNormalized(impl, col, expected, test_info.cols[1])) |failure|
                return failure
            else if (testEqualNormalized(impl, col, expected, test_info.cols[2])) |failure|
                return failure;

            // c5 ==  toNFD(c4) ==  toNFD(c5)
            const alt_expected = test_info.cols[4];
            if (testEqualNormalized(impl, col, alt_expected, test_info.cols[3])) |failure|
                return failure
            else if (testEqualNormalized(impl, col, alt_expected, test_info.cols[4])) |failure|
                return failure;

            return null;
        },
        .nfc => {
            const impl = switch (col) {
                .utf8 => c.utf8norm_normalize_utf8_nfc,
                .utf16le => c.utf8norm_normalize_utf16le_nfc,
                .utf16be => c.utf8norm_normalize_utf16be_nfc,
            };

            // c2 ==  toNFC(c1) ==  toNFC(c2) ==  toNFC(c3)
            const expected = test_info.cols[1];
            if (testEqualNormalized(impl, col, expected, test_info.cols[0])) |failure|
                return failure
            else if (testEqualNormalized(impl, col, expected, test_info.cols[1])) |failure|
                return failure
            else if (testEqualNormalized(impl, col, expected, test_info.cols[2])) |failure|
                return failure;

            // c4 ==  toNFC(c4) ==  toNFC(c5)
            const alt_expected = test_info.cols[3];
            if (testEqualNormalized(impl, col, alt_expected, test_info.cols[3])) |failure|
                return failure
            else if (testEqualNormalized(impl, col, alt_expected, test_info.cols[4])) |failure|
                return failure;

            return null;
        },
        .nfkd => {
            const impl = switch (col) {
                .utf8 => c.utf8norm_normalize_utf8_nfkd,
                .utf16le => c.utf8norm_normalize_utf16le_nfkd,
                .utf16be => c.utf8norm_normalize_utf16be_nfkd,
            };

            // c5 == toNFKD(c1) == toNFKD(c2) == toNFKD(c3) == toNFKD(c4) == toNFKD(c5)
            const expected = test_info.cols[4];
            if (testEqualNormalized(impl, col, expected, test_info.cols[0])) |failure|
                return failure
            else if (testEqualNormalized(impl, col, expected, test_info.cols[1])) |failure|
                return failure
            else if (testEqualNormalized(impl, col, expected, test_info.cols[2])) |failure|
                return failure
            else if (testEqualNormalized(impl, col, expected, test_info.cols[3])) |failure|
                return failure
            else if (testEqualNormalized(impl, col, expected, test_info.cols[4])) |failure|
                return failure;

            return null;
        },
        .nfkc => {
            const impl = switch (col) {
                .utf8 => c.utf8norm_normalize_utf8_nfkc,
                .utf16le => c.utf8norm_normalize_utf16le_nfkc,
                .utf16be => c.utf8norm_normalize_utf16be_nfkc,
            };

            const expected = test_info.cols[3];
            // c4 == toNFKC(c1) == toNFKC(c2) == toNFKC(c3) == toNFKC(c4) == toNFKC(c5)
            if (testEqualNormalized(impl, col, expected, test_info.cols[0])) |failure|
                return failure
            else if (testEqualNormalized(impl, col, expected, test_info.cols[1])) |failure|
                return failure
            else if (testEqualNormalized(impl, col, expected, test_info.cols[2])) |failure|
                return failure
            else if (testEqualNormalized(impl, col, expected, test_info.cols[3])) |failure|
                return failure
            else if (testEqualNormalized(impl, col, expected, test_info.cols[4])) |failure|
                return failure;

            return null;
        },
    }
}

fn writeHex(comptime col: ColumnType, writer: anytype, s: []const col.intType()) !void {
    switch (col) {
        .utf8 => {
            const utf8_view = std.unicode.Utf8View.init(s) catch {
                for (s) |b| {
                    try writer.print("{X:0>2} ", .{b});
                }
                try writer.writeAll("(invalid UTF-8)\n");
                return;
            };
            var code_points = utf8_view.iterator();
            while (code_points.nextCodepoint()) |cp| {
                try writer.print("{X:0>6} ", .{cp});
            }
        },
        .utf16le => {
            var code_points = std.unicode.Utf16LeIterator.init(s);
            // Iterate through once to see if there are any errors. This API is relatively clunky.
            while (code_points.nextCodepoint() catch {
                for (s) |b| {
                    try writer.print("{X:0>4} ", .{b});
                }
                try writer.writeAll("(invalid UTF-16LE)\n");
                return;
            }) |cp| {
                _ = cp;
            }
            code_points = std.unicode.Utf16LeIterator.init(s);
            while (try code_points.nextCodepoint()) |cp| {
                try writer.print("{X:0>6} ", .{cp});
            }
        },
        .utf16be => {
            // TODO: we need to do one of two things: swap the endianness of `s`, or implement
            //       a Utf16BeIterator.
            var code_points = std.unicode.Utf16LeIterator.init(s);
            // Iterate through once to see if there are any errors. This API is relatively clunky.
            while (code_points.nextCodepoint() catch {
                for (s) |b| {
                    try writer.print("{X:0>4} ", .{b});
                }
                try writer.writeAll("(invalid UTF-16BE)\n");
                return;
            }) |cp| {
                _ = cp;
            }
            code_points = std.unicode.Utf16LeIterator.init(s);
            while (try code_points.nextCodepoint()) |cp| {
                try writer.print("{X:0>6} ", .{cp});
            }
        },
    }
    try writer.writeByte('\n');
}

fn printFailure(comptime col: ColumnType, writer: anytype, failure: Failure(col)) !void {
    try writer.writeAll("input:    ");
    try writeHex(col, writer, failure.input);
    try writer.writeAll("expected: ");
    try writeHex(col, writer, failure.expected);
    try writer.writeAll("got:      ");
    try writeHex(col, writer, failure.got);
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
        }
        if (std.mem.startsWith(u8, line, "@Part")) {
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

        inline for (comptime std.meta.tags(ColumnType)) |col| {
            const converted = try convertTest(col, test_info, allocator);
            inline for (comptime std.meta.tags(NormalizationForm)) |form| {
                if (runTest(form, col, converted)) |failure| {
                    const form_name = switch (form) {
                        .nfd => "NFD",
                        .nfc => "NFC",
                        .nfkd => "NFKD",
                        .nfkc => "NFKC",
                    };
                    const col_name = switch (col) {
                        .utf8 => "UTF-8",
                        .utf16le => "UTF-16LE",
                        .utf16be => "UTF-16BE",
                    };
                    try stderr.writer().print(
                        col_name ++ " " ++ form_name ++ " " ++ "test at line {} failed: {?s}\n",
                        .{ i + 1, test_info.comment },
                    );
                    try printFailure(col, stderr.writer(), failure);
                    return error.Failed;
                }
            }
        }
    }
    root_node.end();
    try stderr.writeAll("All tests passed!\n");
}
