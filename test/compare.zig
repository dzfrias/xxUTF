const std = @import("std");
const c = @import("c");
const builtin = @import("builtin");
const Allocator = std.mem.Allocator;
const assert = std.debug.assert;
const native_endian = builtin.cpu.arch.endian();

pub fn main(init: std.process.Init) !void {
    var stdout_buf: [2048]u8 = undefined;
    const stdout = std.Io.File.stdout();
    var stdout_writer = stdout.writer(init.io, &stdout_buf);
    var stderr_buf: [2048]u8 = undefined;
    const stderr = std.Io.File.stderr();
    var stderr_writer = stderr.writer(init.io, &stderr_buf);
    var stdin_buf: [2048]u8 = undefined;
    const stdin = std.Io.File.stdin();
    var stdin_reader = stdin.reader(init.io, &stdin_buf);
    while (try stdin_reader.interface.takeDelimiter('\n')) |line| {
        const parsed = parse(init.gpa, line) catch |e| switch (e) {
            error.ParseError => {
                try stderr_writer.interface.writeAll("compare: invalid line input\n");
                try stderr_writer.interface.flush();
                continue;
            },
            else => return e,
        };
        defer if (parsed) |s| init.gpa.free(s);
        const input = parsed orelse line;
        if (!std.unicode.utf8ValidateSlice(input)) {
            try stderr_writer.interface.writeAll("compare: must provide valid UTF-8\n");
            try stderr_writer.interface.flush();
            continue;
        }
        inline for (comptime std.meta.tags(Encoding)) |encoding| {
            inline for (comptime std.meta.tags(Form)) |form| {
                if (normalizeXxUtf(encoding, form, init.gpa, input)) |normalized_xxutf| {
                    defer init.gpa.free(normalized_xxutf);
                    const normalized_icu = try normalizeIcu(encoding, form, init.gpa, input);
                    defer init.gpa.free(normalized_icu);
                    if (std.mem.eql(EncodingUnit(encoding), normalized_icu, normalized_xxutf)) {
                        try stdout_writer.interface.writeAll(std.fmt.comptimePrint(
                            "Equal ({s}, {s})\n",
                            comptime .{ encoding.name(), form.name() },
                        ));
                    } else {
                        try stdout_writer.interface.writeAll(std.fmt.comptimePrint(
                            "Not equal ({s}, {s})\n",
                            comptime .{ encoding.name(), form.name() },
                        ));
                    }
                } else |e| switch (e) {
                    error.BadCheck => {
                        try stdout_writer.interface.writeAll(std.fmt.comptimePrint(
                            "Bad check ({s}, {s})",
                            comptime .{ encoding.name(), form.name() },
                        ));
                    },
                    else => return e,
                }
            }
        }
        try stdout_writer.flush();
    }
}

fn parse(allocator: Allocator, input: []const u8) (error{ParseError} || Allocator.Error)!?[]const u8 {
    var parsed: std.ArrayList(u8) = .empty;
    errdefer parsed.deinit(allocator);
    var remaining = input;
    var repeat_start: usize = 0;
    while (std.mem.findScalar(u8, remaining, '\\')) |pos| {
        if (pos == remaining.len - 1) {
            break;
        }
        try parsed.appendSlice(allocator, remaining[0..pos]);
        if (remaining[pos + 1] == '\\') {
            try parsed.append(allocator, '\\');
            remaining = remaining[pos + 2 ..];
            continue;
        }
        remaining = remaining[pos + 1 ..];
        const directive_end = std.mem.findScalar(u8, remaining, '{') orelse return error.ParseError;
        const directive = remaining[0..directive_end];
        const value_end = std.mem.findScalar(u8, remaining, '}') orelse return error.ParseError;
        const value = remaining[directive_end + 1 .. value_end];
        if (std.mem.eql(u8, "u", directive)) {
            const code_point = std.fmt.parseInt(u21, value, 16) catch return error.ParseError;
            var out: [4]u8 = undefined;
            const nwritten = std.unicode.utf8Encode(code_point, &out) catch return error.ParseError;
            try parsed.appendSlice(allocator, out[0..nwritten]);
        } else if (std.mem.eql(u8, "repeat", directive)) {
            const n = std.fmt.parseInt(u32, value, 10) catch return error.ParseError;
            try parsed.ensureUnusedCapacity(allocator, n * parsed.items.len);
            const to_copy = parsed.items[repeat_start..];
            for (0..n) |_| {
                try parsed.appendSlice(allocator, to_copy);
            }
            repeat_start = parsed.items.len;
        } else {
            return error.ParseError;
        }
        remaining = remaining[value_end + 1 ..];
    }
    if (parsed.items.len == 0) {
        return null;
    }
    try parsed.appendSlice(allocator, remaining);
    return try parsed.toOwnedSlice(allocator);
}

const Encoding = enum {
    utf8,
    utf16le,
    utf16be,

    pub fn name(encoding: Encoding) [:0]const u8 {
        return switch (encoding) {
            .utf8 => "UTF-8",
            .utf16le => "UTF-16LE",
            .utf16be => "UTF-16BE",
        };
    }
};

const Form = enum {
    nfd,
    nfc,
    nfkd,
    nfkc,
    case_fold,

    pub fn name(form: Form) [:0]const u8 {
        return switch (form) {
            .nfd => "NFD",
            .nfc => "NFC",
            .nfkd => "NFKD",
            .nfkc => "NFKC",
            .case_fold => "Case fold",
        };
    }
};

const XxUtfWriteFn = fn ([*]const u8, usize, [*]u8) callconv(.c) usize;
const XxUtfCheckFn = fn ([*]const u8, usize, *usize) callconv(.c) bool;

fn EncodingUnit(comptime encoding: Encoding) type {
    return switch (encoding) {
        .utf8 => u8,
        .utf16le, .utf16be => u16,
    };
}

fn normalizeIcu(
    comptime encoding: Encoding,
    comptime form: Form,
    allocator: Allocator,
    input: []const u8,
) Allocator.Error![]const EncodingUnit(encoding) {
    var status: c.UErrorCode = c.U_ZERO_ERROR;
    var ustring_len: i32 = undefined;
    // Pre-flight for the length
    _ = c.shim_u_strFromUTF8(null, 0, &ustring_len, input.ptr, @intCast(input.len), &status);
    assert(status == c.U_BUFFER_OVERFLOW_ERROR);
    status = c.U_ZERO_ERROR;
    const ustring = try allocator.alloc(c.UChar, @intCast(ustring_len));
    defer allocator.free(ustring);
    _ = c.shim_u_strFromUTF8(ustring.ptr, ustring_len, null, input.ptr, @intCast(input.len), &status);
    // It's okay if it's not terminated because we have the length
    assert(status == c.U_STRING_NOT_TERMINATED_WARNING);
    status = c.U_ZERO_ERROR;
    const normalized = if (form != .case_fold) blk: {
        const normalizerFn = switch (form) {
            .nfd => c.shim_unorm2_getNFDInstance,
            .nfc => c.shim_unorm2_getNFCInstance,
            .nfkd => c.shim_unorm2_getNFKDInstance,
            .nfkc => c.shim_unorm2_getNFKCInstance,
            .case_fold => unreachable,
        };
        const normalizer = normalizerFn(&status);
        assert(status == c.U_ZERO_ERROR);
        const normalized_len = c.shim_unorm2_normalize(
            normalizer,
            ustring.ptr,
            @intCast(ustring.len),
            null,
            0,
            &status,
        );
        assert(status == c.U_BUFFER_OVERFLOW_ERROR);
        status = c.U_ZERO_ERROR;
        const normalized = try allocator.alloc(c.UChar, @intCast(normalized_len));
        errdefer allocator.free(normalized);
        _ = c.shim_unorm2_normalize(
            normalizer,
            ustring.ptr,
            @intCast(ustring.len),
            normalized.ptr,
            @intCast(normalized.len),
            &status,
        );
        assert(status == c.U_STRING_NOT_TERMINATED_WARNING);
        status = c.U_ZERO_ERROR;
        break :blk normalized;
    } else blk: {
        const folded_len = c.shim_u_strFoldCase(
            null,
            0,
            ustring.ptr,
            @intCast(ustring.len),
            c.U_FOLD_CASE_DEFAULT,
            &status,
        );
        assert(status == c.U_BUFFER_OVERFLOW_ERROR);
        status = c.U_ZERO_ERROR;
        const folded = try allocator.alloc(c.UChar, @intCast(folded_len));
        errdefer allocator.free(folded);
        _ = c.shim_u_strFoldCase(
            folded.ptr,
            folded_len,
            ustring.ptr,
            @intCast(ustring.len),
            c.U_FOLD_CASE_DEFAULT,
            &status,
        );
        // It's okay if it's not terminated because we have the length
        assert(status == c.U_STRING_NOT_TERMINATED_WARNING);
        status = c.U_ZERO_ERROR;
        break :blk folded;
    };
    errdefer allocator.free(normalized);

    return switch (encoding) {
        .utf8 => blk: {
            var encoded_len: i32 = undefined;
            _ = c.shim_u_strToUTF8(
                null,
                0,
                &encoded_len,
                normalized.ptr,
                @intCast(normalized.len),
                &status,
            );
            assert(status == c.U_BUFFER_OVERFLOW_ERROR);
            status = c.U_ZERO_ERROR;
            const encoded = try allocator.alloc(u8, @intCast(encoded_len));
            _ = c.shim_u_strToUTF8(
                encoded.ptr,
                encoded_len,
                null,
                normalized.ptr,
                @intCast(normalized.len),
                &status,
            );
            allocator.free(normalized);
            break :blk encoded;
        },
        .utf16le => blk: {
            if (native_endian == .big) {
                std.mem.byteSwapAllElements(u16, normalized);
            }
            break :blk normalized;
        },
        .utf16be => blk: {
            if (native_endian == .little) {
                std.mem.byteSwapAllElements(u16, normalized);
            }
            break :blk normalized;
        },
    };
}

const XxUtfNormalizeError = error{BadCheck} || Allocator.Error;
fn normalizeXxUtf(
    comptime encoding: Encoding,
    comptime form: Form,
    allocator: Allocator,
    input: []const u8,
) XxUtfNormalizeError![]const EncodingUnit(encoding) {
    const writeFn: XxUtfWriteFn, const checkFn: XxUtfCheckFn = switch (form) {
        .nfd => switch (encoding) {
            .utf8 => .{ c.xxutf_normalize_utf8_nfd, c.xxutf_normalize_utf8_nfd_check },
            .utf16le => .{ c.xxutf_normalize_utf16le_nfd, c.xxutf_normalize_utf16le_nfd_check },
            .utf16be => .{ c.xxutf_normalize_utf16be_nfd, c.xxutf_normalize_utf16be_nfd_check },
        },
        .nfc => switch (encoding) {
            .utf8 => .{ c.xxutf_normalize_utf8_nfc, c.xxutf_normalize_utf8_nfc_check },
            .utf16le => .{ c.xxutf_normalize_utf16le_nfc, c.xxutf_normalize_utf16le_nfc_check },
            .utf16be => .{ c.xxutf_normalize_utf16be_nfc, c.xxutf_normalize_utf16be_nfc_check },
        },
        .nfkd => switch (encoding) {
            .utf8 => .{ c.xxutf_normalize_utf8_nfkd, c.xxutf_normalize_utf8_nfkd_check },
            .utf16le => .{ c.xxutf_normalize_utf16le_nfkd, c.xxutf_normalize_utf16le_nfkd_check },
            .utf16be => .{ c.xxutf_normalize_utf16be_nfkd, c.xxutf_normalize_utf16be_nfkd_check },
        },
        .nfkc => switch (encoding) {
            .utf8 => .{ c.xxutf_normalize_utf8_nfkc, c.xxutf_normalize_utf8_nfkc_check },
            .utf16le => .{ c.xxutf_normalize_utf16le_nfkc, c.xxutf_normalize_utf16le_nfkc_check },
            .utf16be => .{ c.xxutf_normalize_utf16be_nfkc, c.xxutf_normalize_utf16be_nfkc_check },
        },
        .case_fold => switch (encoding) {
            .utf8 => .{ c.xxutf_casefold_utf8, c.xxutf_casefold_utf8_check },
            .utf16le => .{ c.xxutf_casefold_utf16le, c.xxutf_casefold_utf16le_check },
            .utf16be => .{ c.xxutf_casefold_utf16be, c.xxutf_casefold_utf16be_check },
        },
    };
    const encoded_input: []const EncodingUnit(encoding) = switch (encoding) {
        .utf8 => input,
        .utf16le => std.unicode.utf8ToUtf16LeAlloc(allocator, input) catch |e| switch (e) {
            error.InvalidUtf8 => unreachable,
            else => |alloc_err| return alloc_err,
        },
        .utf16be => blk: {
            const utf16be = std.unicode.utf8ToUtf16LeAlloc(allocator, input) catch |e| switch (e) {
                error.InvalidUtf8 => unreachable,
                else => |alloc_err| return alloc_err,
            };
            std.mem.byteSwapAllElements(u16, utf16be);
            break :blk utf16be;
        },
    };
    defer switch (encoding) {
        .utf8 => {},
        .utf16le, .utf16be => allocator.free(encoded_input),
    };
    var out_len: usize = undefined;
    const check = checkFn(std.mem.sliceAsBytes(encoded_input).ptr, std.mem.sliceAsBytes(encoded_input).len, &out_len);
    var normalized_buf = try allocator.alloc(EncodingUnit(encoding), out_len);
    defer allocator.free(normalized_buf);
    const nwritten = writeFn(
        std.mem.sliceAsBytes(encoded_input).ptr,
        std.mem.sliceAsBytes(encoded_input).len,
        std.mem.sliceAsBytes(normalized_buf).ptr,
    );
    const units_written = nwritten / @sizeOf(EncodingUnit(encoding));
    const normalized = try allocator.alloc(EncodingUnit(encoding), units_written);
    @memcpy(normalized, normalized_buf[0..units_written]);
    if (check and !std.mem.eql(EncodingUnit(encoding), encoded_input, normalized)) {
        return error.BadCheck;
    }
    return normalized;
}
