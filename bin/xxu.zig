const std = @import("std");
const builtin = @import("builtin");
const c = @cImport({
    @cInclude("xxutf.h");
});
const Allocator = std.mem.Allocator;
const assert = std.debug.assert;
const flags = @import("./flags.zig");

const Algorithm = enum { nfd, nfkd, nfc, nfkc, casefold };
const Encoding = enum { utf8, utf16le, utf16be, utf16 };
const ResolvedEncoding = enum { utf8, utf16le, utf16be };

// TODO: make return u8 and have proper exit codes
pub fn main() !void {
    if (builtin.mode == .Debug) {
        var dbg_allocator: std.heap.DebugAllocator(.{}) = .init;
        defer _ = dbg_allocator.deinit();
        const allocator = dbg_allocator.allocator();
        try mainWithAllocator(allocator);
    } else {
        try mainWithAllocator(std.heap.smp_allocator);
    }
}

const help =
    \\xxu
    \\Diego Frias <mail@dzfrias.dev>
    \\
    \\xxu implements commonly-used locale-independent Unicode algorithms at speed.
    \\This includes normalization and case folding.
    \\
    \\Project home page: https://github.com/dzfrias/xxUTF
    \\
    \\Usage: xxu -x [algorithm] [<file>] [options]
    \\
    \\Flags:
    \\  -x, --algorithm ALGORITHM     Set the Unicode algorithm to run. Available:
    \\                                  nfd
    \\                                  nfc
    \\                                  nfkd
    \\                                  nfkc
    \\                                  casefold
    \\  -e, --encoding ENCODING       Set the encoding of input and output (default: UTF-8).
    \\  -o, --output FILE             Set the output file to write to (default: stdout).
    \\  --bom                         Add the U+FEFF BOM (byte-order mark) to the output.
    \\  -h, --help                    Print this help and exit.
    \\
;

fn mainWithAllocator(allocator: Allocator) !void {
    var stderr = std.fs.File.stderr();
    var stderr_buf: [2048]u8 = undefined;
    var stderr_writer = stderr.writer(&stderr_buf);

    const result = try flags.parseArgs(Flags, allocator);
    const options = switch (result) {
        .flags => |f| f,
        .err => |e| {
            try stderr_writer.interface.writeAll("xxu: ");
            try flags.printErrorDefault(Flags, &stderr_writer.interface, e);
            try stderr_writer.interface.writeByte('\n');
            try stderr_writer.interface.flush();
            return;
        },
    };

    if (options.help) {
        try stderr_writer.interface.writeAll(help);
        try stderr_writer.interface.flush();
        return;
    }

    const output_file = if (options.output) |path|
        try std.fs.cwd().createFile(path, .{})
    else
        std.fs.File.stdout();
    defer if (options.output != null) output_file.close();
    const input = if (options.input) |path|
        try std.fs.cwd().openFile(path, .{})
    else
        std.fs.File.stdin();
    defer if (options.input != null) input.close();

    try run(
        allocator,
        input,
        output_file,
        options.algorithm,
        options.encoding,
        options.bom,
    );
}

const Flags = struct {
    input: ?[]const u8,
    algorithm: Algorithm,
    encoding: Encoding = .utf8,
    output: ?[]const u8,
    bom: bool,
    help: bool,

    pub const positionals = .{
        .input = void,
    };
    pub const shorts = .{
        .algorithm = 'x',
        .encoding = 'e',
        .output = 'o',
        .help = 'h',
    };
};

const ImplementationFn = fn ([*c]const u8, usize, [*c]u8) callconv(.c) usize;
const ImplementationLengthFn = fn ([*c]const u8, usize) callconv(.c) usize;
const Implementation = struct {
    algorithm: Algorithm,
    encoding: ResolvedEncoding,
    impl: ImplementationFn,
    lengthImpl: ImplementationLengthFn,
    lastStableImpl: ?ImplementationLengthFn,
};
const implementations: []const Implementation = &.{
    .{
        .algorithm = .nfd,
        .encoding = .utf8,
        .impl = c.xxutf_normalize_utf8_nfd,
        .lengthImpl = c.xxutf_normalize_utf8_nfd_length,
        .lastStableImpl = c.xxutf_find_last_stable_utf8_nfd,
    },
    .{
        .algorithm = .nfkd,
        .encoding = .utf8,
        .impl = c.xxutf_normalize_utf8_nfkd,
        .lengthImpl = c.xxutf_normalize_utf8_nfkd_length,
        .lastStableImpl = c.xxutf_find_last_stable_utf8_nfkd,
    },
    .{
        .algorithm = .nfc,
        .encoding = .utf8,
        .impl = c.xxutf_normalize_utf8_nfc,
        .lengthImpl = c.xxutf_normalize_utf8_nfc_length,
        .lastStableImpl = c.xxutf_find_last_stable_utf8_nfc,
    },
    .{
        .algorithm = .nfkc,
        .encoding = .utf8,
        .impl = c.xxutf_normalize_utf8_nfkc,
        .lengthImpl = c.xxutf_normalize_utf8_nfkc_length,
        .lastStableImpl = c.xxutf_find_last_stable_utf8_nfkc,
    },
    .{
        .algorithm = .casefold,
        .encoding = .utf8,
        .impl = c.xxutf_casefold_utf8,
        .lengthImpl = c.xxutf_casefold_utf8_length,
        .lastStableImpl = null,
    },
    .{
        .algorithm = .nfd,
        .encoding = .utf16le,
        .impl = c.xxutf_normalize_utf16le_nfd,
        .lengthImpl = c.xxutf_normalize_utf16le_nfd_length,
        .lastStableImpl = c.xxutf_find_last_stable_utf16le_nfd,
    },
    .{
        .algorithm = .nfkd,
        .encoding = .utf16le,
        .impl = c.xxutf_normalize_utf16le_nfkd,
        .lengthImpl = c.xxutf_normalize_utf16le_nfkd_length,
        .lastStableImpl = c.xxutf_find_last_stable_utf16le_nfkd,
    },
    .{
        .algorithm = .nfc,
        .encoding = .utf16le,
        .impl = c.xxutf_normalize_utf16le_nfc,
        .lengthImpl = c.xxutf_normalize_utf16le_nfc_length,
        .lastStableImpl = c.xxutf_find_last_stable_utf16le_nfc,
    },
    .{
        .algorithm = .nfkc,
        .encoding = .utf16le,
        .impl = c.xxutf_normalize_utf16le_nfkc,
        .lengthImpl = c.xxutf_normalize_utf16le_nfkc_length,
        .lastStableImpl = c.xxutf_find_last_stable_utf16le_nfkc,
    },
    .{
        .algorithm = .casefold,
        .encoding = .utf16le,
        .impl = c.xxutf_casefold_utf16le,
        .lengthImpl = c.xxutf_casefold_utf16le_length,
        .lastStableImpl = null,
    },
    .{
        .algorithm = .nfd,
        .encoding = .utf16be,
        .impl = c.xxutf_normalize_utf16be_nfd,
        .lengthImpl = c.xxutf_normalize_utf16be_nfd_length,
        .lastStableImpl = c.xxutf_find_last_stable_utf16be_nfd,
    },
    .{
        .algorithm = .nfkd,
        .encoding = .utf16be,
        .impl = c.xxutf_normalize_utf16be_nfkd,
        .lengthImpl = c.xxutf_normalize_utf16be_nfkd_length,
        .lastStableImpl = c.xxutf_find_last_stable_utf16be_nfkd,
    },
    .{
        .algorithm = .nfc,
        .encoding = .utf16be,
        .impl = c.xxutf_normalize_utf16be_nfc,
        .lengthImpl = c.xxutf_normalize_utf16be_nfc_length,
        .lastStableImpl = c.xxutf_find_last_stable_utf16be_nfc,
    },
    .{
        .algorithm = .nfkc,
        .encoding = .utf16be,
        .impl = c.xxutf_normalize_utf16be_nfkc,
        .lengthImpl = c.xxutf_normalize_utf16be_nfkc_length,
        .lastStableImpl = c.xxutf_find_last_stable_utf16be_nfkc,
    },
    .{
        .algorithm = .casefold,
        .encoding = .utf16be,
        .impl = c.xxutf_casefold_utf16be,
        .lengthImpl = c.xxutf_casefold_utf16be_length,
        .lastStableImpl = null,
    },
};

const Error = error{
    InvalidInput,
    WriteFailed,
    ReadFailed,
    UnresolvedEncoding,
} || std.mem.Allocator.Error;

const read_buf_size = 4096;

fn run(
    allocator: Allocator,
    input: std.fs.File,
    output_file: std.fs.File,
    algorithm: Algorithm,
    encoding: Encoding,
    write_bom: bool,
) Error!void {
    var output_buffer: [read_buf_size]u8 = undefined;
    var output_writer = output_file.writer(&output_buffer);
    // Start with a guess of two times the size of the read buffer
    var out_buf = try allocator.alloc(u8, read_buf_size * 2);
    defer allocator.free(out_buf);

    // We have `read_buf_size + 3` extra bytes that live at the start of the buffer. These are
    // "carry" bytes that preserve two things:
    // 1. Partial UTF-8 or UTF-16 from previous reads
    // 2. (if performing normalization) All code points through the last stable code point under
    //    that normalization form from the previous normalization result
    // The first part is needed because of incomplete code points in UTF-8 and high surrogates in
    // UTF-16. The second part is needed because normalization is not closed under concatenation.
    // See: https://www.unicode.org/reports/tr15/#Concatenation
    // Note that we could keep this carry information in a separate section of memory, and while
    // although that would indeed make the logic simpler, we would then have to perform extra
    // `memcpy` operations on large amounts of text.
    var read_buf: [(read_buf_size * 2) + 3]u8 = undefined;
    // Denotes where the carry section ends in `read_buf`
    const carry_end = read_buf_size + 3;
    // We have `read_buf_size` bytes after `carry_end`
    var nread = input.read(read_buf[carry_end..]) catch return error.ReadFailed;
    const resolved_encoding = try resolveEncoding(
        read_buf[carry_end .. nread + carry_end],
        encoding,
    );

    if (write_bom and
        !hasBOM(read_buf[read_buf_size + 3 .. nread + read_buf_size + 3], resolved_encoding))
    {
        writeBOM(&output_writer.interface, resolved_encoding) catch return error.WriteFailed;
    }

    var impl: *const ImplementationFn = undefined;
    var lengthImpl: *const ImplementationLengthFn = undefined;
    var lastStableImpl: ?*const ImplementationLengthFn = undefined;
    inline for (implementations) |implementation| {
        if (algorithm == implementation.algorithm and resolved_encoding == implementation.encoding) {
            impl = implementation.impl;
            lengthImpl = implementation.lengthImpl;
            lastStableImpl = if (implementation.lastStableImpl) |f| f else null;
        }
    }

    // Keeps track of how many bytes (from partials/stable code points) have been carried
    // over from the last iteration.
    var carried: usize = 0;
    while (nread > 0) {
        const buf = read_buf[carry_end - carried .. carry_end + nread];
        // Trim partial Unicode text. The trimmed bytes will be put at the start of the read
        // buffer at the end of this iteration.
        const trimmed = trimPartialUnicode(buf, resolved_encoding);
        if (!validateUnicode(trimmed, resolved_encoding)) {
            return error.InvalidInput;
        }
        // Compute the size that we need to run the implementation function. Resize if needed.
        const out_length = lengthImpl(trimmed.ptr, trimmed.len);
        if (out_length > out_buf.len) {
            allocator.free(out_buf);
            out_buf = try allocator.alloc(u8, out_length);
        }
        const nwritten = impl(trimmed.ptr, trimmed.len, out_buf.ptr);
        // When we have an algorithm that is not closed under string concatenation (such as the
        // normalization algorithms), `last_stable` is the last stable code point for which we
        // can use to concatenate this current outputted result and the next outputted result.
        // We put the `last_stable` and the code points after it into the start of the `read_buf`
        // so that they can be normalized with the next bytes we read from the input.
        const last_stable = if (lastStableImpl) |f| f(out_buf.ptr, nwritten) else nwritten;
        // Check if we got -1, meaning we found no stable code point in `out_buf`
        if (last_stable == std.math.maxInt(usize)) {
            return error.InvalidInput;
        }
        output_writer.interface.writeAll(out_buf[0..last_stable]) catch return error.WriteFailed;
        const carry_stable = out_buf[last_stable..nwritten];
        const carry_partial = buf[trimmed.len..];
        assert(carry_partial.len < 4);
        carried = carry_stable.len + carry_partial.len;
        // There is a tiny edge case where we have overlapping regions of memory, so use `memmove`
        // instead of `memcpy`.
        @memmove(read_buf[carry_end - carry_partial.len .. carry_end], carry_partial);
        @memcpy(
            read_buf[(carry_end - carry_partial.len) - carry_stable.len .. carry_end - carry_partial.len],
            carry_stable,
        );
        nread = input.read(read_buf[carry_end..]) catch return error.ReadFailed;
    }
    // Flush the leftover carry bytes
    const leftover = read_buf[carry_end - carried .. carry_end];
    if (!validateUnicode(leftover, resolved_encoding)) {
        return error.InvalidInput;
    }
    output_writer.interface.writeAll(leftover) catch return error.WriteFailed;

    output_writer.interface.flush() catch return error.WriteFailed;
}

fn resolveEncoding(buf: []const u8, encoding: Encoding) error{UnresolvedEncoding}!ResolvedEncoding {
    return switch (encoding) {
        .utf8 => .utf8,
        .utf16le => .utf16le,
        .utf16be => .utf16be,
        .utf16 => resolved: {
            if (buf.len < 2) {
                break :resolved error.UnresolvedEncoding;
            }
            break :resolved if (buf[0] == 0xFF and buf[1] == 0xFE)
                .utf16le
            else if (buf[0] == 0xFE and buf[1] == 0xFF)
                .utf16be
            else
                error.UnresolvedEncoding;
        },
    };
}

fn hasBOM(buf: []const u8, encoding: ResolvedEncoding) bool {
    return switch (encoding) {
        .utf8 => buf.len >= 3 and buf[0] == 0xEF and buf[1] == 0xBB and buf[2] == 0xBF,
        .utf16le => buf.len >= 2 and buf[0] == 0xFF and buf[1] == 0xFE,
        .utf16be => buf.len >= 2 and buf[0] == 0xFE and buf[1] == 0xFF,
    };
}

fn writeBOM(writer: *std.Io.Writer, encoding: ResolvedEncoding) !void {
    return switch (encoding) {
        .utf8 => writer.writeAll(&.{ 0xEF, 0xBB, 0xBF }),
        .utf16le => writer.writeAll(&.{ 0xFF, 0xFE }),
        .utf16be => writer.writeAll(&.{ 0xFE, 0xFF }),
    };
}

fn trimPartialUnicode(buf: []const u8, encoding: ResolvedEncoding) []const u8 {
    return switch (encoding) {
        .utf8 => trimPartialUTF8(buf),
        .utf16le => trimPartialUTF16(buf, .little),
        .utf16be => trimPartialUTF16(buf, .big),
    };
}

fn trimPartialUTF8(buf: []const u8) []const u8 {
    if (buf.len > 0 and buf[buf.len - 1] >= 0xC0) {
        return buf[0 .. buf.len - 1];
    }
    if (buf.len > 1 and buf[buf.len - 2] >= 0xE0) {
        return buf[0 .. buf.len - 2];
    }
    if (buf.len > 2 and buf[buf.len - 3] >= 0xF0) {
        return buf[0 .. buf.len - 3];
    }
    return buf;
}

fn trimPartialUTF16(input: []const u8, endian: std.builtin.Endian) []const u8 {
    if (input.len < 2) {
        return &.{};
    }
    const last_code_unit_pos = input.len - (2 + input.len % 2);
    assert(last_code_unit_pos % 2 == 0);
    const code_unit = std.mem.readInt(
        u16,
        &.{ input[last_code_unit_pos], input[last_code_unit_pos + 1] },
        endian,
    );
    return if (std.unicode.utf16IsHighSurrogate(code_unit))
        return input[0..last_code_unit_pos -| 2]
    else
        return input;
}

fn validateUnicode(buf: []const u8, encoding: ResolvedEncoding) bool {
    return switch (encoding) {
        .utf8 => std.unicode.utf8ValidateSlice(buf),
        .utf16le => validateUtf16(buf, .little),
        .utf16be => validateUtf16(buf, .big),
    };
}

fn validateUtf16(buf: []const u8, endian: std.builtin.Endian) bool {
    if (buf.len % 2 != 0) {
        return false;
    }
    var p: usize = 0;
    while (p < buf.len) : (p += 2) {
        const code_unit = std.mem.readInt(u16, &.{ buf[p], buf[p + 1] }, endian);
        if (std.unicode.utf16IsHighSurrogate(code_unit)) {
            p += 2;
            const low = std.mem.readInt(u16, &.{ buf[p], buf[p + 1] }, endian);
            _ = std.unicode.utf16DecodeSurrogatePair(&.{ code_unit, low }) catch return false;
        }
    }
    return true;
}

test {
    _ = flags;
}
