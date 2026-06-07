const std = @import("std");
const builtin = @import("builtin");
const c = @import("c");
const flags = @import("flags");
const Allocator = std.mem.Allocator;
const assert = std.debug.assert;

const Algorithm = enum { nfd, nfkd, nfc, nfkc, casefold };
const Encoding = enum { utf8, utf16le, utf16be, utf16 };
const ResolvedEncoding = enum { utf8, utf16le, utf16be };

const version = "0.1.0";

const help = std.fmt.comptimePrint(
    \\xxu {s}
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
    \\  -v, --version                 Print the version and exit.
    \\  -h, --help                    Print this help and exit.
    \\
, .{version});

pub fn main(init: std.process.Init) u8 {
    var stderr = std.Io.File.stderr();
    var stderr_buf: [2048]u8 = undefined;
    var stderr_writer = stderr.writer(init.io, &stderr_buf);

    var args_it = init.minimal.args.iterate();
    _ = args_it.next();
    const result = flags.parseFromIterator(Flags, args_it, init.gpa) catch {
        stderr_writer.interface.writeAll("xxu: out of memory") catch return 1;
        stderr_writer.interface.flush() catch return 1;
        return 1;
    };
    const options = switch (result) {
        .flags => |f| f,
        .err => |e| {
            stderr_writer.interface.writeAll("xxu: ") catch return 1;
            flags.printErrorDefault(Flags, &stderr_writer.interface, e) catch return 1;
            stderr_writer.interface.writeByte('\n') catch return 1;
            stderr_writer.interface.flush() catch return 1;
            return 1;
        },
    };

    if (options.help) {
        stderr_writer.interface.writeAll(help) catch return 1;
        stderr_writer.interface.flush() catch return 1;
        return 0;
    }
    if (options.version) {
        var stdout = std.Io.File.stdout();
        var stdout_buf: [2048]u8 = undefined;
        var stdout_writer = stdout.writer(init.io, &stdout_buf);
        stdout_writer.interface.print("xxu {s}\n", .{version}) catch return 1;
        stdout_writer.interface.flush() catch return 1;
        return 0;
    }

    const output_file = if (options.output) |path|
        std.Io.Dir.cwd().createFile(init.io, path, .{}) catch |e| {
            stderr_writer.interface.print("xxu: error creating '{s}': ", .{path}) catch return 1;
            writeError(&stderr_writer.interface, e) catch return 1;
            stderr_writer.interface.writeByte('\n') catch return 1;
            stderr_writer.interface.flush() catch return 1;
            return 1;
        }
    else
        std.Io.File.stdout();
    defer if (options.output != null) output_file.close(init.io);
    const input = if (options.input) |path|
        std.Io.Dir.cwd().openFile(init.io, path, .{}) catch |e| {
            stderr_writer.interface.print("xxu: error opening '{s}': ", .{path}) catch return 1;
            writeError(&stderr_writer.interface, e) catch return 1;
            stderr_writer.interface.writeByte('\n') catch return 1;
            stderr_writer.interface.flush() catch return 1;
            return 1;
        }
    else
        std.Io.File.stdin();
    defer if (options.input != null) input.close(init.io);

    run(
        init.io,
        init.gpa,
        input,
        output_file,
        options.algorithm,
        options.encoding,
        options.bom,
    ) catch |e| {
        stderr_writer.interface.print(
            "xxu: error running '{s}' on input: ",
            .{@tagName(options.algorithm)},
        ) catch return 1;
        writeError(&stderr_writer.interface, e) catch return 1;
        stderr_writer.interface.writeByte('\n') catch return 1;
        stderr_writer.interface.flush() catch return 1;
        return 1;
    };
    return 0;
}

fn writeError(writer: *std.Io.Writer, e: anyerror) std.Io.Writer.Error!void {
    const name = @errorName(e);
    for (name, 0..) |b, i| {
        if (std.ascii.isUpper(b)) {
            if (i > 0) {
                try writer.writeByte(' ');
            }
            try writer.writeByte(std.ascii.toLower(b));
        } else {
            try writer.writeByte(b);
        }
    }
}

const Flags = struct {
    input: ?[]const u8,
    algorithm: Algorithm,
    encoding: Encoding = .utf8,
    output: ?[]const u8,
    bom: bool,
    help: bool,
    version: bool,

    pub const positionals = .{
        .input = void,
    };
    pub const shorts = .{
        .algorithm = 'x',
        .encoding = 'e',
        .output = 'o',
        .help = 'h',
        .version = 'v',
    };
    pub const standalone = .{
        .help = void,
        .version = void,
    };
};

const ImplementationFn = fn ([*c]const u8, usize, [*c]u8) callconv(.c) usize;
const ImplementationCheckFn = fn ([*c]const u8, usize, [*c]usize) callconv(.c) bool;
const ImplementationFindFn = fn ([*c]const u8, usize) callconv(.c) usize;
const Implementation = struct {
    algorithm: Algorithm,
    encoding: ResolvedEncoding,
    impl: ImplementationFn,
    checkImpl: ImplementationCheckFn,
    lastStableImpl: ?ImplementationFindFn,
};
const implementations: []const Implementation = &.{
    .{
        .algorithm = .nfd,
        .encoding = .utf8,
        .impl = c.xxutf_normalize_utf8_nfd,
        .checkImpl = c.xxutf_normalize_utf8_nfd_check,
        .lastStableImpl = c.xxutf_find_last_stable_utf8_nfd,
    },
    .{
        .algorithm = .nfkd,
        .encoding = .utf8,
        .impl = c.xxutf_normalize_utf8_nfkd,
        .checkImpl = c.xxutf_normalize_utf8_nfkd_check,
        .lastStableImpl = c.xxutf_find_last_stable_utf8_nfkd,
    },
    .{
        .algorithm = .nfc,
        .encoding = .utf8,
        .impl = c.xxutf_normalize_utf8_nfc,
        .checkImpl = c.xxutf_normalize_utf8_nfc_check,
        .lastStableImpl = c.xxutf_find_last_stable_utf8_nfc,
    },
    .{
        .algorithm = .nfkc,
        .encoding = .utf8,
        .impl = c.xxutf_normalize_utf8_nfkc,
        .checkImpl = c.xxutf_normalize_utf8_nfkc_check,
        .lastStableImpl = c.xxutf_find_last_stable_utf8_nfkc,
    },
    .{
        .algorithm = .casefold,
        .encoding = .utf8,
        .impl = c.xxutf_casefold_utf8,
        .checkImpl = c.xxutf_casefold_utf8_check,
        .lastStableImpl = null,
    },
    .{
        .algorithm = .nfd,
        .encoding = .utf16le,
        .impl = c.xxutf_normalize_utf16le_nfd,
        .checkImpl = c.xxutf_normalize_utf16le_nfd_check,
        .lastStableImpl = c.xxutf_find_last_stable_utf16le_nfd,
    },
    .{
        .algorithm = .nfkd,
        .encoding = .utf16le,
        .impl = c.xxutf_normalize_utf16le_nfkd,
        .checkImpl = c.xxutf_normalize_utf16le_nfkd_check,
        .lastStableImpl = c.xxutf_find_last_stable_utf16le_nfkd,
    },
    .{
        .algorithm = .nfc,
        .encoding = .utf16le,
        .impl = c.xxutf_normalize_utf16le_nfc,
        .checkImpl = c.xxutf_normalize_utf16le_nfc_check,
        .lastStableImpl = c.xxutf_find_last_stable_utf16le_nfc,
    },
    .{
        .algorithm = .nfkc,
        .encoding = .utf16le,
        .impl = c.xxutf_normalize_utf16le_nfkc,
        .checkImpl = c.xxutf_normalize_utf16le_nfkc_check,
        .lastStableImpl = c.xxutf_find_last_stable_utf16le_nfkc,
    },
    .{
        .algorithm = .casefold,
        .encoding = .utf16le,
        .impl = c.xxutf_casefold_utf16le,
        .checkImpl = c.xxutf_casefold_utf16le_check,
        .lastStableImpl = null,
    },
    .{
        .algorithm = .nfd,
        .encoding = .utf16be,
        .impl = c.xxutf_normalize_utf16be_nfd,
        .checkImpl = c.xxutf_normalize_utf16be_nfd_check,
        .lastStableImpl = c.xxutf_find_last_stable_utf16be_nfd,
    },
    .{
        .algorithm = .nfkd,
        .encoding = .utf16be,
        .impl = c.xxutf_normalize_utf16be_nfkd,
        .checkImpl = c.xxutf_normalize_utf16be_nfkd_check,
        .lastStableImpl = c.xxutf_find_last_stable_utf16be_nfkd,
    },
    .{
        .algorithm = .nfc,
        .encoding = .utf16be,
        .impl = c.xxutf_normalize_utf16be_nfc,
        .checkImpl = c.xxutf_normalize_utf16be_nfc_check,
        .lastStableImpl = c.xxutf_find_last_stable_utf16be_nfc,
    },
    .{
        .algorithm = .nfkc,
        .encoding = .utf16be,
        .impl = c.xxutf_normalize_utf16be_nfkc,
        .checkImpl = c.xxutf_normalize_utf16be_nfkc_check,
        .lastStableImpl = c.xxutf_find_last_stable_utf16be_nfkc,
    },
    .{
        .algorithm = .casefold,
        .encoding = .utf16be,
        .impl = c.xxutf_casefold_utf16be,
        .checkImpl = c.xxutf_casefold_utf16be_check,
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
    io: std.Io,
    allocator: Allocator,
    input: std.Io.File,
    output_file: std.Io.File,
    algorithm: Algorithm,
    encoding: Encoding,
    write_bom: bool,
) Error!void {
    var output_buffer: [read_buf_size]u8 = undefined;
    var output_writer = output_file.writer(io, &output_buffer);
    // Start with a guess of two times the size of the read buffer
    var out_buf = try allocator.alloc(u8, read_buf_size * 2);
    defer allocator.free(out_buf);

    var io_reader_buf: [4096]u8 = undefined;
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
    var input_reader = input.reader(io, &io_reader_buf);
    var nread = input_reader.interface.readSliceShort(read_buf[carry_end..]) catch return error.ReadFailed;
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
    var checkImpl: *const ImplementationCheckFn = undefined;
    var lastStableImpl: ?*const ImplementationFindFn = undefined;
    inline for (implementations) |implementation| {
        if (algorithm == implementation.algorithm and resolved_encoding == implementation.encoding) {
            impl = implementation.impl;
            checkImpl = implementation.checkImpl;
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
        var out_length: usize = undefined;
        const check = checkImpl(trimmed.ptr, trimmed.len, &out_length);
        // If `check` is true, we can skip the work of writing using `impl`
        const final_buf: []const u8 = if (check) trimmed else out: {
            if (out_length > out_buf.len) {
                allocator.free(out_buf);
                out_buf = try allocator.alloc(u8, out_length);
            }
            const nwritten = impl(trimmed.ptr, trimmed.len, out_buf.ptr);
            break :out out_buf[0..nwritten];
        };
        // When we have an algorithm that is not closed under string concatenation (such as the
        // normalization algorithms), `last_stable` is the last stable code point for which we
        // can use to concatenate this current outputted result and the next outputted result.
        // We put the `last_stable` and the code points after it into the start of the `read_buf`
        // so that they can be normalized with the next bytes we read from the input.
        const last_stable = if (lastStableImpl) |f|
            f(final_buf.ptr, final_buf.len)
        else
            final_buf.len;
        // Check if we got -1, meaning we found no stable code point in `out_buf`
        if (last_stable == std.math.maxInt(usize)) {
            return error.InvalidInput;
        }
        output_writer.interface.writeAll(final_buf[0..last_stable]) catch return error.WriteFailed;
        const carry_stable = final_buf[last_stable..];
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
        nread = input_reader.interface.readSliceShort(read_buf[carry_end..]) catch return error.ReadFailed;
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
