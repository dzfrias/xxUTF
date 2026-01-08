const std = @import("std");
const builtin = @import("builtin");
const c = @cImport({
    @cInclude("xxutf.h");
});
const flags = @import("flags");
const Allocator = std.mem.Allocator;
const assert = std.debug.assert;

const Algorithm = enum { nfd, nfkd, nfc, nfkc, casefold };
const Encoding = enum { utf8, utf16le, utf16be, utf16 };
const ResolvedEncoding = enum { utf8, utf16le, utf16be };

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

fn mainWithAllocator(allocator: Allocator) !void {
    const args = try std.process.argsAlloc(allocator);
    defer std.process.argsFree(allocator, args);

    // TODO: we might want to write our own argument parser...
    //       Doesn't seem too hard and the current options don't fully fit what I want
    const options = flags.parse(args, "xxu", Flags, .{});

    const output_file = if (options.output) |path|
        try std.fs.cwd().createFile(path, .{})
    else
        std.fs.File.stdout();
    defer if (options.output != null) output_file.close();
    const input = if (options.positional.input) |path|
        try std.fs.cwd().openFile(path, .{})
    else
        std.fs.File.stdin();
    defer if (options.positional.input != null) input.close();

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
    algorithm: Algorithm,
    encoding: Encoding = .utf8,
    output: ?[]const u8,
    bom: bool,
    positional: struct {
        input: ?[]const u8,

        pub const descriptions = .{
            .input = "Set the input file (default: stdin)",
        };
    },

    pub const description =
        \\xxu implements commonly-used locale-independent Unicode algorithms at speed.
        \\
        \\Project home page: https://github.com/dzfrias/xxUTF
    ;

    pub const descriptions = .{
        .algorithm = "Set the Unicode algorithm to run",
        .encoding = "Set the encoding of input and output (default: UTF-8)",
        .output = "Set the output file to write to (default: stdout)",
        .bom = "Add the U+FEFF BOM (byte-order mark) to the output",
    };

    pub const switches = .{
        .algorithm = 'x',
        .encoding = 'e',
        .output = 'o',
    };
};

const ImplementationFn = fn ([*c]const u8, usize, [*c]u8) callconv(.c) usize;
const ImplementationLengthFn = fn ([*c]const u8, usize) callconv(.c) usize;
const Implementation = struct {
    algorithm: Algorithm,
    encoding: ResolvedEncoding,
    impl: ImplementationFn,
    lengthImpl: ImplementationLengthFn,
};
const implementations: []const Implementation = &.{
    .{
        .algorithm = .nfd,
        .encoding = .utf8,
        .impl = c.xxutf_normalize_utf8_nfd,
        .lengthImpl = c.xxutf_normalize_utf8_nfd_length,
    },
    .{
        .algorithm = .nfkd,
        .encoding = .utf8,
        .impl = c.xxutf_normalize_utf8_nfkd,
        .lengthImpl = c.xxutf_normalize_utf8_nfkd_length,
    },
    .{
        .algorithm = .casefold,
        .encoding = .utf8,
        .impl = c.xxutf_casefold_utf8,
        .lengthImpl = c.xxutf_casefold_utf8_length,
    },
    .{
        .algorithm = .nfd,
        .encoding = .utf16le,
        .impl = c.xxutf_normalize_utf16le_nfd,
        .lengthImpl = c.xxutf_normalize_utf16le_nfd_length,
    },
    .{
        .algorithm = .nfkd,
        .encoding = .utf16le,
        .impl = c.xxutf_normalize_utf16le_nfkd,
        .lengthImpl = c.xxutf_normalize_utf16le_nfkd_length,
    },
    .{
        .algorithm = .casefold,
        .encoding = .utf16le,
        .impl = c.xxutf_casefold_utf16le,
        .lengthImpl = c.xxutf_casefold_utf16le_length,
    },
    .{
        .algorithm = .nfd,
        .encoding = .utf16be,
        .impl = c.xxutf_normalize_utf16be_nfd,
        .lengthImpl = c.xxutf_normalize_utf16be_nfd_length,
    },
    .{
        .algorithm = .nfkd,
        .encoding = .utf16be,
        .impl = c.xxutf_normalize_utf16be_nfkd,
        .lengthImpl = c.xxutf_normalize_utf16be_nfkd_length,
    },
    .{
        .algorithm = .casefold,
        .encoding = .utf16be,
        .impl = c.xxutf_casefold_utf16be,
        .lengthImpl = c.xxutf_casefold_utf16be_length,
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

    // We have 3 extra bytes that live at the start of the buffer. These are the "carry" bytes
    // that we preserve partial UTF-8 or UTF-16 from previous reads.
    var read_buf: [read_buf_size + 3]u8 = undefined;
    // We always read `read_buf_size` bytes at this position
    var nread = input.read(read_buf[3..]) catch return error.ReadFailed;
    const resolved_encoding = try resolveEncoding(read_buf[3 .. nread + 3], encoding);

    if (write_bom and !hasBOM(read_buf[3 .. nread + 3], resolved_encoding)) {
        writeBOM(&output_writer.interface, resolved_encoding) catch return error.WriteFailed;
    }

    var impl: *const ImplementationFn = undefined;
    var lengthImpl: *const ImplementationLengthFn = undefined;
    inline for (implementations) |implementation| {
        if (algorithm == implementation.algorithm and resolved_encoding == implementation.encoding) {
            impl = implementation.impl;
            lengthImpl = implementation.lengthImpl;
        }
    }

    var carried: usize = 0;
    while (nread > 0) {
        assert(carried < 4);
        const buf = read_buf[3 - carried .. nread + 3];
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
        // TODO: not technically correct: need "normalize second and append" functionality
        //       to normalize across the boundary.
        output_writer.interface.writeAll(out_buf[0..nwritten]) catch return error.WriteFailed;
        const carry = buf[trimmed.len..];
        carried = carry.len;
        // There is a tiny edge case where we have overlapping regions of memory, so use `memmove`
        // instead of `memcpy`.
        @memmove(read_buf[(3 - carry.len)..3], carry);
        nread = input.read(read_buf[3..]) catch return error.ReadFailed;
    }
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
