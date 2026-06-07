const std = @import("std");
const c = @import("c");
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
    return union(enum) {
        contents_mismatch: struct {
            expected: []const col.intType(),
            input: []const col.intType(),
            got: []const col.intType(),
        },
        lengths_mismatch: struct {
            expected: usize,
            input: []const col.intType(),
            got: usize,
        },
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
                std.mem.byteSwapAllElements(u16, utf16_col);
                new_test.cols[i] = utf16_col;
            }
            return new_test;
        },
    }
}

fn extractComment(input: []const u8) ?struct { []const u8, []const u8 } {
    const cut = std.mem.cutScalar(u8, input, '#') orelse return null;
    const comment = std.mem.cutPrefix(u8, cut[1], " ") orelse cut[1];
    return .{ cut[0], comment };
}

fn parseTestInfo(allocator: Allocator, input: []const u8) !TestInfo(.utf8) {
    const comment_extract = extractComment(input);
    const data = if (comment_extract) |e| e[0] else input;
    const comment = if (comment_extract) |e| e[1] else null;

    var raw_cols: [5][]const u8 = undefined;
    {
        const trimmed = std.mem.cutSuffix(u8, data, "; ") orelse @panic("bad data found");
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
    comptime check_impl: ?fn ([*c]const u8, usize, [*c]usize) callconv(.c) bool,
    comptime col: ColumnType,
    expected: []const col.intType(),
    input: []const col.intType(),
) ?Failure(col) {
    const input_bytes = std.mem.sliceAsBytes(input);
    const expected_bytes = std.mem.sliceAsBytes(expected);

    const nwritten = impl(input_bytes.ptr, input_bytes.len, &out);
    const normalized = out[0..nwritten];
    if (!std.mem.eql(u8, expected_bytes, normalized)) {
        const normalized_cast = std.mem.bytesAsSlice(col.intType(), normalized);
        return .{ .contents_mismatch = .{
            .expected = expected,
            .input = input,
            .got = normalized_cast,
        } };
    }
    if (check_impl) |f| {
        var expect_nwritten: usize = undefined;
        const check = f(input_bytes.ptr, input_bytes.len, &expect_nwritten);
        if (expect_nwritten != nwritten) {
            return .{ .lengths_mismatch = .{
                .expected = expect_nwritten,
                .input = input,
                .got = nwritten,
            } };
        }
        if (check and !std.mem.eql(u8, input_bytes, normalized)) {
            const normalized_cast = std.mem.bytesAsSlice(col.intType(), normalized);
            return .{ .contents_mismatch = .{
                .expected = input,
                .input = input,
                .got = normalized_cast,
            } };
        }
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
                .utf8 => c.xxutf_normalize_utf8_nfd,
                .utf16le => c.xxutf_normalize_utf16le_nfd,
                .utf16be => c.xxutf_normalize_utf16be_nfd,
            };
            const check_impl = switch (col) {
                .utf8 => c.xxutf_normalize_utf8_nfd_check,
                .utf16le => c.xxutf_normalize_utf16le_nfd_check,
                .utf16be => c.xxutf_normalize_utf16be_nfd_check,
            };

            // c3 ==  toNFD(c1) ==  toNFD(c2) ==  toNFD(c3)
            const expected = test_info.cols[2];
            if (testEqualNormalized(impl, check_impl, col, expected, test_info.cols[0])) |failure|
                return failure
            else if (testEqualNormalized(impl, check_impl, col, expected, test_info.cols[1])) |failure|
                return failure
            else if (testEqualNormalized(impl, check_impl, col, expected, test_info.cols[2])) |failure|
                return failure;

            // c5 ==  toNFD(c4) ==  toNFD(c5)
            const alt_expected = test_info.cols[4];
            if (testEqualNormalized(impl, check_impl, col, alt_expected, test_info.cols[3])) |failure|
                return failure
            else if (testEqualNormalized(impl, check_impl, col, alt_expected, test_info.cols[4])) |failure|
                return failure;

            return null;
        },
        .nfc => {
            const impl = switch (col) {
                .utf8 => c.xxutf_normalize_utf8_nfc,
                .utf16le => c.xxutf_normalize_utf16le_nfc,
                .utf16be => c.xxutf_normalize_utf16be_nfc,
            };

            // c2 ==  toNFC(c1) ==  toNFC(c2) ==  toNFC(c3)
            const expected = test_info.cols[1];
            if (testEqualNormalized(impl, null, col, expected, test_info.cols[0])) |failure|
                return failure
            else if (testEqualNormalized(impl, null, col, expected, test_info.cols[1])) |failure|
                return failure
            else if (testEqualNormalized(impl, null, col, expected, test_info.cols[2])) |failure|
                return failure;

            // c4 ==  toNFC(c4) ==  toNFC(c5)
            const alt_expected = test_info.cols[3];
            if (testEqualNormalized(impl, null, col, alt_expected, test_info.cols[3])) |failure|
                return failure
            else if (testEqualNormalized(impl, null, col, alt_expected, test_info.cols[4])) |failure|
                return failure;

            return null;
        },
        .nfkd => {
            const impl = switch (col) {
                .utf8 => c.xxutf_normalize_utf8_nfkd,
                .utf16le => c.xxutf_normalize_utf16le_nfkd,
                .utf16be => c.xxutf_normalize_utf16be_nfkd,
            };
            const check_impl = switch (col) {
                .utf8 => c.xxutf_normalize_utf8_nfkd_check,
                .utf16le => c.xxutf_normalize_utf16le_nfkd_check,
                .utf16be => c.xxutf_normalize_utf16be_nfkd_check,
            };

            // c5 == toNFKD(c1) == toNFKD(c2) == toNFKD(c3) == toNFKD(c4) == toNFKD(c5)
            const expected = test_info.cols[4];
            if (testEqualNormalized(impl, check_impl, col, expected, test_info.cols[0])) |failure|
                return failure
            else if (testEqualNormalized(impl, check_impl, col, expected, test_info.cols[1])) |failure|
                return failure
            else if (testEqualNormalized(impl, check_impl, col, expected, test_info.cols[2])) |failure|
                return failure
            else if (testEqualNormalized(impl, check_impl, col, expected, test_info.cols[3])) |failure|
                return failure
            else if (testEqualNormalized(impl, check_impl, col, expected, test_info.cols[4])) |failure|
                return failure;

            return null;
        },
        .nfkc => {
            const impl = switch (col) {
                .utf8 => c.xxutf_normalize_utf8_nfkc,
                .utf16le => c.xxutf_normalize_utf16le_nfkc,
                .utf16be => c.xxutf_normalize_utf16be_nfkc,
            };

            const expected = test_info.cols[3];
            // c4 == toNFKC(c1) == toNFKC(c2) == toNFKC(c3) == toNFKC(c4) == toNFKC(c5)
            if (testEqualNormalized(impl, null, col, expected, test_info.cols[0])) |failure|
                return failure
            else if (testEqualNormalized(impl, null, col, expected, test_info.cols[1])) |failure|
                return failure
            else if (testEqualNormalized(impl, null, col, expected, test_info.cols[2])) |failure|
                return failure
            else if (testEqualNormalized(impl, null, col, expected, test_info.cols[3])) |failure|
                return failure
            else if (testEqualNormalized(impl, null, col, expected, test_info.cols[4])) |failure|
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
            var utf16le_buffer: [128]u16 = undefined;
            @memcpy(utf16le_buffer[0..s.len], s);
            std.mem.byteSwapAllElements(u16, utf16le_buffer[0..s.len]);
            var code_points = std.unicode.Utf16LeIterator.init(utf16le_buffer[0..s.len]);
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
            code_points = std.unicode.Utf16LeIterator.init(utf16le_buffer[0..s.len]);
            while (try code_points.nextCodepoint()) |cp| {
                try writer.print("{X:0>6} ", .{cp});
            }
        },
    }
    try writer.writeByte('\n');
}

fn printFailure(comptime col: ColumnType, writer: *std.Io.Writer, failure: Failure(col)) !void {
    switch (failure) {
        .contents_mismatch => |info| {
            try writer.writeAll("input:    ");
            try writeHex(col, writer, info.input);
            try writer.writeAll("expected: ");
            try writeHex(col, writer, info.expected);
            try writer.writeAll("got:      ");
            try writeHex(col, writer, info.got);
        },
        .lengths_mismatch => |info| {
            try writer.writeAll("input:    ");
            try writeHex(col, writer, info.input);
            try writer.print("expected: {} (# bytes)\n", .{info.expected});
            try writer.print("got:      {} (# bytes)\n", .{info.got});
        },
    }
}

pub fn main(init: std.process.Init) !void {
    var args_it = init.minimal.args.iterate();
    _ = args_it.next() orelse @panic("should have initial argument");
    const arg1 = args_it.next() orelse return error.NeedArgument;

    const file = try std.Io.Dir.openFileAbsolute(init.io, arg1, .{});
    defer file.close(init.io);
    var file_buffer: [1024]u8 = undefined;
    var file_reader = file.reader(init.io, &file_buffer);
    var stderr_buffer: [1024]u8 = undefined;
    var stderr_writer = std.Io.File.stderr().writer(init.io, &stderr_buffer);
    const stderr = &stderr_writer.interface;

    var i: usize = 0;
    var root_node = std.Progress.start(init.io, .{ .root_name = "tests" });
    var current_part: ?std.Progress.Node = null;
    while (try file_reader.interface.takeDelimiter('\n')) |line| : (i += 1) {
        if (std.mem.startsWith(u8, line, "#")) {
            continue;
        }
        if (std.mem.startsWith(u8, line, "@Part")) {
            if (current_part) |n| n.end();
            const part_num = line["@Part".len];
            const comment = if (extractComment(line)) |e| e[1] else null;
            const part_name = try if (comment) |s|
                std.fmt.allocPrint(init.arena.allocator(), "part {c} ({s})", .{ part_num, s })
            else
                std.fmt.allocPrint(init.arena.allocator(), "part {c}", .{part_num});
            current_part = root_node.start(part_name, 0);
            continue;
        }
        var test_node = current_part.?.start(try std.fmt.allocPrint(init.arena.allocator(), "test (line {})", .{i + 1}), 1);
        defer test_node.end();
        const test_info = try parseTestInfo(init.arena.allocator(), line);

        inline for (comptime std.meta.tags(ColumnType)) |col| {
            const converted = try convertTest(col, test_info, init.arena.allocator());
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
                    try stderr.print(
                        col_name ++ " " ++ form_name ++ " " ++ "test at line {} failed: {?s}\n",
                        .{ i + 1, test_info.comment },
                    );
                    try printFailure(col, stderr, failure);
                    try stderr.flush();
                    return error.Failed;
                }
            }
        }
    }
    root_node.end();
    try stderr.writeAll("All Unicode tests passed!\n");
    try stderr.flush();
}
